/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as logue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* The panels, written from Felucca's own descriptors.
 *
 * Every unit's rows.def is produced here rather than typed by hand, so no label, range or default
 * can drift from what the engine actually has: they are read out of params.c's TP[] and each
 * engine's edit[] - the very tables the FM-1's own display reads. tools/check_rows.c (built from
 * the same sources) reads a rows.def back and holds it to those tables, and its microKORG2 twin
 * (tools/check_rows_microkorg2.c) does the same for the second platform's panels.
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
 * The microKORG2 panel (WALK_MICROKORG2) is the same engine with less room and more instrument around it.
 * A microKORG2 oscillator unit declares 13 parameters and sits *inside* a voice that already has an
 * EG, a filter and an amp behind it, so the envelope group is not on this panel at all: the
 * instrument's own EG is the envelope, and the unit's ADSR is held open (shim/fluyt_microkorg2.c) so that
 * it is. The LFO's filter destination goes too, and for the same kind of reason - the instrument's
 * LFO can be patched to the unit's own cutoff row, so that one destination is the instrument's to
 * give, where nothing on the device reaches the engine's internal shape amount.
 *
 * Build (host, no container needed):
 *   cc -O1 -o build/gen_rows tools/gen_rows.c -Ishim/gen -Ifelucca/firmware/src -DFLUYT_ALL_ENGINES
 *   build/gen_rows fluyt_analog_drumlogue/rows.def 0
 *   build/gen_rows fluyt_analog_microkorg2/rows.def 0 microkorg2
 *
 * The second argument is the unit's number in shim/fluyt_engines.c's FLUYT_FAMILY[], which is the
 * engine and its index in Felucca's own ENGINES[]; build.sh runs this once per unit.
 * FLUYT_ALL_ENGINES compiles every engine of the family into this tool, the way Felucca's own
 * engines.c compiles them.
 *
 * The third argument is the platform - `drumlogue` (the default) or `microkorg2` - and it is the
 * only thing the two builds do not share: which walk is used, how many rows the device holds, the
 * name the device shows, and the config.mk template. Everything the rows are made of is Felucca's
 * either way. */

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

/* the drumlogue panel, in the order the device shows it; the engine's four knobs go in at the
 * walk's own knob point (TARGETS[]), because which parameters they are is the engine's own business:
 * here it is after the envelope, before the engine's parameters */
static const struct row WALK_DL[] = {
    W(P_ATK), W(P_DEC), W(P_SUS), W(P_REL),
    W(P_E0), W(P_E1), W(P_E2), W(P_E3), W(P_E4), W(P_E5), W(P_E6), W(P_E7),
    WC(P_ED_FLT),
    W(P_VOICE),
    W(P_LRATE), W(P_LWAVE),
    W(P_LD_PIT), WC(P_LD_FLT), WS(P_LD_SHP), W(P_LD_AMP),
};

/* the microKORG2 panel: this engine's own eight parameters (its four HOME knobs first, as on the
 * drumlogue panel, then the rest of P_E0..P_E7 in order), then the LFO's destinations. No envelope,
 * no voice mode, and no P_LD_FLT - see the top of this file. */
static const struct row WALK_MICROKORG2[] = {
    W(P_E0), W(P_E1), W(P_E2), W(P_E3), W(P_E4), W(P_E5), W(P_E6), W(P_E7),
    W(P_LRATE), W(P_LWAVE),
    W(P_LD_PIT), WS(P_LD_SHP), W(P_LD_AMP),
};

/* The run's own platform: which walk, and the two numbers that go with it */
static const struct row *walk;
static uint32_t nwalk, walk_knob_at, row_max;

/* The parameters a panel does not have at all, whatever the walk says and even where an engine's own
 * HOME knob is one of them: the microKORG2 panel's whole envelope group. The instrument's own EG is
 * the envelope there, and the unit's ADSR is held open instead (shim/fluyt_microkorg2.c), so a row that
 * moves it would be a knob doing what the instrument already does - and on that panel it would take
 * the slot the LFO's shape amount needs. ANALOG is where it shows: two of its four HOME knobs are
 * ATK and REL, which are the same drop as the four envelope rows, not a second rule. */
static const uint8_t SKIP_MICROKORG2[] = {P_ATK, P_DEC, P_SUS, P_REL, P_ED_FLT, P_ED_PIT, P_ED_SHP};
static const uint8_t *skip;
static uint32_t nskip;

/* the larger of the two panels: any walk's rows fit, and a walk longer than this is a bug here */
static struct row rows[sizeof WALK_DL / sizeof WALK_DL[0]];
static uint32_t nrows;
static const engine_t *eng;      /* the unit's engine (row_desc) */
static uint8_t needs[3];         /* W_ANY/W_CUT/W_SHP: what this engine has, from UNITS[] */
static char knob_name[4][12];    /* for a knob whose parameter the walk does not name (unlikely) */

/* a parameter's own name, as the walk has it */
static const char *name_of(uint32_t id, uint32_t slot)
{
  uint32_t i;
  for (i = 0; i < nwalk; i++)
    if (walk[i].id == id)
      return walk[i].name;
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

/* a parameter this platform's panel has no room for, whatever the engine's knobs say */
static int skipped(uint32_t id)
{
  uint32_t i;
  for (i = 0; i < nskip; i++)
    if (skip[i] == id)
      return 1;
  return 0;
}

/* Felucca's own descriptor for a parameter, without the engine's desc hook: a row's label is fixed,
 * where a descriptor's may depend on the mode the engine is in */
static const param_desc_t *row_desc(const engine_t *e, uint32_t id);

/* one row, unless the walk has already taken that parameter, this engine has nothing behind it (a
 * row whose condition the engine does not meet), the parameter is one of Felucca's inert ones (a
 * label of "-" and no range at all: PHASE leaves P_E7 so), this platform's panel does not have that
 * parameter (skipped()), or the panel is full */
static void add(const struct row *r)
{
  if (nrows >= row_max || seen(r->id) || skipped(r->id) || !needs[r->needs] ||
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
 *
 * The last two are the same kind of fact, about the envelope: whether the track's ADSR is this
 * engine's amplitude at all. FM6 says no (`eng_fm6.c:367` `.ownenv = 1`, and its own comment: "the
 * operator envelopes are the voice's amplitude ... the track's ADSR, ENV DEST and the matrix's ENV
 * do nothing here") and DRUM says no (`eng_drum.c:301` `.oneshot = 1` with `drum_amp`, whose
 * parameter is named `adsr` and never read: "the hit, not the ADSR, which still runs, held at
 * full"). Both hold the ADSR at full, so no ATK, DEC, SUS or REL render can move either unit, and
 * on FM6 there is no envelope for the ENV DEST rows to multiply. The rows are still on those two
 * panels (Felucca's own editor shows the envelope group for every engine, and this generator's walk
 * is Felucca's panel), so the flags are what tools/reginecheck.sh reads to tell a row of that kind
 * from a row that has genuinely come unwired.
 *
 * The last one is a row of the engine's own that can mask the envelope's filter amount: LOFI's own
 * TONE. `eng_lofi.c:128` opens its lowpass with `lpk = 3000 + (p[P_E7] << 8) + (m->cutoff > 0 ?
 * m->cutoff : 0)`, and `eng_lofi.c:150` clamps that at 32767 - which TONE's own default of 127
 * already reaches (3000 + 32512), so the filter is wide open before the envelope adds anything and
 * no amount can open it further. That is why FLUYT LOFI's FLT row moves the sound only with TONE
 * below about 116, and why tools/reginecheck.sh closes that row for the pair it renders rather than
 * reporting the envelope's row as unwired. Empty for an engine whose filter has no such row.
 *
 * The engines themselves are FLUYT_FAMILY[]'s (shim/fluyt_engines.c), in the same order - checked
 * below, so the numbering the units are built with cannot come apart from the numbering they render
 * with. */
static const struct unit {
  const char *name;               /* the drumlogue's name for it (FLUYT ANALOG) */
  const char *microkorg2;                /* the microKORG2's: F-ANALOG, whose name field is 8 characters
                                   * (UNIT_NAME_LEN) and so cannot hold "FLUYT ANALOG" */
  const char *src;
  const char *sym;
  const engine_t *eng;
  uint8_t gate;
  uint8_t cutoff, shape;
  uint8_t ownenv, oneshot;
  const char *tone;               /* the engine's own filter-opening row, or "" */
} UNITS[] = {
    {"ANALOG", "F-ANALOG", "eng_analog.c", "ENG_ANALOG", &ENG_ANALOG, 60, 1, 1, 0, 0, ""},
    {"TRIO", "F-TRIO", "eng_trio.c", "ENG_TRIO", &ENG_TRIO, 60, 1, 1, 0, 0, ""},
    {"PHASE", "F-PHASE", "eng_phase.c", "ENG_PHASE", &ENG_PHASE, 60, 1, 1, 0, 0, ""},
    {"LOFI", "F-LOFI", "eng_lofi.c", "ENG_LOFI", &ENG_LOFI, 60, 1, 0, 0, 0, "P_E7"},
    {"WHEEL", "F-WHEEL", "eng_wheel.c", "ENG_WHEEL", &ENG_WHEEL, 60, 0, 0, 0, 0, ""},
    {"NOISE", "F-NOISE", "eng_noise.c", "ENG_NOISE", &ENG_NOISE, 60, 1, 1, 0, 0, ""},
    {"FORMANT", "F-FORMNT", "eng_formant.c", "ENG_FORMANT", &ENG_FORMANT, 60, 1, 1, 0, 0, ""},
    {"DRUM", "F-DRUM", "eng_drum.c", "ENG_DRUM", &ENG_DRUM, 36, 0, 0, 0, 1, ""},
    {"FM6", "F-FM6", "eng_fm6.c", "ENG_FM6", &ENG_FM6, 60, 1, 1, 1, 0, ""},
};
#define NUNITS (sizeof(UNITS) / sizeof(UNITS[0]))

/* the two platforms this generator writes panels for. The panel is the same computation either way
 * - the same engine read through the same descriptors - so what a platform fixes is the walk it
 * follows, how much room the device has for the rows, and the emitter: what the unit's directory and
 * its project are called, the module it declares itself to be, and which two of the shim's files are
 * that module's own (the header, and the ABI's entry points). */
static const struct target {
  const char *arg;                /* what the third argument says */
  const char *suffix;             /* the unit directory's tail */
  const char *type;               /* PROJECT_TYPE: the module this unit is */
  const char *header_src;         /* the unit header (the panel, declared to the device) */
  const char *unit_src;           /* the ABI's entry points */
  const char *device;             /* the machine, as these notes call it */
  const char *check;              /* the panel check that reads this unit's rows.def */
  const char *render_check;       /* and the check that renders it */
  const char *defs;               /* UDEFS the second platform needs and the first does not */
  const char *flag_why;           /* config.mk's note on FLUYT_ENGINE_CUTOFF/-SHAPE: who reads them
                                   * on this platform, which is not the same answer twice */
  const char *env_why;            /* and on FLUYT_ENGINE_OWNENV/-ONESHOT */
  const char *tone_why;           /* and on FLUYT_ENGINE_TONE */
  const struct row *walk;         /* the panel, in the order this device shows it */
  uint32_t nwalk;                 /* that walk's length */
  uint32_t knob_at;               /* where in it the engine's four HOME knobs go */
  uint32_t row_max;               /* how many rows the device holds (UNIT_MAX_PARAM_COUNT) */
  const uint8_t *skip;            /* parameters this panel does not have at all, or NULL */
  uint32_t nskip;
  uint8_t spaced_name;            /* whether the unit's name has a space in it */
} TARGETS[] = {
    {"drumlogue", "drumlogue", "synth", "fluyt_header.c", "fluyt_unit.c", "drumlogue",
     "tools/check_rows.c", "tools/reginecheck.sh",
     "",
     "tools/check_rows.c holds the panel to these two - a filter row or an LFO\n"
     "# filter amount where there is no filter is a slot spent on nothing.",
     "The rows stay on the\n"
     "# panel, so tools/reginecheck.sh is what reads these two - a render cannot move them.",
     "Empty where the engine has no such row (see UNITS[] in the generator).",
     WALK_DL, (uint32_t)(sizeof WALK_DL / sizeof WALK_DL[0]), 4u, 24u, NULL, 0u, 1},
    {"microkorg2", "microkorg2", "osc", "fluyt_microkorg2_header.c", "fluyt_microkorg2_unit.c",
     "microKORG2", "tools/check_rows_microkorg2.c", "tools/microkorg2sim.c",
     "# this build is the microKORG2's: the bridge renders one voice per lane (shim/fluyt_microkorg2.c),\n"
     "# the envelope is held open rather than shown, and the panel is 13 rows (rows.def)\n"
     "UDEFS += -DFLUYT_TARGET_MICROKORG2\n",
     "The shape decides a row here and tools/check_rows_microkorg2.c holds\n"
     "# it to that (P_LD_SHP, the walk's one conditional row). The cutoff decides none: this\n"
     "# panel's walk has no filter row at all - no P_LD_FLT, and none of this engine's own rows\n"
     "# reads m->cutoff - so the flag is the same fact about the engine, written for the panel\n"
     "# that does have one.",
     "Neither is a row here:\n"
     "# the whole envelope group is off this panel and the bridge holds Felucca's ADSR open in its\n"
     "# place (shim/fluyt_microkorg2.c). Both flags are still read: tools/check_rows_microkorg2.c\n"
     "# holds them to the engine descriptor the unit renders with, and the drumlogue panel's own\n"
     "# render check reads the same fact about the same engine.",
     "This panel has no envelope filter row to mask, so nothing\n"
     "# here reads it: the same fact about the engine, and empty where there is no such row (see\n"
     "# UNITS[] in the generator).",
     WALK_MICROKORG2, (uint32_t)(sizeof WALK_MICROKORG2 / sizeof WALK_MICROKORG2[0]), 0u, 13u,
     SKIP_MICROKORG2, (uint32_t)(sizeof SKIP_MICROKORG2 / sizeof SKIP_MICROKORG2[0]), 0},
};
#define NTARGETS (sizeof(TARGETS) / sizeof(TARGETS[0]))

/* the engine's name, lowercased: ANALOG -> analog */
static void lower_name(char *out, size_t n, const char *name)
{
  size_t i = 0;
  for (; *name && i + 1u < n; name++)
    out[i++] = (char)(*name >= 'A' && *name <= 'Z' ? *name - 'A' + 'a' : *name);
  out[i] = 0;
}

/* the unit directory a name gets: fluyt_analog_drumlogue, fluyt_analog_microkorg2 */
static void unit_dir(char *out, size_t n, const char *name, const char *suffix)
{
  char low[16];                    /* "formant" is the longest of the nine */
  lower_name(low, sizeof low, name);
  snprintf(out, n, "fluyt_%s_%s", low, suffix);
}

/* the name the SDK's Makefile gives what it builds: fluyt_analog, which the two platforms then
 * extend with their own extension (.drmlgunit, .mk2unit), so the two files never collide in bin/ */
static void proj_name(char *out, size_t n, const char *name)
{
  char low[16];
  lower_name(low, sizeof low, name);
  snprintf(out, n, "fluyt_%s", low);
}

/* the unit's config.mk: what the Makefile needs to compile this one engine, written from the same
 * facts rows.def comes from. `which` is the unit's place in the Fluyt family, `felucca` is that
 * engine's own number in Felucca's ENGINES[] - the two are never the same number and both are
 * needed: the first names the unit, the second is what the engine's code is told it is (a unit
 * whose engine is DRUM has to know that, and Felucca's presets are numbered by it).
 *
 * One template for both platforms, because almost all of it is the same either way: what differs is
 * the module the unit declares itself (a `synth` or an `osc`), the two shim files that module's ABI
 * lives in, the name the device shows, and the target define the bridge reads. */
static int write_config(const char *dir, const struct unit *u, uint32_t which, uint32_t felucca,
                        uint32_t npresets, const struct target *t)
{
  char path[512], proj[64], d[64];
  FILE *f;
  unit_dir(d, sizeof d, u->name, t->suffix);
  proj_name(proj, sizeof proj, u->name);                 /* fluyt_analog: what it builds */
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
          "PROJECT_TYPE := %s\n"
          "\n"
          "SHIM_PATH    ?= $(PROJECT_ROOT)../shim\n"
          "FELUCCA_PATH ?= $(PROJECT_ROOT)../felucca\n"
          "\n",
          u->name, u->src, u->sym, (unsigned)felucca, d, proj, t->type);
  fprintf(f,
          "CSRC  = $(SHIM_PATH)/%s\n"
          "CSRC += $(SHIM_PATH)/fluyt_engine.c\n"
          "CSRC += $(SHIM_PATH)/%s\n"
          "\n"
          "UINCDIR  = $(SHIM_PATH)\n"
          "UINCDIR += $(SHIM_PATH)/gen\n"
          "UINCDIR += $(FELUCCA_PATH)/firmware/src\n"
          "\n"
          "ULIBDIR =\n"
          "\n"
          "# the %s is an ordinary Linux: libc is real here (Felucca's own libc.c is not\n"
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
          "%s",
          t->header_src, t->unit_src, t->device, u->src, u->sym, (unsigned)felucca, t->defs);
  if (t->spaced_name)
    fprintf(f,
            "# the name has a space in it, and a -D goes through the shell unquoted: the backslash\n"
            "# keeps the compiler seeing one word, the escaped quotes make it a C string\n"
            "UDEFS += -DFLUYT_UNIT_NAME=\\\"FLUYT\\ %s\\\"\n",
            u->name);
  else
    fprintf(f,
            "# the name the instrument shows: 8 characters at most (UNIT_NAME_LEN), and it is the\n"
            "# unit's own - see UNITS[] in the generator\n"
            "UDEFS += -DFLUYT_UNIT_NAME=\\\"%s\\\"\n",
            u->microkorg2);
  fprintf(f,
          "UDEFS += -DFLUYT_UNIT_ID=0x%08XU\n"
          "UDEFS += -DFLUYT_UNIT_PRESETS=%u\n"
          "UDEFS += -DFLUYT_GATE_NOTE=%u\n",
          (unsigned)(0x464C5900u | (which + 1u)), (unsigned)npresets, (unsigned)u->gate);
  /* the three notes below are the target's own, because the same flag is read by a different check
   * on each platform - or, for a row the panel does not have, by none of them */
  fprintf(f,
          "# what this engine has of itself: it reads m->cutoff / m->shape (see UNITS[] in the\n"
          "# generator). %s\n"
          "UDEFS += -DFLUYT_ENGINE_CUTOFF=%u\n"
          "UDEFS += -DFLUYT_ENGINE_SHAPE=%u\n",
          t->flag_why, (unsigned)u->cutoff, (unsigned)u->shape);
  fprintf(f,
          "# and whether the track's ADSR is this engine's amplitude: FM6's operator envelopes are\n"
          "# (ownenv), DRUM's hit is (oneshot), and both hold the ADSR at full. %s\n"
          "UDEFS += -DFLUYT_ENGINE_OWNENV=%u\n"
          "UDEFS += -DFLUYT_ENGINE_ONESHOT=%u\n",
          t->env_why, (unsigned)u->ownenv, (unsigned)u->oneshot);
  fprintf(f,
          "# and the row of this engine's own that can mask the envelope's filter amount: LOFI's TONE\n"
          "# saturates its own lowpass at its default, so the FLT row only opens anything below it.\n"
          "# %s\n"
          "UDEFS += -DFLUYT_ENGINE_TONE=%s\n",
          t->tone_why, u->tone);
  fclose(f);
  return 0;
}

int main(int argc, char **argv)
{
  const char *dir = argc > 1 ? argv[1] : ".";
  uint32_t which = argc > 2 ? (uint32_t)strtoul(argv[2], 0, 0) : 0;
  const char *plat = argc > 3 ? argv[3] : "drumlogue";
  const struct target *t = NULL;
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
  for (i = 0; i < NTARGETS; i++)
    if (!strcmp(plat, TARGETS[i].arg))
      t = &TARGETS[i];
  if (!t) {
    fprintf(stderr, "no such platform: %s\n", plat);
    return 2;
  }
  walk = t->walk;
  nwalk = t->nwalk;
  walk_knob_at = t->knob_at;
  row_max = t->row_max;
  skip = t->skip;
  nskip = t->nskip;

  u = &UNITS[which];
  eng = u->eng;
  needs[W_ANY] = 1;                                 /* every engine renders an envelope, a voice and
                                                     * an LFO */
  needs[W_CUT] = u->cutoff;
  needs[W_SHP] = u->shape;

  for (i = 0; i < walk_knob_at; i++)
    add(&walk[i]);
  for (k = 0; k < 4u; k++) {                        /* the engine's own HOME knobs */
    knob.name = name_of(eng->knob[k], k);
    knob.id = eng->knob[k];
    knob.needs = W_ANY;                             /* it is the engine's own parameter, whatever
                                                     * the row it lands on is called */
    add(&knob);
  }
  for (i = walk_knob_at; i < nwalk; i++)
    add(&walk[i]);

  snprintf(path, sizeof path, "%s/rows.def", dir);
  f = fopen(path, "w");
  if (!f) {
    perror(path);
    return 1;
  }
  /* `panel` says which of the two this is, and is empty on the drumlogue: its panels were written
   * before there was a second platform, and they read as they always did */
  fprintf(f,
          "/* Fluyt - the %s %spanel. GENERATED by tools/gen_rows.c from Felucca's own descriptors:\n"
          " * edit the generator, not this file. %u rows, %u pages of four. */\n",
          u->name, t->spaced_name ? "" : "microKORG2 ", (unsigned)nrows,
          (unsigned)((nrows + 3u) / 4u));
  for (i = 0; i < nrows; i++) {
    const param_desc_t *d = row_desc(eng, rows[i].id);
    fprintf(f, "FLUYT_ROW(%s, \"%s\", %d, %d, %d, %s, 0)\n",
            rows[i].name, d->label, d->min, d->max, d->def, type_name(d));
  }
  fclose(f);

  if (write_config(dir, u, which, FLUYT_FAMILY[which].index, eng->npresets, t) != 0)
    return 1;
  printf("%s: %u rows, %u presets - %s (Felucca's %s, engine %u)\n", dir, (unsigned)nrows,
         (unsigned)eng->npresets, u->name, u->src, (unsigned)FLUYT_FAMILY[which].index);
  return 0;
}
