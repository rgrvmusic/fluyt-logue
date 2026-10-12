/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as drumlogue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see LICENSING.md. */
/* The whole bridge: the drumlogue's unit.c talks to Felucca through these, and nothing else. No
 * drumlogue type appears here, so a host build (tools/) can drive it as well. */
#ifndef FLUYT_ENGINE_H
#define FLUYT_ENGINE_H

#include <stdint.h>

/* ---- lifecycle (the audio thread) ---- */

/* Felucca's tables are generated for 48 kHz (build.sh), so this is a check, not a setup */
int fluyt_init(float samplerate);
void fluyt_reset(void);
void fluyt_render(const float *in, float *out, uint32_t frames);

/* ---- events (the drumlogue's control thread; applied on the audio thread) ---- */

void fluyt_note_on(uint8_t note, uint8_t velocity);
void fluyt_note_off(uint8_t note);
void fluyt_gate_on(uint8_t velocity);
void fluyt_gate_off(void);
void fluyt_all_note_off(void);
void fluyt_pitch_bend(uint16_t bend);
/* Felucca's AT (mod.c's modulation source) is the channel's aftertouch, so both of a drumlogue's
 * pressures are it; a per-note pressure has nowhere of its own to go. */
void fluyt_channel_pressure(uint8_t pressure);
void fluyt_set_param(uint8_t id, int32_t value);   /* id: the row (0..fluyt_num_params()-1) */
void fluyt_load_preset(uint8_t idx);               /* idx: 0..fluyt_num_presets()-1 */

/* ---- queries (any thread) ---- */

uint32_t fluyt_num_params(void);                   /* as many rows as the panel declares */
uint32_t fluyt_num_presets(void);
int32_t fluyt_get_param(uint8_t id);               /* the row's current value */
const char *fluyt_param_str(uint8_t id, int32_t value);   /* the row's value as the panel shows it */
const char *fluyt_preset_name(uint8_t idx);
uint8_t fluyt_preset_index(void);
const char *fluyt_engine_name(void);               /* Felucca's own name of the engine ("ANALOG") */

#endif /* FLUYT_ENGINE_H */
