/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as logue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* Holds a microKORG2 unit's panel to Felucca's own parameter descriptors - the panel itself and the
 * declaration the device reads. The twin of tools/check_rows.c, and it checks the same things; what
 * follows is what is different on this platform.
 *
 * The panel is generated (tools/gen_rows.c) out of Felucca's TP[] and each engine's edit[] table, so
 * in principle it cannot disagree with them. This is what says so, for the microKORG2's walk:
 *
 *   - rows.def, every row's label, range and default, against the descriptor the running engine
 *     reports, plus the checks a generator cannot make about itself (no parameter twice, nothing out
 *     of the parameter enum, no envelope row, the engine's four HOME knobs first);
 *   - the unit header the device actually reads (shim/fluyt_microkorg2_header.c, compiled in here),
 *     against the same descriptors: the row starts at zero, spans Felucca's width, starts where
 *     Felucca starts it, and carries Felucca's label, type and decimals. A row declared with a
 *     negative minimum is one the runtime cannot hold - it comes back wrapped, which reads as the
 *     row's maximum - so a row below zero here is a panel whose lower half shows its top ("+98%",
 *     "+24st"): the same bug this file is here to catch, and the reason a row whose text the unit
 *     formats itself is declared from zero up on both platforms;
 *   - and the header's own three facts about the module: it is an osc module of this platform
 *     (k_unit_target_microkorg2_osc, which is what unit_init compares the runtime's descriptor
 *     against), its api is the one the SDK defines, and the slots past the panel are the SDK's blank
 *     descriptor rather than a fourteenth row the device would show as nothing.
 *
 * The envelope is not on this panel, and that is a rule about the panel *and* about the render: the
 * instrument's own EG is the envelope there, so this file checks both halves - that no row of the
 * envelope group is on the panel (OFFROW), and that the bridge puts those parameters back where it
 * holds them (open) at the head of every block, whatever a preset or a row has just done to them.
 * That second half is why the check loads every preset of the engine: a preset carries its own
 * envelope, and on this platform a preset may change what the panel can show and nothing else.
 *
 * The three flags config.mk carries are read here too, because they are the generator's claims about
 * the engine's own code: FLUYT_ENGINE_SHAPE against the panel (the LFO's shape amount is the one row
 * of this walk that is conditional), and FLUYT_ENGINE_OWNENV / FLUYT_ENGINE_ONESHOT against the
 * engine descriptor the unit renders with, which is where the claim comes from in the first place.
 * FLUYT_ENGINE_CUTOFF is not read here, and cannot be: this panel's walk has no filter row for any
 * engine (see tools/gen_rows.c), so nothing on this platform holds a row to it.
 *
 * It also *moves* every row, as the drumlogue's check does: each one is set from the control thread
 * the way the device sets it, at its lowest, its default and its highest value, with the parameter
 * Felucca's engine holds checked against the descriptor afterwards, as is the text the device is
 * shown. And it prints the panel the way the device shows it.
 *
 * shim/fluyt_microkorg2_unit.c is not compiled here: it includes unit_osc.h, whose SIMD helpers are
 * the device's own (NEON), so a host compiler can have neither. The render it calls is what
 * tools/microkorg2sim.c holds to the drumlogue's, and what a block is - including a block with no
 * voice in it - is that same call.
 *
 * Build and run (tools/hostcheck-microkorg2.sh does this for all nine):
 *   cc -O1 -Wall -o build/check_rows_microkorg2 tools/check_rows_microkorg2.c \
 *      -I. -Ifluyt_analog_microkorg2 -Ishim -Ishim/gen -Ifelucca/firmware/src \
 *      -I $(LOGUE_SDK_PATH)/platform/microkorg2/common -I $(LOGUE_SDK_PATH)/platform/common \
 *      -DFLUYT_ENGINE_C='"eng_analog.c"' -DFLUYT_ENGINE_SYM=ENG_ANALOG \
 *      -DFLUYT_ENGINE_INDEX=0 -DFLUYT_ROWS='"rows.def"' -DFLUYT_TARGET_MICROKORG2 \
 *      -DFLUYT_UNIT_NAME='"F-ANALOG"' -DFLUYT_UNIT_ID=0x464C5901U -DFLUYT_UNIT_PRESETS=12 \
 *      -DFLUYT_ENGINE_CUTOFF=1 -DFLUYT_ENGINE_SHAPE=1
 *   build/check_rows_microkorg2
 * (those are the unit's own UDEFS: hostcheck-microkorg2.sh reads them out of its config.mk, so there
 * is only ever one copy of them.) */

#ifndef FLUYT_ENGINE_SHAPE
#error "FLUYT_ENGINE_SHAPE must say whether this engine reads m->shape (see a unit's config.mk)"
#endif
#ifndef FLUYT_ENGINE_OWNENV
#error "FLUYT_ENGINE_OWNENV must say whether the engine's own envelopes are its amplitude"
#endif
#ifndef FLUYT_ENGINE_ONESHOT
#error "FLUYT_ENGINE_ONESHOT must say whether this engine is a hit (DRUM)"
#endif

#include <stdio.h>
#include <string.h>

#include "unit.h"                /* UNIT_MAX_PARAM_COUNT, the device's own (13 here) */
#include "runtime.h"             /* k_unit_target_microkorg2_osc, UNIT_API_VERSION, the module */

/* the unit's own translation unit, exactly as the unit builds it: one engine, this rows.def */
#include "../shim/fluyt_engine.c"

/* and the header itself, so what is checked is what the device is told rather than a second reading
 * of rows.def (this is also where FLUYT_UNIT_TYPE and the row count come from) */
#include "../shim/fluyt_microkorg2_header.c"

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

/* The parameters this panel does not have at all, each with the reason it does not. The first seven
 * are the walk's own skip set (tools/gen_rows.c SKIP_MICROKORG2): the whole envelope group, because
 * the instrument's EG is the envelope there and the unit's ADSR is held open in its place - which is
 * also why an engine's own HOME knob being ATK or REL (ANALOG's are) drops the same way, and is
 * checked below with the rest of the walk.
 *
 * P_LD_FLT is the one row of the LFO's four that is not here: the instrument's own LFO reaches the
 * unit's CUT knob through its virtual patch, so the row would be a second, worse way to do what the
 * instrument already does. P_VOICE has no meaning when the instrument's allocator is what plays the
 * voices, and the FM-1's own track is off both platforms' panels (the device's level and pan, the
 * sequencer's mute, and the two LFO rows that lost their slot to its four destinations). */
struct offrow {
  const char *name;                /* the parameter, as the walk in tools/gen_rows.c writes it */
  uint8_t id;
  const char *why;
};
static const struct offrow OFFROW[] = {
    {"P_ATK", P_ATK, "the envelope is the instrument's own EG"},
    {"P_DEC", P_DEC, "the envelope is the instrument's own EG"},
    {"P_SUS", P_SUS, "the envelope is the instrument's own EG"},
    {"P_REL", P_REL, "the envelope is the instrument's own EG"},
    {"P_ED_FLT", P_ED_FLT, "the envelope's filter amount, and the envelope is off this panel"},
    {"P_ED_PIT", P_ED_PIT, "the envelope's pitch amount, and the envelope is off this panel"},
    {"P_ED_SHP", P_ED_SHP, "the envelope's shape amount, and the envelope is off this panel"},
    {"P_LD_FLT", P_LD_FLT, "the instrument's own LFO reaches the CUT knob through its virtual patch"},
    {"P_VOICE", P_VOICE, "the instrument's allocator decides the voices, not the unit"},
    {"P_LEVEL", P_LEVEL, "level is the instrument's, one per timbre"},
    {"P_PAN", P_PAN, "pan is the instrument's, one per timbre"},
    {"P_MUTE", P_MUTE, "the instrument's mixer owns mute"},
    {"P_GLIDE", P_GLIDE, "the instrument's own glide reaches the unit through pitch[]"},
    {"P_LPHASE", P_LPHASE, "its slot went to the LFO's four destinations"},
    {"P_LFADE", P_LFADE, "its slot went to the LFO's four destinations"},
};
#define NOFFROW (sizeof(OFFROW) / sizeof(OFFROW[0]))

/* where the bridge holds the envelope, and what it holds it at: the panel has no envelope, so the
 * instrument's EG is the one that plays, and Felucca's own is open and out of the way (ATK 0 and
 * DEC 0 are its fastest, SUS 127 its longest hold, REL 127 its slowest release; the three amounts
 * are 0 because there is nothing on this panel for them to modulate). shim/fluyt_microkorg2.c's
 * fluyt_microkorg2_open_env is where this is, and this is what says it is still true. */
static const struct {
  uint8_t id;
  int16_t value;
} ENV_OPEN[7] = {
    {P_ATK, 0}, {P_DEC, 0}, {P_SUS, 127}, {P_REL, 127}, {P_ED_FLT, 0}, {P_ED_PIT, 0}, {P_ED_SHP, 0},
};

/* a block with no voice in it: what the runtime sends when no voice started, and what drains the
 * control thread's queue (a row, a preset). The lane pointers are never read - there are no voices -
 * so one entry of the slice is all this needs. */
static void drain_block(void)
{
  fluyt_microkorg2_voice_t none[1];
  memset(none, 0, sizeof none);
  fluyt_microkorg2_block(none, 0, CTL, 1);
}

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

static uint32_t on_panel(uint8_t id)
{
  uint32_t i;
  for (i = 0; i < NROWS; i++)
    if (fluyt_row_param[i] == id)
      return 1;
  return 0;
}

/* ------------------------------------------------ the panel the walk asks for --- */

/* The panel's shape, stated here a second time: the engine's own four HOME knobs first (each one the
 * engine's own parameter, whatever the row it lands on is called), then the engine's own eight
 * (P_E0..P_E7), then the LFO's destinations - and never P_LD_FLT, because the instrument's own LFO
 * reaches the unit's CUT row through its virtual patch, so the row would be a second, worse way to do
 * what the instrument already does.
 *
 * A parameter is on the panel once (ANALOG's CUT and RES knobs are also two of its eight, and show up
 * where the knobs put them), a dropped parameter is never on it however the walk names it (ANALOG's
 * other two knobs are ATK and REL, which this panel does not have), a row with no range is not shown
 * (Felucca has inert rows, e.g. PHASE's P_E7), and there are thirteen slots at most.
 *
 * tools/gen_rows.c computes the same walk, and this is what says rows.def is still that walk. What is
 * not restated here is which of Felucca's parameters exist, what they are called, or what they span:
 * that is the per-row check below, against Felucca's own descriptors. */
static uint8_t walk_panel[UNIT_MAX_PARAM_COUNT];
static uint32_t walk_n;

static void walk_row(uint8_t id)
{
  const param_desc_t *d = track_desc(&trk[0], id);
  uint32_t i;
  if (walk_n >= (uint32_t)UNIT_MAX_PARAM_COUNT || d->min == d->max)
    return;                                        /* out of slots, or a row that moves nothing */
  for (i = 0; i < NOFFROW; i++)
    if (OFFROW[i].id == id)
      return;
  for (i = 0; i < walk_n; i++)
    if (walk_panel[i] == id)
      return;
  walk_panel[walk_n++] = id;
}

static void walk_build(void)
{
  const engine_t *e = ENGINES[eng_idx(trk[0].eng_req)];
  uint32_t i;
  walk_n = 0;
  for (i = 0; i < 4u; i++)                         /* the engine's own HOME knobs */
    walk_row(e->knob[i]);
  for (i = 0; i < 8u; i++)                         /* and the rest of its eight */
    walk_row((uint8_t)(P_E0 + i));
  walk_row(P_LRATE);                               /* the LFO: its rate and wave, */
  walk_row(P_LWAVE);
  walk_row(P_LD_PIT);                              /* the three destinations that reach the engine, */
  if (FLUYT_ENGINE_SHAPE)
    walk_row(P_LD_SHP);                            /* the shape amount where the engine is shaped, */
  walk_row(P_LD_AMP);                              /* and the amplitude. No P_LD_FLT: OFFROW has it. */
}

int main(void)
{
  const engine_t *e;
  uint32_t i, j, bad = 0;

  if (!fluyt_init(48000.0f)) {
    printf("FAIL: the bridge refused to start\n");
    return 1;
  }
  e = ENGINES[eng_idx(trk[0].eng_req)];

  printf("%s: Felucca's %s, engine %u, %u rows, %u presets (microKORG2 osc module)\n",
         FLUYT_UNIT_NAME, e->name, (unsigned)FLUYT_ENGINE_INDEX, (unsigned)fluyt_num_params(),
         (unsigned)e->npresets);

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
  if (unit_header.target != k_unit_target_microkorg2_osc || unit_header.api != UNIT_API_VERSION) {
    printf("FAIL: the header is for target %#x api %u, this unit is %#x api %u\n",
           (unsigned)unit_header.target, (unsigned)unit_header.api,
           (unsigned)k_unit_target_microkorg2_osc, (unsigned)UNIT_API_VERSION);
    bad++;
  }

  /* the slots past the panel are the SDK's blank descriptor: a row the device would show as nothing */
  for (i = NROWS; i < UNIT_MAX_PARAM_COUNT; i++) {
    const unit_param_t *u = &unit_header.params[i];
    if (u->type != k_unit_param_type_none || u->name[0] || u->min || u->max || u->init) {
      printf("FAIL: slot %u past the panel is not blank: type %u \"%s\" %d..%d def %d\n",
             (unsigned)(i + 1), (unsigned)u->type, u->name, u->min, u->max, u->init);
      bad++;
    }
  }

  /* the panel, against the walk this check states for itself: the engine's own knobs, then its eight,
   * then the LFO. On this platform the engine is the whole instrument, so its knobs are the first
   * thing the panel shows - except where the walk has nothing behind the knob (ANALOG's ATK and REL),
   * which is where the count falls short of four. */
  walk_build();
  if (walk_n != NROWS) {
    printf("FAIL: the panel has %u rows, this walk's is %u\n", (unsigned)NROWS, (unsigned)walk_n);
    bad++;
  }
  for (i = 0; i < NROWS && i < walk_n; i++)
    if (fluyt_row_param[i] != walk_panel[i]) {
      printf("FAIL: row %u is %s, the walk has %s there\n", (unsigned)(i + 1),
             track_desc(&trk[0], fluyt_row_param[i])->label,
             track_desc(&trk[0], walk_panel[i])->label);
      bad++;
    }

  /* the panel against the walk's skip set, and against the FM-1's own track */
  for (i = 0; i < NOFFROW; i++)
    if (on_panel(OFFROW[i].id)) {
      printf("FAIL: %s (%s) is on the panel, but %s\n", OFFROW[i].name,
             track_desc(&trk[0], OFFROW[i].id)->label, OFFROW[i].why);
      bad++;
    }

  /* the LFO's shape amount is the one row of the walk that depends on the engine, and config.mk is
   * where the generator says which way: the row is there when the engine renders a shape, and gone
   * when it does not (a knob that moves nothing is a slot spent) */
  if (on_panel(P_LD_SHP) != (uint32_t)(FLUYT_ENGINE_SHAPE ? 1 : 0)) {
    printf("FAIL: P_LD_SHP is %s the panel, but FLUYT_ENGINE_SHAPE is %u\n",
           on_panel(P_LD_SHP) ? "on" : "not on", (unsigned)FLUYT_ENGINE_SHAPE);
    bad++;
  }

  /* and the two flags about the envelope are the generator's claim about the engine's own code, so
   * they are held to the engine descriptor the unit renders with */
  if ((uint32_t)(e->ownenv ? 1 : 0) != (uint32_t)FLUYT_ENGINE_OWNENV ||
      (uint32_t)(e->oneshot ? 1 : 0) != (uint32_t)FLUYT_ENGINE_ONESHOT) {
    printf("FAIL: config.mk says ownenv %u oneshot %u, Felucca's engine says %u and %u\n",
           (unsigned)FLUYT_ENGINE_OWNENV, (unsigned)FLUYT_ENGINE_ONESHOT, (unsigned)(e->ownenv ? 1 : 0),
           (unsigned)(e->oneshot ? 1 : 0));
    bad++;
  }

  /* the envelope, held open: nothing on the panel can move it, and the bridge puts it back at the head
   * of every block - including after a preset, which carries an envelope of its own */
  for (i = 0; i < sizeof ENV_OPEN / sizeof ENV_OPEN[0]; i++)
    trk[0].p[ENV_OPEN[i].id] = (int16_t)(ENV_OPEN[i].value == 0 ? 64 : 0);   /* somewhere else entirely */
  drain_block();
  for (i = 0; i < sizeof ENV_OPEN / sizeof ENV_OPEN[0]; i++)
    if (trk[0].p[ENV_OPEN[i].id] != ENV_OPEN[i].value) {
      printf("FAIL: a block left %s at %d, the bridge holds it at %d\n",
             track_desc(&trk[0], ENV_OPEN[i].id)->label, (int)trk[0].p[ENV_OPEN[i].id],
             (int)ENV_OPEN[i].value);
      bad++;
    }
  for (i = 0; i < fluyt_num_presets(); i++) {
    fluyt_load_preset((uint8_t)i);
    drain_block();
    for (j = 0; j < sizeof ENV_OPEN / sizeof ENV_OPEN[0]; j++)
      if (trk[0].p[ENV_OPEN[j].id] != ENV_OPEN[j].value) {
        printf("FAIL: preset %u (%s) closed the envelope: %s is %d, the bridge holds it at %d\n",
               (unsigned)i, fluyt_preset_name((uint8_t)i),
               track_desc(&trk[0], ENV_OPEN[j].id)->label, (int)trk[0].p[ENV_OPEN[j].id],
               (int)ENV_OPEN[j].value);
        bad++;
      }
  }
  fluyt_init(48000.0f);                     /* the presets above moved every row: start over */
  fluyt_load_preset(0);
  drain_block();

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

    /* the declaration the device holds: the same row, counted from zero (a row the unit formats
     * itself lives in a byte, so a negative minimum is a row the runtime cannot hold) */
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
    if (u->type != k_unit_param_type_strings) {   /* every row of Felucca's is one the unit formats:
                                                   * the panel shows "SINE", not a bare number */
      printf("FAIL: row %u (%s) is type %u, and this panel's rows are the unit's own text\n",
             (unsigned)i, d->label, (unsigned)u->type);
      bad++;
    }

    /* and it moves: the device's own bottom, default and top, through the control thread and the
     * block that drains it, with the engine's parameter and the device's text checked after each */
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
      drain_block();                              /* the control thread's row reaches the engine */
      if (trk[0].p[id] != want) {
        printf("FAIL: row %u (%s) at its %s shows %d but the parameter is %d\n", (unsigned)i,
               d->label, j == 0 ? "bottom" : j == 1 ? "default" : "top", (int)want,
               (int)trk[0].p[id]);
        bad++;
      }
    }
    fluyt_set_param((uint8_t)i, row_offset(d, d->def));   /* back where the panel starts it */
    drain_block();

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
