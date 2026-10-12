/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as logue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* The drumlogue's side of a Fluyt unit: the unit_* entry points the runtime calls, each one line
 * over the bridge (shim/fluyt_engine.c). Nothing of Felucca is here, and nothing of the runtime
 * reaches the engine other than through shim/fluyt_engine.h - which is why the bridge can be driven
 * from a host program without a drumlogue anywhere.
 *
 * The drumlogue calls the playing entry points (note, gate, parameter, preset, bend) from its
 * control thread and Render from its audio thread. Every one of them here only appends to the
 * bridge's queue, and Render drains it: see shim/fluyt_queue.h.
 *
 * Every unit compiles this file with its own config.mk, which is where the two things that differ
 * between the nine - the name the device shows and the unit id - come from. */

#include <stdint.h>

#include "unit.h"

#include "fluyt_engine.h"

#ifndef FLUYT_UNIT_NAME
#error "FLUYT_UNIT_NAME must be the name the device shows (see a unit's config.mk)"
#endif
#ifndef FLUYT_UNIT_ID
#error "FLUYT_UNIT_ID must be this unit's id in the Fluyt family (see a unit's config.mk)"
#endif

__unit_callback int8_t unit_init(const unit_runtime_desc_t *desc)
{
  if (!desc)
    return k_unit_err_undef;
  if (desc->target != (UNIT_TARGET_PLATFORM | k_unit_module_synth))
    return k_unit_err_target;
  if (!UNIT_API_IS_COMPAT(desc->api))
    return k_unit_err_api_version;
  if (desc->output_channels != 2)
    return k_unit_err_geometry;
  if (desc->samplerate != 48000)
    return k_unit_err_samplerate;      /* the tables are generated for 48 kHz: see README.md */
  if (!fluyt_init((float)desc->samplerate))
    return k_unit_err_undef;
  return k_unit_err_none;
}

__unit_callback void unit_teardown(void)
{
  fluyt_all_note_off();
}

__unit_callback void unit_reset(void)
{
  fluyt_reset();
}

__unit_callback void unit_resume(void)
{
}

__unit_callback void unit_suspend(void)
{
  fluyt_all_note_off();
}

__unit_callback void unit_render(const float *in, float *out, uint32_t frames)
{
  fluyt_render(in, out, frames);
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
                                        * arpeggiator and slicer are not part of a unit) */
}

__unit_callback void unit_note_on(uint8_t note, uint8_t velocity)
{
  fluyt_note_on(note, velocity);
}

__unit_callback void unit_note_off(uint8_t note)
{
  fluyt_note_off(note);
}

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

__unit_callback void unit_pitch_bend(uint16_t bend)
{
  fluyt_pitch_bend(bend);
}

__unit_callback void unit_channel_pressure(uint8_t pressure)
{
  fluyt_channel_pressure(pressure);
}

__unit_callback void unit_aftertouch(uint8_t note, uint8_t pressure)
{
  (void)note;                          /* Felucca's AT is the channel's: see fluyt_engine.h */
  fluyt_channel_pressure(pressure);
}
