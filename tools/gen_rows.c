/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as drumlogue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* The panel, written from Felucca's own descriptors.
 *
 * Every unit's rows.def is produced here rather than typed by hand, so no label, range or default
 * can drift from what the engine actually has: they are read out of params.c's TP[] and each
 * engine's edit[] - the very tables the FM-1's own display reads. tools/check_rows.c (built from
 * the same sources) reads a rows.def back and holds it to those tables.
 *
 * The panel is this engine's own sound, and nothing else: the envelope first (always rows 1-4), then
 * the engine's four HOME knobs and its eight parameters, then the envelope's filter amount, the
 * voice mode and the LFO (its rate, its wave, and where it goes: pitch, the filter, the shape, the
 * amp). A parameter is shown once - where a knob already is that row (ANALOG's cutoff, say) the
 * panel closes up rather than showing it twice - and a row this engine has nothing behind is not
 * shown at all: WHEEL and DRUM render no filter, so they have no filter row and no LFO filter
 * amount, and LOFI, WHEEL and DRUM are not shaped, so they have no LFO shape amount.
 *
 * What is not here is the FM-1's track around the engine - level, pan, mute, glide, the LFO's phase
 * and fade, the envelope's shape amount. Level and pan are the drumlogue's own (the unit renders
 * through its part's volume and pan) and mute is the sequencer's, so those three were three of the
 * 24 slots spent on knobs the device already has; the rest lost their slot to the LFO, which a
 * player reaches for far more often than a fade time.
 *
 * Build (host, no container needed):
 *   cc -O1 -o build/gen_rows tools/gen_rows.c -Ishim/gen -Ifelucca/firmware/src -DFLUYT_ALL_ENGINES
 *   build/gen_rows fluyt_analog_drumlogue/rows.def 0
 *
 * The second argument is the unit's number in shim/fluyt_engines.c's FLUYT_FAMILY[], which is the
 * engine and its index in Felucca's own ENGINES[]; build.sh runs this once per unit.
 * FLUYT_ALL_ENGINES compiles every engine of the family into this tool, the way Felucca's own
 * engines.c compiles them. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fluyt_config.h"
#include "fluyt_fmt.h"           /* params.c's formatters (Felucca's libc is not in this TU) */

#include "felucca_tables.h"
#include "core.h"
#include "dsp.c"

/* the engines of the family (the nine units), and the table that numbers them */
#include "eng_analog.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_wheel.c"
#include "eng_drum.c"
#include "eng_noise.c"
#include "eng_fm6.c"

#include "fluyt_engines.c"
#include "params.c"

#include "fluyt_rows.h"          /* FLUYT_T_*: what a row is told to be */

/* a walk entry: the parameter, its own name as C source, and what the engine has to have for the row
 * to be worth a slot. WC/WS are the rows an engine without that part of itself does not get: DRUM
 * and WHEEL render no filter, LOFI, WHEEL and DRUM no shape (that is, their code never reads
 * m->cutoff / m->shape - see the flags in UNITS[]). */
#define W(id) {#id, id, W_ANY}
#define WC(id) {#id, id, W_CUT}
#define WS(id) {#id, id, W_SHP}
enum { W_ANY, W_CUT, W_SHP };    /* what a row needs: nothing, a filter, a shape */

/* one row: the parameter it moves, that parameter's name as C source ("P_E4"), and its condition */
struct row {
  const char *name;
  uint8_t id;
  uint8_t needs;
};

/* the panel, in the order the device shows it; the engine's four knobs go in at WALK_KNOB_AT,
 * because which parameters they are is the engine's own business */
static const struct row WALK[] = {
    W(P_ATK), W(P_DEC), W(P_SUS), W(P_REL),
    W(P_E0), W(P_E1), W(P_E2), W(P_E3), W(P_E4), W(P_E5), W(P_E6), W(P_E7),
    WC(P_ED_FLT),
    W(P_VOICE),
    W(P_LRATE), W(P_LWAVE),
    W(P_LD_PIT), WC(P_LD_FLT), WS(P_LD_SHP), W(P_LD_AMP),
};
#define WALK_KNOB_AT 4u          /* after the envelope, before the engine's parameters */
#define NWALK (sizeof(WALK) / sizeof(WALK[0]))
#define ROW_MAX 24u              /* UNIT_MAX_PARAM_COUNT */

static struct row rows[ROW_MAX];
static uint32_t nrows;
static const engine_t *eng;      /* the unit's engine (row_desc) */
static uint8_t needs[3];         /* W_ANY/W_CUT/W_SHP: what this engine has, from UNITS[] */
static char knob_name[4][12];    /* for a knob whose parameter the walk does not name (unlikely) */

/* a parameter's own name, as the walk has it */
static const char *name_of(uint32_t id, uint32_t slot)
{
  uint32_t i;
  for (i = 0; i < NWALK; i++)
    if (WALK[i].id == id)
      return WALK[i].name;
  snprintf(knob_name[slot & 3u], sizeof knob_name[0], "P_E0 + %u", (unsigned)(id - P_E0));
  return knob_name[slot & 3u];
}

static int seen(uint32_t id)
{
  uint32_t i;
  for (i = 0; i < nrows; i++)
    if (rows[i].id == id)
      return 1;
  return 0;
}

/* Felucca's own descriptor for a parameter, without the engine's desc hook: a row's label is fixed,
 * where a descriptor's may depend on the mode the engine is in */
static const param_desc_t *row_desc(const engine_t *e, uint32_t id);

/* one row, unless the walk has already taken that parameter, this engine has nothing behind it (a
 * row whose condition the engine does not meet), the parameter is one of Felucca's inert ones (a
 * label of "-" and no range at all: PHASE leaves P_E7 so), or the panel is full */
static void add(const struct row *r)
{
  if (nrows >= ROW_MAX || seen(r->id) || !needs[r->needs] ||
      row_desc(eng, r->id)->min == row_desc(eng, r->id)->max)
    return;
  rows[nrows++] = *r;
}

/* Felucca's own descriptor for a parameter, without the engine's desc hook: a row's label is fixed,
 * where a descriptor's may depend on the mode the engine is in */
static const param_desc_t *row_desc(const engine_t *e, uint32_t id)
{
  return id >= P_E0 ? &e->edit[id - P_E0] : &TP[id];
}

/* what the drumlogue is told the row is: almost every row of Felucca's is an index into one of its
 * tables, and the device is shown Felucca's own formatting of it. A switch and a true percentage
 * would be the exceptions - a row where the number is the display - and no row of any Fluyt panel is
 * either today (both of those were the mix rows, which the panel leaves off). */
static const char *type_name(const param_desc_t *d)
{
  if (d->names == N_ONOFF)                       /* the 0/1 switches: MUTE, ARP HOLD */
    return "FLUYT_T_ONOFF";
  if (d->fmt == F_PCT && d->min == 0 && d->max == 100)
    return "FLUYT_T_PCT";
  return "FLUYT_T_STR";
}

/* Fluyt's nine units: the name this repository calls each engine, the Felucca source and symbol it
 * is, the note a sequencer step plays (the DRUM kit is General MIDI numbered, so its step is the
 * kick; every other engine is melodic and plays middle C, as bg1-va_drumlogue does), and the two
 * parts of itself an engine may not have: a cutoff it never reads (m->cutoff, so no filter row and
 * no LFO filter amount) and a shape (m->shape, so no LFO shape amount). Both are read out of the
 * engine's own code - `grep -c 'm->cutoff' felucca/firmware/src/eng_*.c`, 0 for eng_wheel.c and
 * eng_drum.c, and eng_lofi.c reads a cutoff but no shape - and the generator writes them into
 * config.mk as FLUYT_ENGINE_CUTOFF/FLUYT_ENGINE_SHAPE, which tools/check_rows.c holds the panel to.
 * The engines themselves are FLUYT_FAMILY[]'s (shim/fluyt_engines.c), in the same order - checked
 * below, so the numbering the units are built with cannot come apart from the numbering they render
 * with. */
static const struct unit {
  const char *name;
  const char *src;
  const char *sym;
  const engine_t *eng;
  uint8_t gate;
  uint8_t cutoff, shape;
} UNITS[] = {
    {"ANALOG", "eng_analog.c", "ENG_ANALOG", &ENG_ANALOG, 60, 1, 1},
    {"TRIO", "eng_trio.c", "ENG_TRIO", &ENG_TRIO, 60, 1, 1},
    {"PHASE", "eng_phase.c", "ENG_PHASE", &ENG_PHASE, 60, 1, 1},
    {"LOFI", "eng_lofi.c", "ENG_LOFI", &ENG_LOFI, 60, 1, 0},
    {"WHEEL", "eng_wheel.c", "ENG_WHEEL", &ENG_WHEEL, 60, 0, 0},
    {"NOISE", "eng_noise.c", "ENG_NOISE", &ENG_NOISE, 60, 1, 1},
    {"FORMANT", "eng_formant.c", "ENG_FORMANT", &ENG_FORMANT, 60, 1, 1},
    {"DRUM", "eng_drum.c", "ENG_DRUM", &ENG_DRUM, 36, 0, 0},
    {"FM6", "eng_fm6.c", "ENG_FM6", &ENG_FM6, 60, 1, 1},
};
#define NUNITS (sizeof(UNITS) / sizeof(UNITS[0]))

/* the unit directory a name gets: fluyt_analog_drumlogue */
static void unit_dir(char *out, size_t n, const char *name)
{
  size_t i = 0;
  i += (size_t)snprintf(out + i, n - i, "fluyt_");
  for (; *name && i + 8u < n; name++)
    out[i++] = (char)(*name >= 'A' && *name <= 'Z' ? *name - 'A' + 'a' : *name);
  snprintf(out + i, n - i, "_drumlogue");
}

/* the unit's config.mk: what the Makefile needs to compile this one engine, written from the same
 * facts rows.def comes from. `which` is the unit's place in the Fluyt family, `Felucca` is that
 * engine's own number in Felucca's ENGINES[] - the two are never the same number and both are
 * needed: the first names the unit, the second is what the engine's code is told it is (a unit
 * whose engine is DRUM has to know that, and Felucca's presets are numbered by it). */
static int write_config(const char *dir, const struct unit *u, uint32_t which, uint32_t felucca,
                        uint32_t npresets)
{
  char path[512], proj[64], d[64];
  FILE *f;
  unit_dir(d, sizeof d, u->name);
  snprintf(proj, sizeof proj, "fluyt_%s", d + 6);        /* fluyt_analog (the .drmlgunit name) */
  proj[strlen(proj) - strlen("_drumlogue")] = 0;
  snprintf(path, sizeof path, "%s/config.mk", dir);
  f = fopen(path, "w");
  if (!f) {
    perror(path);
    return -1;
  }
  fprintf(f,
          "##############################################################################\n"
          "# Fluyt - the %s unit. GENERATED by tools/gen_rows.c: edit the generator.\n"
          "#\n"
          "# Felucca's %s (%s, engine %u of its ENGINES[]) and the panel of\n"
          "# %s/rows.def. The Makefile next to this file is the logue SDK's own, unmodified.\n"
          "##############################################################################\n"
          "\n"
          "PROJECT      := %s\n"
          "PROJECT_TYPE := synth\n"
          "\n"
          "SHIM_PATH    ?= $(PROJECT_ROOT)../shim\n"
          "FELUCCA_PATH ?= $(PROJECT_ROOT)../felucca\n"
          "\n"
          "CSRC  = $(SHIM_PATH)/fluyt_header.c\n"
          "CSRC += $(SHIM_PATH)/fluyt_engine.c\n"
          "CSRC += $(SHIM_PATH)/fluyt_unit.c\n"
          "\n"
          "UINCDIR  = $(SHIM_PATH)\n"
          "UINCDIR += $(SHIM_PATH)/gen\n"
          "UINCDIR += $(FELUCCA_PATH)/firmware/src\n"
          "\n"
          "ULIBDIR =\n"
          "\n"
          "# the drumlogue is an ordinary Linux: libc is real here (Felucca's own libc.c is not\n"
          "# compiled in, see shim/fluyt_fmt.h), and -lm for the float conversion\n"
          "ULIBS  = -lm\n"
          "ULIBS += -lc\n"
          "\n"
          "# Felucca's own flags (tools/build.py: -Os -ffunction-sections -fno-builtin), and the three\n"
          "# that make its fixed-point arithmetic portable to a second toolchain: the FM-1's char is\n"
          "# signed, its arithmetic wraps, and its DSP puns types (so no strict aliasing)\n"
          "USE_OPT  := -Os -ffunction-sections -fno-builtin\n"
          "USE_COPT := -fsigned-char -fwrapv -fno-strict-aliasing\n"
          "\n"
          "# Felucca's files are each one compilation unit of its own, with more in them than one\n"
          "# engine calls (tools/build.py compiles them the same way)\n"
          "USE_CWARN := -W -Wall -Wextra -Wno-unused-function -Wno-unused-variable\n"
          "\n"
          "UDEFS  = -DFLUYT_ENGINE_C=\\\"%s\\\"\n"
          "UDEFS += -DFLUYT_ENGINE_SYM=%s\n"
          "UDEFS += -DFLUYT_ENGINE_INDEX=%u\n"
          "UDEFS += -DFLUYT_ROWS=\\\"rows.def\\\"\n"
          "# the name has a space in it, and a -D goes through the shell unquoted: the backslash\n"
          "# keeps the compiler seeing one word, the escaped quotes make it a C string\n"
          "UDEFS += -DFLUYT_UNIT_NAME=\\\"FLUYT\\ %s\\\"\n"
          "UDEFS += -DFLUYT_UNIT_ID=0x%08XU\n"
          "UDEFS += -DFLUYT_UNIT_PRESETS=%u\n"
          "UDEFS += -DFLUYT_GATE_NOTE=%u\n"
          "# what this engine has of itself: it reads m->cutoff / m->shape (see UNITS[] in the\n"
          "# generator). tools/check_rows.c holds the panel to these two - a filter row or an LFO\n"
          "# filter amount where there is no filter is a slot spent on nothing.\n"
          "UDEFS += -DFLUYT_ENGINE_CUTOFF=%u\n"
          "UDEFS += -DFLUYT_ENGINE_SHAPE=%u\n",
          u->name, u->src, u->sym, (unsigned)felucca, d, proj, u->src, u->sym, (unsigned)felucca,
          u->name, 0x464C5900u | (unsigned)(which + 1u), (unsigned)npresets, (unsigned)u->gate,
          (unsigned)u->cutoff, (unsigned)u->shape);
  fclose(f);
  return 0;
}

int main(int argc, char **argv)
{
  const char *dir = argc > 1 ? argv[1] : ".";
  uint32_t which = argc > 2 ? (uint32_t)strtoul(argv[2], 0, 0) : 0;
  const struct unit *u;
  struct row knob;
  char path[512];
  FILE *f;
  uint32_t i, k;

  if (which >= NUNITS || which >= FLUYT_FAMILY_N) {
    fprintf(stderr, "unit %u: the family has %u\n", which, (unsigned)NUNITS);
    return 2;
  }
  if (UNITS[which].eng != FLUYT_FAMILY[which].eng) {     /* the two tables' numbering */
    fprintf(stderr, "unit %u: %s is not FLUYT_FAMILY's engine %u\n", which, UNITS[which].sym,
            (unsigned)FLUYT_FAMILY[which].index);
    return 2;
  }
  u = &UNITS[which];
  eng = u->eng;
  needs[W_ANY] = 1;                                 /* every engine renders an envelope, a voice and
                                                     * an LFO */
  needs[W_CUT] = u->cutoff;
  needs[W_SHP] = u->shape;

  for (i = 0; i < WALK_KNOB_AT; i++)
    add(&WALK[i]);
  for (k = 0; k < 4u; k++) {                        /* the engine's own HOME knobs */
    knob.name = name_of(eng->knob[k], k);
    knob.id = eng->knob[k];
    add(&knob);
  }
  for (i = WALK_KNOB_AT; i < NWALK; i++)
    add(&WALK[i]);

  snprintf(path, sizeof path, "%s/rows.def", dir);
  f = fopen(path, "w");
  if (!f) {
    perror(path);
    return 1;
  }
  fprintf(f,
          "/* Fluyt - the %s panel. GENERATED by tools/gen_rows.c from Felucca's own descriptors:\n"
          " * edit the generator, not this file. %u rows, %u pages of four. */\n",
          u->name, (unsigned)nrows, (unsigned)((nrows + 3u) / 4u));
  for (i = 0; i < nrows; i++) {
    const param_desc_t *d = row_desc(eng, rows[i].id);
    fprintf(f, "FLUYT_ROW(%s, \"%s\", %d, %d, %d, %s, 0)\n",
            rows[i].name, d->label, d->min, d->max, d->def, type_name(d));
  }
  fclose(f);

  if (write_config(dir, u, which, FLUYT_FAMILY[which].index, eng->npresets) != 0)
    return 1;
  printf("%s: %u rows, %u presets - %s (Felucca's %s, engine %u)\n", dir, (unsigned)nrows,
         (unsigned)eng->npresets, u->name, u->src, (unsigned)FLUYT_FAMILY[which].index);
  return 0;
}
