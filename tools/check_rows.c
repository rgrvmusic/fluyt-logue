/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as logue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* Holds a unit's panel to Felucca's own parameter descriptors - the panel itself and the declaration
 * the device reads.
 *
 * The panel is generated (tools/gen_rows.c) out of Felucca's TP[] and each engine's edit[] table,
 * so in principle it cannot disagree with them. This is what says so, and it says it twice over:
 *
 *   - rows.def, every row's label, range and default, against the descriptor the running engine
 *     reports, plus the checks a generator cannot make about itself (no parameter twice, nothing
 *     out of the parameter enum, the envelope first);
 *   - the unit header the device actually reads (shim/fluyt_header.c, compiled in here), against
 *     the same descriptors: the row starts at zero, spans Felucca's width, starts where Felucca
 *     starts it, and carries Felucca's label, type and decimals. A row declared to the drumlogue
 *     with a negative minimum is one the runtime cannot hold - it comes back wrapped, which reads
 *     as the row's maximum - so a row below zero here is a panel whose lower half shows its top
 *     ("+98%", "+24st"): the bug this file is here to catch.
 *
 *   - the panel against this engine's own parts (FLUYT_ENGINE_CUTOFF / FLUYT_ENGINE_SHAPE, which
 *     config.mk carries and which say what the engine's code reads): no filter row and no LFO filter
 *     amount on a unit whose engine has no filter, no LFO shape amount where it is not shaped, and
 *     none of the rows Fluyt leaves off on purpose - the FM-1's own track (level, pan, mute, glide)
 *     and the envelope's non-filter destinations.
 *
 * It also *moves* every row: each one is set from the control thread the way the device sets it, at
 * its lowest, its default and its highest value, and the parameter Felucca's engine holds is checked
 * against the descriptor afterwards, as is the text the device is shown. And it prints the panel the
 * way the device shows it: each row, its label, and Felucca's own formatting of the row at its
 * lowest, its default and its highest value. Reading that column is how a wrong unit or a wrong
 * scale is caught before the device is involved.
 *
 * Build and run (tools/hostcheck.sh does this for all nine):
 *   cc -O1 -Wall -o build/check_rows tools/check_rows.c \
 *      -I. -Ishim -Ishim/gen -Ifelucca/firmware/src \
 *      -DFLUYT_ENGINE_C='"eng_analog.c"' -DFLUYT_ENGINE_SYM=ENG_ANALOG \
 *      -DFLUYT_ENGINE_INDEX=0 -DFLUYT_ROWS='"fluyt_analog_drumlogue/rows.def"' \
 *      -DFLUYT_UNIT_NAME='"FLUYT ANALOG"' -DFLUYT_UNIT_ID=1 -DFLUYT_UNIT_PRESETS=12
 *      -DFLUYT_ENGINE_CUTOFF=1 -DFLUYT_ENGINE_SHAPE=1
 *   build/check_rows
 * (those are the unit's own UDEFS: hostcheck.sh reads them out of its config.mk, so there is only
 * ever one copy of them.) */

#ifndef FLUYT_ENGINE_CUTOFF
#error "FLUYT_ENGINE_CUTOFF must say whether this engine reads m->cutoff (see a unit's config.mk)"
#endif
#ifndef FLUYT_ENGINE_SHAPE
#error "FLUYT_ENGINE_SHAPE must say whether this engine reads m->shape (see a unit's config.mk)"
#endif

#include <stdio.h>
#include <string.h>

#include "unit.h"                /* UNIT_MAX_PARAM_COUNT, the device's own: -I $(LOGUE_SDK ...)/common */

/* the unit's own translation unit, exactly as the unit builds it: one engine, this rows.def */
#include "../shim/fluyt_engine.c"

/* and the header itself, so what is checked is what the device is told rather than a second reading
 * of rows.def (this is also where FLUYT_UNIT_TYPE comes from) */
#include "../shim/fluyt_header.c"

/* rows.def once more, this time as data: what the panel claims, to compare with what Felucca says */
struct checkrow {
  const char *label;
  int32_t min, max, def;
  uint8_t type, frac;
};
static const struct checkrow ROWS[] = {
#define FLUYT_ROW(p, l, mn, mx, df, t, f) {l, mn, mx, df, FLUYT_UNIT_TYPE(t), f},
#include FLUYT_ROWS
#undef FLUYT_ROW
};
#define NROWS (sizeof(ROWS) / sizeof(ROWS[0]))

/* The rows that are only a row where the engine has the part behind them, and the ones Fluyt leaves
 * off the panel altogether.
 *
 * 'C' and 'S' are the engine's own code: the row modulates a cutoff (voice.c's m.cutoff) or a shape
 * (m.shape), and an engine that never reads either has nothing behind the row, so a panel that shows
 * it is a slot spent on a knob that moves nothing. That is what config.mk's FLUYT_ENGINE_CUTOFF /
 * FLUYT_ENGINE_SHAPE say (tools/gen_rows.c's UNITS[] read them out of the engine's source), and a
 * row expected here against the flag is the panel and the engine having come apart.
 *
 * 0 is the FM-1's own track, which a drumlogue unit does not put on its panel: level and pan are the
 * part the unit renders into (the device's own volume and pan), mute is the sequencer's, and glide,
 * the envelope's non-filter destinations and the LFO's phase and fade lost their slot to the LFO's
 * four destinations - which is what the panel says it is, so a row from this list turning up on a
 * panel is the panel drifting. */
struct flagrow {
  const char *name;                /* the parameter, as the walk in tools/gen_rows.c writes it */
  uint8_t id;
  uint8_t flag;                    /* 0: never on a Fluyt panel, 'C': m->cutoff, 'S': m->shape */
};
static const struct flagrow FLAGROW[] = {
    {"P_ED_FLT", P_ED_FLT, 'C'}, {"P_LD_FLT", P_LD_FLT, 'C'}, {"P_LD_SHP", P_LD_SHP, 'S'},
    {"P_LEVEL", P_LEVEL, 0}, {"P_PAN", P_PAN, 0}, {"P_MUTE", P_MUTE, 0}, {"P_GLIDE", P_GLIDE, 0},
    {"P_ED_PIT", P_ED_PIT, 0}, {"P_ED_SHP", P_ED_SHP, 0}, {"P_LPHASE", P_LPHASE, 0},
    {"P_LFADE", P_LFADE, 0},
};
#define NFLAGROW (sizeof(FLAGROW) / sizeof(FLAGROW[0]))

/* Felucca's own formatting of a value, for holding the unit's answer to it */
static void felucca_str(const param_desc_t *d, int32_t value, char *out, size_t n)
{
  const char *unit = "";
  char val[8];
  fmt_named = 0;
  param_format(d, value, val, &unit);
  snprintf(out, n, "%s%s", val, unit);
}

/* the row's value as the device holds it: its offset into Felucca's range */
static int32_t row_offset(const param_desc_t *d, int32_t value)
{
  return value - d->min;
}

static float sink[CTL * 2];      /* the render that drains the bridge's queue */

int main(void)
{
  const engine_t *e;
  uint32_t i, j, bad = 0;

  if (!fluyt_init(48000.0f)) {
    printf("FAIL: the bridge refused to start\n");
    return 1;
  }
  e = ENGINES[eng_idx(trk[0].eng_req)];

  printf("%s: Felucca's %s, engine %u, %u rows, %u presets\n", FLUYT_UNIT_NAME, e->name,
         (unsigned)FLUYT_ENGINE_INDEX, (unsigned)fluyt_num_params(), (unsigned)e->npresets);

  /* what the unit's header declares has to be what it answers with */
  if (NROWS != fluyt_num_params()) {
    printf("FAIL: rows.def has %u rows, the bridge says %u\n", (unsigned)NROWS,
           (unsigned)fluyt_num_params());
    bad++;
  }
  if ((uint32_t)FLUYT_UNIT_PRESETS != e->npresets) {
    printf("FAIL: config.mk says %u presets, the engine has %u\n", (unsigned)FLUYT_UNIT_PRESETS,
           (unsigned)e->npresets);
    bad++;
  }
  if (fluyt_num_params() > UNIT_MAX_PARAM_COUNT) {
    printf("FAIL: %u rows, the device holds %u\n", (unsigned)fluyt_num_params(),
           (unsigned)UNIT_MAX_PARAM_COUNT);
    bad++;
  }
  if (unit_header.num_params != NROWS || unit_header.num_presets != FLUYT_UNIT_PRESETS) {
    printf("FAIL: the header declares %u rows and %u presets, the unit has %u and %u\n",
           (unsigned)unit_header.num_params, (unsigned)unit_header.num_presets, (unsigned)NROWS,
           (unsigned)FLUYT_UNIT_PRESETS);
    bad++;
  }
  if (strcmp(unit_header.name, FLUYT_UNIT_NAME) || unit_header.unit_id != FLUYT_UNIT_ID) {
    printf("FAIL: the header is \"%s\" %#x, the unit is \"%s\" %#x\n", unit_header.name,
           (unsigned)unit_header.unit_id, FLUYT_UNIT_NAME, (unsigned)FLUYT_UNIT_ID);
    bad++;
  }
  if (unit_header.header_size != sizeof(unit_header_t)) {
    printf("FAIL: header_size is %u, the struct is %u bytes\n", (unsigned)unit_header.header_size,
           (unsigned)sizeof(unit_header_t));
    bad++;
  }

  /* the envelope is the first page, whatever engine this is: a drumlogue unit is played by hand as
   * well as by the sequencer, and a note that does not decay where it is told to is the one thing
   * no preset can excuse */
  if (fluyt_row_param[0] != P_ATK || fluyt_row_param[1] != P_DEC || fluyt_row_param[2] != P_SUS ||
      fluyt_row_param[3] != P_REL) {
    printf("FAIL: rows 1-4 are not ATK, DEC, SUS, REL\n");
    bad++;
  }

  /* the panel against the engine's own parts, and against what Fluyt leaves off (FLAGROW) */
  for (i = 0; i < NFLAGROW; i++) {
    const char *why;
    uint32_t on = 0;
    int want;
    for (j = 0; j < NROWS; j++)
      if (fluyt_row_param[j] == FLAGROW[i].id)
        on = 1;
    if (FLAGROW[i].flag == 'C') {
      want = FLUYT_ENGINE_CUTOFF ? 1 : 0;
      why = want ? "this engine renders a filter, so the row has to be here"
                 : "this engine renders no filter (FLUYT_ENGINE_CUTOFF is 0)";
    } else if (FLAGROW[i].flag == 'S') {
      want = FLUYT_ENGINE_SHAPE ? 1 : 0;
      why = want ? "this engine is shaped, so the row has to be here"
                 : "this engine is not shaped (FLUYT_ENGINE_SHAPE is 0)";
    } else {
      want = 0;
      why = "a Fluyt panel is this engine's sound, and that row is not part of it";
    }
    if (on != (uint32_t)want) {
      printf("FAIL: %s (%s) is %s the panel, but %s\n", FLAGROW[i].name,
             track_desc(&trk[0], FLAGROW[i].id)->label, on ? "on" : "not on", why);
      bad++;
    }
  }

  for (i = 0; i < NROWS; i++) {
    const param_desc_t *d = fluyt_row_desc(i);
    const unit_param_t *u = &unit_header.params[i];
    uint32_t id = fluyt_row_param[i];
    int32_t span, off;
    char lo[24], df[24], hi[24];

    if (id >= P_COUNT) {                          /* id is a field of track_t::p[P_COUNT] */
      printf("FAIL: row %u is parameter %u, past the end of the enum\n", (unsigned)i, (unsigned)id);
      bad++;
      continue;
    }
    for (j = 0; j < i; j++)                       /* a parameter is shown once, or a knob that is
                                                   * also a row would fight itself */
      if (fluyt_row_param[j] == id) {
        printf("FAIL: row %u and row %u are both parameter %s\n", (unsigned)j, (unsigned)i,
               d->label);
        bad++;
      }

    if (strcmp(d->label, ROWS[i].label)) {
      printf("FAIL: row %u is \"%s\", rows.def says \"%s\"\n", (unsigned)i, d->label,
             ROWS[i].label);
      bad++;
    }
    if (d->min != ROWS[i].min || d->max != ROWS[i].max || d->def != ROWS[i].def) {
      printf("FAIL: row %u (%s) is %d..%d def %d, rows.def says %d..%d def %d\n", (unsigned)i,
             d->label, d->min, d->max, d->def, (int)ROWS[i].min, (int)ROWS[i].max,
             (int)ROWS[i].def);
      bad++;
    }
    if (d->min >= d->max) {                       /* Felucca's inert rows (PHASE's P_E7) are not
                                                   * shown, so one here is a row that moves nothing */
      printf("FAIL: row %u (%s) has no range: %d..%d\n", (unsigned)i, d->label, d->min, d->max);
      bad++;
    }

    /* the declaration the device holds: the same row, counted from zero */
    span = d->max - d->min;
    if (u->min != 0 || u->max != span || u->init != d->def - d->min || u->center != -d->min) {
      printf("FAIL: row %u (%s) is declared %d..%d center %d def %d, Felucca's range counted from "
             "zero is 0..%d center %d def %d\n", (unsigned)i, d->label, u->min, u->max, u->center,
             u->init, (int)span, (int)-d->min, (int)(d->def - d->min));
      bad++;
    }
    if (u->type != ROWS[i].type || u->frac != ROWS[i].frac || strcmp(u->name, d->label)) {
      printf("FAIL: row %u (%s) is declared type %u frac %u \"%s\", rows.def says type %u frac %u "
             "\"%s\"\n", (unsigned)i, d->label, (unsigned)u->type, (unsigned)u->frac, u->name,
             (unsigned)ROWS[i].type, (unsigned)ROWS[i].frac, d->label);
      bad++;
    }
    if (u->min < 0 || u->init < 0 || u->init > u->max) {
      printf("FAIL: row %u (%s) is declared def %d in %d..%d: a row the device holds is not "
             "negative\n", (unsigned)i, d->label, u->init, u->min, u->max);
      bad++;
    }

    /* and it moves: the device's own bottom, default and top, through the control thread and the
     * render that drains it, with the engine's parameter and the device's text checked after each */
    for (j = 0; j < 3; j++) {
      int32_t want = j == 0 ? d->min : j == 1 ? d->def : d->max;
      char expect[24], got[24];
      off = row_offset(d, want);
      fluyt_set_param((uint8_t)i, off);
      felucca_str(d, want, expect, sizeof expect);
      snprintf(got, sizeof got, "%s", fluyt_param_str((uint8_t)i, off));
      if (fluyt_get_param((uint8_t)i) != off) {
        printf("FAIL: row %u (%s) at %d answers %d\n", (unsigned)i, d->label, (int)off,
               (int)fluyt_get_param((uint8_t)i));
        bad++;
      }
      if (strcmp(got, expect)) {
        printf("FAIL: row %u (%s) at its %s (%d) shows \"%s\", Felucca says \"%s\"\n",
               (unsigned)i, d->label, j == 0 ? "bottom" : j == 1 ? "default" : "top", (int)want,
               got, expect);
        bad++;
      }
      fluyt_render(NULL, sink, CTL);              /* the control thread's row reaches the engine */
      if (trk[0].p[id] != want) {
        printf("FAIL: row %u (%s) at its %s shows %d but the parameter is %d\n", (unsigned)i,
               d->label, j == 0 ? "bottom" : j == 1 ? "default" : "top", (int)want,
               (int)trk[0].p[id]);
        bad++;
      }
    }
    fluyt_set_param((uint8_t)i, row_offset(d, d->def));   /* back where the panel starts it */
    fluyt_render(NULL, sink, CTL);

    /* the same three values as the device will show them */
    snprintf(lo, sizeof lo, "%s", fluyt_param_str((uint8_t)i, 0));
    snprintf(df, sizeof df, "%s", fluyt_param_str((uint8_t)i, row_offset(d, d->def)));
    snprintf(hi, sizeof hi, "%s", fluyt_param_str((uint8_t)i, (int32_t)span));
    printf("  %2u %-5s %4d..%-4d  %8s %8s %8s\n", (unsigned)(i + 1), d->label, d->min, d->max, lo,
           df, hi);
  }

  printf("%s: %s\n", FLUYT_UNIT_NAME, bad ? "FAILED" : "ok");
  return bad ? 1 : 0;
}
