/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as logue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* The microKORG2's side of a Fluyt unit: the unit_* entry points the runtime calls, each one line
 * over the bridge (shim/fluyt_engine.c, and shim/fluyt_microkorg2.c for the render). Nothing of Felucca is
 * here, and nothing of the runtime reaches the engine other than through shim/fluyt_engine.h.
 *
 * What this file does that the drumlogue's (shim/fluyt_unit.c) does not is turn the runtime's
 * oscillator context into a block: the runtime does not call Render once per voice, it calls it once
 * per block with a context that says how the voices lie in `out` (the lane geometry) and what each
 * of them is playing. That is the whole of the difference between the two platforms' unit files.
 *
 * This is the one translation unit that may include unit_osc.h: the header defines GetBufferOffset
 * and the WriteUnitModData helpers rather than declaring them, so a second inclusion would be a
 * second definition at link time.
 *
 * The three entry points 2.1.0 deprecates - unit_pitch_bend, unit_channel_pressure and
 * unit_aftertouch - are not defined here. The runtime folds the bend into the block's pitch[]
 * (shim/fluyt_microkorg2.c follows it there), and the instrument's own modulation reaches a unit through
 * the context's mod outputs, not through a pressure callback. */

#include <stdint.h>

#include "unit.h"
#include "unit_osc.h"

#include "fluyt_engine.h"
#include "fluyt_microkorg2.h"

#ifndef FLUYT_UNIT_NAME
#error "FLUYT_UNIT_NAME must be the name the device shows (see a unit's config.mk)"
#endif
#ifndef FLUYT_UNIT_ID
#error "FLUYT_UNIT_ID must be this unit's id in the Fluyt family (see a unit's config.mk)"
#endif

/* the runtime's descriptor, kept because the block context hangs off it (hooks.runtime_context is
 * the runtime's own, filled in for every block) */
static unit_runtime_desc_t microkorg2_desc;

/* The voices the runtime had triggered as of the last block. `trigger` is a level, one bit per
 * voice, so the edge is what says a voice started this block - a bit that stays high is a voice
 * that is still being held. */
static uint8_t microkorg2_trigger;

__unit_callback int8_t unit_init(const unit_runtime_desc_t *desc)
{
  if (!desc)
    return k_unit_err_undef;
  if (desc->target != unit_header.target)
    return k_unit_err_target;
  if (!UNIT_API_IS_COMPAT(desc->api))
    return k_unit_err_api_version;
  if (desc->samplerate != 48000)
    return k_unit_err_samplerate;      /* the tables are generated for 48 kHz: see README.md */
  if (desc->input_channels != 0 || desc->output_channels != kMk2MaxVoices)
    return k_unit_err_geometry;        /* an oscillator: eight lanes out, one per voice, nothing in */
  if (!fluyt_init((float)desc->samplerate))
    return k_unit_err_undef;
  microkorg2_desc = *desc;
  microkorg2_trigger = 0;
  return k_unit_err_none;
}

__unit_callback void unit_teardown(void)
{
  fluyt_all_note_off();
}

__unit_callback void unit_reset(void)
{
  fluyt_reset();
  microkorg2_trigger = 0;
}

__unit_callback void unit_resume(void)
{
}

__unit_callback void unit_suspend(void)
{
  fluyt_all_note_off();
}

/* One block: the voices this call covers, each in its own lane. See the layout in the microKORG2
 * SDK's README ("Buffer layout") and docs/microkorg2.md - a voice's samples are at
 * lane[frame * outputStride], the lane starts at GetBufferOffset(ctxt, voice, frames), and a group
 * of four voices shares one frame's run. */
__unit_callback void unit_render(const float *in, float *out, uint32_t frames)
{
  const unit_runtime_osc_context_t *ctxt =
      (const unit_runtime_osc_context_t *)microkorg2_desc.hooks.runtime_context;
  fluyt_microkorg2_voice_t slice[kMk2MaxVoices];
  uint8_t fresh;
  uint32_t i, n = 0;
  (void)in;
  if (!ctxt || !frames)
    return;
  fresh = (uint8_t)(ctxt->trigger & (uint8_t)~microkorg2_trigger);
  microkorg2_trigger = ctxt->trigger;
  for (i = 0; i < (uint32_t)ctxt->voiceLimit && n < kMk2MaxVoices; i++) {
    uint32_t voice = (uint32_t)ctxt->voiceOffset + i;
    if (voice >= kMk2MaxVoices)
      continue;                        /* never: voiceOffset + voiceLimit <= kMk2MaxVoices */
    slice[n].lane = out + GetBufferOffset(ctxt, (int)voice, (int32_t)frames) + (voice & 3u);
    slice[n].pitch = ctxt->pitch[voice];
    slice[n].index = (uint8_t)voice;
    slice[n].fresh = (uint8_t)((fresh >> voice) & 1u);
    n++;
  }
  /* An empty slice still ticks: the events (a preset, a row) are the bridge's, and they are drained
   * at the head of a block whether or not there is a voice in it. */
  fluyt_microkorg2_block(slice, n, frames, ctxt->outputStride ? ctxt->outputStride : 1u);
}

__unit_callback uint8_t unit_get_preset_index(void)
{
  return fluyt_preset_index();
}

__unit_callback const char *unit_get_preset_name(uint8_t idx)
{
  return fluyt_preset_name(idx);
}

__unit_callback void unit_load_preset(uint8_t idx)
{
  fluyt_load_preset(idx);
}

/* the panel reads the row, not the runtime's own record of what was last set: a row that has not
 * been moved is the sound's own value, which is what the device shows and what the FM-1 shows (see
 * README.md). */
__unit_callback int32_t unit_get_param_value(uint8_t id)
{
  return fluyt_get_param(id);
}

__unit_callback const char *unit_get_param_str_value(uint8_t id, int32_t value)
{
  return fluyt_param_str(id, value);
}

__unit_callback const uint8_t *unit_get_param_bmp_value(uint8_t id, int32_t value)
{
  (void)id;
  (void)value;
  return NULL;                         /* no bitmap rows: every row is a number or a name */
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value)
{
  fluyt_set_param(id, value);
}

__unit_callback void unit_set_tempo(uint32_t tempo)
{
  (void)tempo;                         /* the engine has no clock of its own (the FM-1's sequencer,
                                        * arpeggiator and slicer are not part of a unit), and neither
                                        * the LFO nor an envelope of Felucca's is tempo-relative */
}

__unit_callback void unit_note_on(uint8_t note, uint8_t velocity)
{
  fluyt_note_on(note, velocity);
}

__unit_callback void unit_note_off(uint8_t note)
{
  fluyt_note_off(note);
}

/* A gate is a velocity and no note: shim/fluyt_microkorg2.c reads it as one (the trigger bit is what says
 * a voice started, and pitch[] says at what). */
__unit_callback void unit_gate_on(uint8_t velocity)
{
  fluyt_gate_on(velocity);
}

__unit_callback void unit_gate_off(void)
{
  fluyt_gate_off();
}

__unit_callback void unit_all_note_off(void)
{
  fluyt_all_note_off();
}
