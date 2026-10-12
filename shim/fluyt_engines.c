/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as drumlogue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* What Felucca's engines.c supplies to voice.c, for a unit that carries one engine instead of
 * fourteen: the engine table, eng_idx(), and the ENG_DRUM symbol voice.c compares against.
 * Felucca's factory patterns and its power-on sounds (engines.c PATTERNS / TRK_DEF) belong to the
 * sequencer, which a drumlogue unit has not: they are not here.
 *
 * A unit's table points every slot at its own engine, because an engine number it never asked for
 * is still a number it can be handed (a preset, a project, an old sound). The track keeps the
 * engine's real number (FLUYT_ENGINE_INDEX), so every comparison inside voice.c - "is this the
 * DRUM?" - keeps the meaning it has on the FM-1. */

#ifdef FLUYT_ALL_ENGINES

/* the host tools' view: the nine engines at Felucca's own numbers (engines.c ENGINES[]), the three
 * this family leaves out standing in as the nearest engine that is here. The eight indices are the
 * stores' and the editor protocol's, so they are Felucca's, never renumbered. */
static const engine_t *const ENGINES[NENGINES] = {
    &ENG_ANALOG,   /* 0 */
    &ENG_FM6,      /* 1: reserved (DIGITAL, retired: its sounds load as FM6) */
    &ENG_PHASE,    /* 2 */
    &ENG_LOFI,     /* 3 */
    &ENG_ANALOG,   /* 4: SAMPLE, not in this family (placeholder: never selected here) */
    &ENG_FORMANT,  /* 5 VOICE */
    &ENG_TRIO,     /* 6 */
    &ENG_WHEEL,    /* 7 */
    &ENG_ANALOG,   /* 8: GRAIN, not in this family (placeholder) */
    &ENG_ANALOG,   /* 9: PHYS, not in this family (placeholder) */
    &ENG_DRUM,     /* 10 */
    &ENG_NOISE,    /* 11 */
    &ENG_FM6,      /* 12 */
};

/* one row per unit this repository builds: the engine, at its number in ENGINES[] */
static const struct {
  const engine_t *eng;
  uint32_t index;
} FLUYT_FAMILY[] = {
    {&ENG_ANALOG, 0},  {&ENG_TRIO, 6},  {&ENG_PHASE, 2}, {&ENG_LOFI, 3},  {&ENG_WHEEL, 7},
    {&ENG_NOISE, 11},  {&ENG_FORMANT, 5}, {&ENG_DRUM, 10}, {&ENG_FM6, 12},
};
#define FLUYT_FAMILY_N (sizeof(FLUYT_FAMILY) / sizeof(FLUYT_FAMILY[0]))

#else /* one engine */

static const engine_t *const ENGINES[NENGINES] = {
    &FLUYT_ENGINE_SYM, &FLUYT_ENGINE_SYM, &FLUYT_ENGINE_SYM, &FLUYT_ENGINE_SYM,
    &FLUYT_ENGINE_SYM, &FLUYT_ENGINE_SYM, &FLUYT_ENGINE_SYM, &FLUYT_ENGINE_SYM,
    &FLUYT_ENGINE_SYM, &FLUYT_ENGINE_SYM, &FLUYT_ENGINE_SYM, &FLUYT_ENGINE_SYM,
    &FLUYT_ENGINE_SYM,
#if FELUCCA_SLICE
    &FLUYT_ENGINE_SYM,
#endif
};

#if FLUYT_ENGINE_INDEX != ENGI_DRUM
/* voice.c only ever compares against this (track_render: a DRUM track holds its bend at 0); the
 * table above never points at it, so nothing of it is ever rendered. The DRUM engine's own
 * definition (eng_drum.c) is the one that stands when this unit is the DRUM. */
static const engine_t ENG_DRUM = {0};

/* the same for the one thing voice.c calls on the DRUM engine's behalf (voice_alloc: a lane keeps
 * its voice). `t->engine` is this unit's engine and never ENGI_DRUM, so the call is not reached;
 * it has to exist for the link. */
static voice_t *drum_reuse(track_t *t, uint32_t note)
{
  (void)t;
  (void)note;
  return 0;
}
#endif

#endif /* FLUYT_ALL_ENGINES */

/* engines.c pulls in eng_sample.c, whose generated table of set names is what params.c's
 * enum_orig() and ui.c's preset_orig() compare a descriptor's names against (a retired set kept as
 * an alias). This family has not the SAMPLE engine: the comparison is against an empty table, so
 * every value is itself and the alias machinery is inert. */
static const char *const SMP_ALL_NAMES[1] = {0};
#define SMP_NSETS 0
static const uint8_t SMP_SET_ORIG[1] = {0};

/* a track's engine number as an index (engines.c: a compare, cheaper than % NENGINES) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }
