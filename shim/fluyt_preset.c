/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as drumlogue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see LICENSING.md. */
/* Loading a sound, and the state a track starts in. Felucca applies a preset in ui.c
 * (apply_preset_to, ui.c:726) and its engine defaults in set_engine_of (ui.c:774); both are the UI
 * file, which a unit has not, so the two are re-stated here with the same meaning. Everything they
 * reach into the UI for - the undo copy (load_begin/load_end), the motion snapshot, G_ENGSEL/the
 * editor's audition - is not part of a sound. What is Felucca's is kept: the same parameters go to
 * their defaults, the same eight take the preset's values, and a sound load still leaves the
 * track's own settings alone (param_kept, ui.c:655). Included after voice.c: a load releases what
 * sounds, which is the panic Felucca asks the audio ISR for (ui.c:741). */

/* the track's settings, not the sound's (ui.c:655): what a sound load leaves alone - the mix, the
 * arpeggiator, the scale and key map, the pattern's parameters, the SLICER insert and the chord
 * keys. Only LVL, PAN and MUTE of these are rows of a unit; the rest is kept for parity. */
static int fluyt_param_kept(uint32_t i)
{
  return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_AMODE && i <= P_SGATE) ||
         (i >= P_SLCR && i <= P_SLDEPTH) || i == P_CHRD || i == P_VOIC;
}

/* preset pi of the engine: the sound only (ui.c:726). t->eng_req is the engine this unit carries. */
static void apply_preset_to(track_t *t, uint32_t pi)
{
  const engine_t *e = ENGINES[eng_idx(t->eng_req)];
  uint32_t i;
  if (!e->npresets)
    return;
  pi = pi % e->npresets;
  trk_all_off(t);                                     /* MONO/POLY may change: release what sounds */
  t->preset = (uint8_t)pi;
  for (i = 0; i < P_E0; i++)                          /* the rest of the sound to its defaults: a preset */
    if (!fluyt_param_kept(i))                         /* sounds the same after any edit */
      t->p[i] = TP[i].def;
  for (i = 0; i < 8u; i++)
    t->p[P_E0 + i] = (int16_t)e->presets[pi].e[i];
  t->p[P_ATK] = e->presets[pi].env[0];
  t->p[P_DEC] = e->presets[pi].env[1];
  t->p[P_SUS] = e->presets[pi].env[2];
  t->p[P_REL] = e->presets[pi].env[3];
  t->p[P_ED_FLT] = e->presets[pi].fenv;
  t->p[P_VOICE] = e->presets[pi].mono ? V_LEGATO : V_POLY;   /* mono presets keep the legato feel */
  {                                                   /* the sends (fx.c's buses; a unit has no effects, */
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};  /* but the sound's record holds them) */
    const preset_t *pr = &e->presets[pi];
    for (i = 0; i < 4u; i++)
      t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
  }
#if defined(FLUYT_ALL_ENGINES) || FLUYT_ENGINE_INDEX == 12
  fm6_track_loaded(t);                                /* FM6: the preset's patch (its PTCH) */
#endif
}

/* the engine's defaults and its first preset (ui.c:774 set_engine_of, after ui.c:799 track_defaults
 * has put the track's own parameters where Felucca starts them). A drumlogue unit is powered on with
 * no project behind it, so both halves are needed here: set_engine_of alone leaves the parameters a
 * preset does not carry - LEVEL among them, which is a track's own and not the sound's - at whatever
 * the memory held, and a track at LEVEL 0 is a unit that makes no sound (tools/hostsim.c is what
 * says so). */
static void fluyt_track_init(track_t *t)
{
  uint32_t i, j;
  t->engine = t->eng_req = (uint8_t)FLUYT_ENGINE_INDEX;
  t->user = 0;
  t->xf_on = t->xf = 0;
  t->nheld = 0;
  t->nmono = t->mono_note = 0;
  t->xp_n = 0;
  for (i = 0; i < NVOICE; i++) {
    voice_t *v = &t->v[i];
    v->active = v->gate = 0;
    v->stage = 0;
    v->env = v->env_out = 0;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    for (j = 0; j < 8u; j++)                        /* (kept engines hold state: start from silence) */
      v->s[j] = 0;
  }
  for (i = 0; i < P_E0; i++)                        /* the track's own parameters, as Felucca */
    t->p[i] = TP[i].def;                            /* starts them (ui.c:799 track_defaults) */
  for (i = 0; i < 8u; i++)                          /* .. and the engine's own defaults */
    t->p[P_E0 + i] = ENGINES[eng_idx(t->eng_req)]->edit[i].def;
  apply_preset_to(t, 0);
}
