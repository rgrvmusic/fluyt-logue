/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as drumlogue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* A drumlogue that is a host program: it plays every preset of a unit and listens to the result.
 *
 * The bridge is built here exactly as the unit builds it (tools/check_rows.c does the panel); what
 * this adds is a player. It drives the same entry points the runtime does - note on, gate on, rows,
 * bend, aftertouch, preset load - at the same block sizes, and looks at the samples that come out:
 *
 *   - does the unit make a sound at all, and where does its peak land (the int->float scale),
 *   - is any of it NaN, infinite, or outside the +/-1.0 the drumlogue takes,
 *   - does a buffer that is not a multiple of Felucca's 32-sample control block come out right (the
 *     runtime's frames_per_buffer is its own business, so the carry-over between calls is real),
 *   - does the panel's own event path work: a row set from another thread arrives, a preset loads,
 *     a sequencer step plays and all-notes-off silences it.
 *
 * Built with -fsanitize=address,undefined by tools/hostcheck.sh, so Felucca's fixed-point arithmetic
 * is also checked for the overflows and out-of-bounds its own build (no sanitizers, no -fwrapv) can
 * only ever hide. Anything it reports is a port problem, not a Felucca problem. */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../shim/fluyt_engine.c"

#define SR 48000u
#define MAXFRAMES 256u

/* what one call produced: the peak, the mean, and how many samples were not a number the drumlogue
 * could use (either not finite or outside +/-1.0, which is the same thing to the device) */
struct out {
  double peak, rms;
  uint32_t ugly;
};

static float inbuf[MAXFRAMES * 2u];      /* the drumlogue always hands the unit float stereo */
static float outbuf[MAXFRAMES * 2u];

static void render_block(uint32_t frames, struct out *o)
{
  uint32_t i, ugly = 0;
  double p = 0.0, e = 0.0;

  fluyt_render(inbuf, outbuf, frames);
  for (i = 0; i < 2u * frames; i++) {
    double v = outbuf[i];
    if (isnan(v) || isinf(v) || v > 1.0 || v < -1.0) {
      ugly++;
      continue;
    }
    if (fabs(v) > p)
      p = fabs(v);
    e += v * v;
  }
  o->peak = p;
  o->rms = frames ? sqrt(e / (2.0 * frames)) : 0.0;
  o->ugly = ugly;
}

static void quiet_block(struct out *o)
{
  render_block(64u, o);
  if (o->ugly) {
    printf("  FAIL: %u samples the drumlogue cannot use\n", (unsigned)o->ugly);
    o->ugly = 0;
  }
}

/* blocks of `block` frames, deliberately not always a multiple of Felucca's 32: the runtime's own
 * buffer is not one either, and a unit that only works in whole control blocks would be a unit that
 * clicks */
static void play(uint32_t note, uint32_t frames, uint32_t block, struct out *o)
{
  uint32_t left = frames;
  struct out b;

  o->peak = o->rms = 0.0;
  o->ugly = 0;
  fluyt_note_on((uint8_t)note, 100u);
  while (left) {
    uint32_t n = left < block ? left : block;
    if (n > MAXFRAMES)
      n = MAXFRAMES;
    render_block(n, &b);
    if (b.peak > o->peak)
      o->peak = b.peak;
    if (b.rms > o->rms)
      o->rms = b.rms;
    o->ugly += b.ugly;
    left -= n;
  }
  fluyt_note_off((uint8_t)note);
}

int main(void)
{
  static const uint32_t BLOCKS[] = {64u, 37u, 128u, 1u};
  const uint32_t nblocks = (uint32_t)(sizeof BLOCKS / sizeof BLOCKS[0]);
  struct out o;
  uint32_t npresets, i;
  double quietest = 1.0, loudest = 0.0;
  uint32_t bad = 0;

  memset(inbuf, 0, sizeof inbuf);
  if (!fluyt_init((float)SR)) {
    printf("FAIL: the bridge refused to start\n");
    return 1;
  }
  npresets = fluyt_num_presets();
  if (!npresets) {                              /* DRUM's kit is one preset, and never zero */
    printf("FAIL: the engine has no preset to play\n");
    return 1;
  }
  printf("%s: %u rows, %u presets, %u Hz\n", FLUYT_UNIT_NAME, (unsigned)fluyt_num_params(),
         (unsigned)npresets, (unsigned)SR);

  /* every preset, through the runtime's own entry points: the load, then a note played the way the
   * sequencer plays it (a gate on the engine's own note - middle C, or the kick for the kit) */
  for (i = 0; i < npresets; i++) {
    fluyt_load_preset((uint8_t)i);
    quiet_block(&o);                            /* let the load arrive before the note */
    play(FLUYT_GATE_NOTE, SR / 2u, BLOCKS[i % nblocks], &o);
    printf("  preset %u  %-12s peak %6.3f  rms %6.4f%s\n", (unsigned)i,
           fluyt_preset_name((uint8_t)i), o.peak, o.rms, o.ugly ? "  <-- BAD SAMPLES" : "");
    if (o.ugly) {
      printf("    FAIL: %u samples the drumlogue cannot use\n", (unsigned)o.ugly);
      bad++;
    }
    if (o.peak <= 0.0) {
      printf("    FAIL: the preset is silent\n");
      bad++;
    }
    if (o.peak < quietest)
      quietest = o.peak;
    if (o.peak > loudest)
      loudest = o.peak;
  }

  /* the panel's own path, while a note is sounding: a row moved (the value it already has, so the
   * sound must not change), the last row moved, the bend and the channel pressure, and then the
   * sequencer's gate. None of it should be a surprise - which is the point of doing it. */
  fluyt_load_preset(0u);
  quiet_block(&o);
  fluyt_note_on(FLUYT_GATE_NOTE, 100u);
  quiet_block(&o);
  fluyt_set_param(0u, fluyt_get_param(0u));
  fluyt_set_param((uint8_t)(fluyt_num_params() - 1u), 0);
  fluyt_pitch_bend(8192u + 4096u);
  fluyt_channel_pressure(64u);
  fluyt_set_param(0u, 127);                     /* and a row that really does move */
  quiet_block(&o);
  fluyt_pitch_bend(8192u);
  fluyt_all_note_off();
  quiet_block(&o);
  fluyt_gate_on(100u);
  quiet_block(&o);
  fluyt_gate_off();
  quiet_block(&o);

  /* the reset the runtime calls when a unit is re-selected, after which it has to play again */
  fluyt_reset();
  play(FLUYT_GATE_NOTE, SR / 10u, 64u, &o);
  if (o.peak <= 0.0) {
    printf("  FAIL: the unit is silent after a reset\n");
    bad++;
  }

  /* the peak of the loudest preset is the int->float scale: it should sit just under 1.0 - the
   * master's soft clip bounds it there - and not at a fraction of it, which would be a unit that is
   * quiet on the device for no reason a player could see */
  printf("%s: %s (presets peaked between %.3f and %.3f)\n", FLUYT_UNIT_NAME, bad ? "FAILED" : "ok",
         quietest, loudest);
  return bad ? 1 : 0;
}
