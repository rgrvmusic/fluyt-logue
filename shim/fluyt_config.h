/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as drumlogue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* What a unit tells the bridge about itself. Every unit's config.mk passes these in UDEFS, one
 * engine each; the host generator (tools/gen_rows.c) passes FLUYT_ALL_ENGINES instead and gets all
 * nine, so one program can read every engine's descriptors. */
#ifndef FLUYT_CONFIG_H
#define FLUYT_CONFIG_H

#ifdef FLUYT_ALL_ENGINES

/* every engine of the family in one translation unit (the host tools/): the nine listed in
 * shim/fluyt_engines.c's FLUYT_FAMILY[], in engines.c's own order */

#else /* one engine: what config.mk says */

#ifndef FLUYT_ENGINE_C
#error "FLUYT_ENGINE_C must name the Felucca engine source (see a unit's config.mk)"
#endif
#ifndef FLUYT_ENGINE_SYM
#error "FLUYT_ENGINE_SYM must name Felucca's engine_t symbol (see a unit's config.mk)"
#endif
#ifndef FLUYT_ENGINE_INDEX
#error "FLUYT_ENGINE_INDEX must be the engine's number in Felucca's ENGINES[]"
#endif

#endif /* FLUYT_ALL_ENGINES */

/* A sequencer step on drumlogue arrives as a gate with no key behind it, so it plays at one note.
 * Felucca's DRUM presets and patterns are General MIDI numbered, 36 is the kick. */
#ifndef FLUYT_GATE_NOTE
#define FLUYT_GATE_NOTE 60
#endif

/* pitch bend: the runtime's bend is 14 bit, 8192 = centre; Felucca takes Q8 semitones */
#ifndef FLUYT_BEND_SEMITONES
#define FLUYT_BEND_SEMITONES 2
#endif

#endif /* FLUYT_CONFIG_H */
