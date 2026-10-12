/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as logue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* The microKORG2's render, as the bridge offers it: one block of one call, over the voices the
 * runtime says it covers. No microKORG2 type appears here - the runtime's own context is turned
 * into these by whoever holds it (shim/fluyt_microkorg2_unit.c on the unit, tools/microkorg2sim.c on the host) -
 * so this is the same bridge as fluyt_engine.h's, with the shape of a call that has lanes in it.
 *
 * shim/fluyt_microkorg2.c is the render itself; what it restates of Felucca's voice.c is named there. */
#ifndef FLUYT_MICROKORG2_H
#define FLUYT_MICROKORG2_H

#include <stdint.h>

/* one voice of the block: where to write it, and what the runtime says it is playing.
 *
 * `lane` is the voice's own buffer, and the samples go at lane[frame * stride]: the runtime's
 * voices are packed four to a group, so a group's voices interleave - see the layout in the
 * microKORG2 SDK's README ("Buffer layout") and docs/microkorg2.md. Every voice of the slice is
 * written by every call, silence included, because the runtime does not clear a lane for us. */
typedef struct {
  float *lane;                   /* the voice's first sample */
  float pitch;                   /* the note it is playing, a float MIDI number (ctxt->pitch[voice]) */
  uint8_t index;                 /* the voice: 0..7, Felucca's own voice of trk[0] (see fluyt_microkorg2.c) */
  uint8_t fresh;                 /* the runtime started this voice this block (the trigger bit's edge) */
} fluyt_microkorg2_voice_t;

/* Render `frames` samples of `nvoices` voices, whose samples step by `stride`. The block is the
 * runtime's, so it may be any length: Felucca ticks its envelopes, its LFO and its modulation once
 * per CTL (32) samples, and a block that is not a whole number of ticks carries the rest into the
 * next call, as fluyt_render does. */
void fluyt_microkorg2_block(const fluyt_microkorg2_voice_t *voices, uint32_t nvoices, uint32_t frames, uint32_t stride);

#endif /* FLUYT_MICROKORG2_H */
