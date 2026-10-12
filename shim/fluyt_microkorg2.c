/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as logue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* The microKORG2's render: Felucca's engine, one voice per lane.
 *
 * Included by shim/fluyt_engine.c under FLUYT_TARGET_MICROKORG2 the way fluyt_preset.c already is, because
 * it is the same translation unit: what it restates is voice.c's track_render (voice.c:513-605), and
 * what it reaches for is Felucca's own statics (trk[], voice_start, voice_reuse, env_tick, mod_note,
 * track_lfo_tick) and this bridge's (the queue, the rows, the mix).
 *
 * The one thing that differs is who allocates. On the drumlogue a note arrives and Felucca's
 * voice_alloc picks a voice out of the eight; on the microKORG2 the runtime plays up to eight voices
 * of its own and tells the unit, once per block, which of them started (ctxt->trigger) and what each
 * is playing (ctxt->pitch). So the device's voice i *is* Felucca's voice i of trk[0] and there is
 * nothing to choose: the whole of track_render's per-voice body - the envelope, glide, the pitch and
 * cutoff modulation, the engine's own render - runs once per voice into that voice's own lane,
 * instead of eight voices into one buffer of a part.
 *
 * What is not here, and why:
 *   - the engine switch (voice.c t->xf/t->pe_old, engine_block): a unit carries the one engine it was
 *     built with (FLUYT_ENGINE_INDEX), so a part never switches and never fades;
 *   - the voice budget (voice_room/voice_kill, the stage-4 give-up): the instrument's allocator is
 *     the one that decides which voice a new note takes, and it does not take one from this unit;
 *   - the mix (fx.c mix_part's LEVEL_Q12, the pan, song.master_q12): level and pan are the
 *     instrument's own - they are on its panel, one per timbre - so the lane holds the voice, and the
 *     master chain alone is Felucca's (see fluyt_microkorg2_master);
 *   - P_VOICE and the mono/legato/unison stack: the panel has no voice mode (tools/gen_rows.c) and a
 *     voice is one the instrument started, which makes this path always POLY - what trk_vmode()
 *     answers for P_VOICE 0, and for an engine of hits.
 *
 * Everything else is track_render's own arithmetic, in its own order: the lines are marked where
 * they come from, so the two can be read side by side (tools/microkorg2sim.c holds them to each other). */

/* ------------------------------------------------------------- state --- */

/* one control tick of the part, as track_render computes it once for all of its voices (voice.c:517):
 * the LFO's faded value and the song's TUNE are the same for every voice of the block */
struct fluyt_microkorg2_tick {
  int32_t lfo;                 /* the LFO, faded (voice.c:518) */
  int32_t tune, tune_fine;     /* TUNE: whole 1/16 semitones, and the rest as a fine factor */
  uint32_t n;                  /* the frames of one tick: CTL */
};

/* the voices' control-rate samples: one row of CTL per voice, keyed by Felucca's voice number -
 * which is the device's, see fluyt_microkorg2_block - so that a tick's remainder carries into the next
 * call the way a block's does in fluyt_render. Row NVOICE is never rendered into and never zeroed,
 * so a caller that names a voice outside the eight gets silence rather than another voice's samples
 * (the runtime does not: voiceOffset + voiceLimit <= kMk2MaxVoices = NVOICE). */
static int32_t microkorg2_buf[NVOICE + 1u][CTL];
static uint32_t microkorg2_have, microkorg2_sent;

/* fx.c master_out's state and the bend, per voice: a voice is its own part-sized signal on this
 * platform (there is no summing buffer before the lanes), so what fx.c keeps once for a part, this
 * keeps once per voice. One row more than there are voices, for microkorg2_buf's own reason. */
static int32_t microkorg2_dc[NVOICE + 1u], microkorg2_dcerr[NVOICE + 1u], microkorg2_lim[NVOICE + 1u];
static int32_t microkorg2_bend_q8[NVOICE], microkorg2_bend_target[NVOICE];

/* the velocity of each key (0 = none of the callbacks has named it), and the last velocity any of
 * them carried: velocity is the one thing the block context does not have (see fluyt_microkorg2_drain).
 * A voice the callbacks did not name plays at FLUYT_MICROKORG2_VEL, the same default Gerard's own units
 * use for a retrigger. */
#define FLUYT_MICROKORG2_VEL 100u
static uint8_t microkorg2_vel[128];
static uint8_t microkorg2_vel_last = FLUYT_MICROKORG2_VEL;

/* -------------------------------------------------- the open envelope --- */

/* The envelope is not on this panel: the instrument's own EG is the envelope, and Felucca's ADSR is
 * held open so that it is. Felucca's envelope still runs (env_tick) - which is what gives a note-off
 * its release - it is simply never in the way of the instrument's.
 *
 * Set at the head of every block - after that block's events, which is where the drain calls it -
 * rather than once, because a preset carries its own envelopes and apply_preset_to writes them: on
 * this platform a preset may change what the panel can show (the engine's eight, the LFO) and
 * nothing else.
 *
 *   ATK 0     Felucca's fastest attack (ENV_LIN[0]: full in about 1.3 ms at 48 kHz),
 *   DEC 0     .. and its fastest decay, so the sustain is reached at once,
 *   SUS 127   its longest hold: while the key is down the voice is at full,
 *   REL 127   its slowest release, so the release the instrument's EG plays is the audible one
 *             (the two multiply; the longer of them is the one that is still open),
 *   the three amounts 0, because there is nothing here for the envelope to modulate: the LFO's own
 *   PIT is a row of this panel, the filter's is the instrument's to patch, and the engine's shape
 *   amount is the LFO's (P_LD_SHP) - the envelope's SHP is the row the drumlogue panel has and this
 *   one does not. */
static void fluyt_microkorg2_open_env(void)
{
  trk[0].p[P_ATK] = 0;
  trk[0].p[P_DEC] = 0;
  trk[0].p[P_SUS] = 127;
  trk[0].p[P_REL] = 127;
  trk[0].p[P_ED_FLT] = 0;
  trk[0].p[P_ED_PIT] = 0;
  trk[0].p[P_ED_SHP] = 0;
}

/* ------------------------------------------------ the voices the runtime started --- */

/* The bend of a voice that is at pitch16 (1/16 semitones) and was started at note, in the Q8
 * semitones voice.c's own MIDI state and track_render's bend16/bend_fine are in.
 *
 * A DRUM voice holds its bend at 0, as it does on the FM-1 (voice.c:524): its lanes are hits, and
 * each hit's lane comes from the note (eng_drum.c drum_note_on), so a bend would smear them. */
static int32_t fluyt_microkorg2_bend(int32_t pitch16, uint32_t note)
{
  if (ENGINES[eng_idx(trk[0].eng_req)] == &ENG_DRUM)
    return 0;
  return (pitch16 - (int32_t)note * 16) * 16;
}

/* What the runtime says a voice is playing now, whether it started this block or not: the bend is
 * the difference between that pitch and the note the voice was started at.
 *
 * This is where the instrument's own bend and its portamento reach the engine. The osc ABI has no
 * unit_pitch_bend - 2.1.0 deprecates it and the runtime folds it into pitch[] - so the unit follows
 * the pitch it is handed, per block, and voice.c's own MIDI bend state (midi_bend_target) is not
 * this path's. Nothing clamps it to a bend range: the instrument's range is the instrument's, and
 * the field Felucca holds the result in is the one that limits it (0..2047, as track_render's
 * clamp). */
static void fluyt_microkorg2_follow(const fluyt_microkorg2_voice_t *v)
{
  if (v->index >= NVOICE || !(v->pitch > 0.0f))     /* no index, no note (or a NaN): nothing to follow */
    return;
  microkorg2_bend_target[v->index] = fluyt_microkorg2_bend((int32_t)(v->pitch * 16.0f + 0.5f),
                                             trk[0].v[v->index].note);
}

/* A voice the runtime has started this block: Felucca's own note-on (voice.c:340 trk_note_on) with
 * the voice already decided, which is the whole difference between the two platforms. The note is
 * what the runtime says it is playing, rounded to the 1/16 semitone Felucca holds a note at; the
 * velocity is the one the callbacks carried for that key; and the bend starts where the runtime has
 * it, because there is nothing to ease from - this voice's previous note is not this note.
 *
 * The instrument's allocator is what reused this voice, so there is no voice_alloc here and no
 * rotation or steal: voice_reuse is the same "give me this one" trk_note_on asks for, and it is a
 * no-op unless a budget was taken from the voice (which this path never takes). */
static void fluyt_microkorg2_fresh(const fluyt_microkorg2_voice_t *v)
{
  track_t *t = &trk[0];
  voice_t *nv;
  uint32_t i, any = 0, note, vel;
  int32_t pitch16;
  if (v->index >= NVOICE || !(v->pitch > 0.0f))
    return;                                           /* a bit with no note behind it: nothing to start */
  pitch16 = (int32_t)(v->pitch * 16.0f + 0.5f);
  note = (uint32_t)(pitch16 < 0 ? 0 : pitch16 >= 127 * 16 ? 127 : pitch16 >> 4);
  vel = microkorg2_vel[note] ? microkorg2_vel[note] : microkorg2_vel_last;
  mod_note(t, note, vel);                             /* the matrix's VEL / KEY / RAND (voice.c:355) */
  for (i = 0; i < NVOICE; i++)                        /* voice.c:356: a fresh phrase reseeds the LFO */
    any |= t->v[i].gate;
  if (!any) {
    t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
    t->lfo_fade = 0;
  }
  nv = voice_reuse(t, &t->v[v->index]);               /* voice.c:363, with the voice decided already */
  nv->fine = 0;
  voice_start(t, nv, note, vel, t->p[P_GLIDE] != 0 && !ENGINES[t->engine]->oneshot);
  microkorg2_bend_q8[v->index] = microkorg2_bend_target[v->index] = fluyt_microkorg2_bend(pitch16, note);
}

/* A note-off: Felucca's own (voice.c:412), which releases every voice holding that note. The
 * instrument decides which voice that is, so the note is all there is to go on - which is how the
 * drumlogue's own path finds it too. A voice that is not gated is not that note's any more. */
static void fluyt_microkorg2_release(uint32_t note)
{
  uint32_t i;
  for (i = 0; i < NVOICE; i++)
    if (trk[0].v[i].note == note && trk[0].v[i].gate) {
      trk[0].v[i].gate = 0;
      trk[0].v[i].stage = 3;
    }
}

/* ------------------------------------------------------------- events --- */

/* The bridge's events, drained at the head of a block, plus the envelope held open above. Every
 * event means what it means on the drumlogue except for two:
 *
 *   - a gate carries a velocity and no note. On the drumlogue that is a sequencer step and it plays
 *     at FLUYT_GATE_NOTE; here the runtime's trigger bit is what says a voice started, and a note-on
 *     is what says at what, so a gate only ever says how hard (a keyboard that reports through the
 *     gate, and the instrument's own arp and sequencer, still get their velocity in);
 *   - FQ_BEND has nowhere to go: the bend arrives in the block's pitch[] (see fluyt_microkorg2_follow), and
 *     nothing on this platform calls fluyt_pitch_bend. */
static void fluyt_microkorg2_drain(void)
{
  fluyt_event_t e;
  while (fq_pop(&e)) {
    switch (e.type) {
    case FQ_NOTE_ON:
      if (e.b <= 0)
        fluyt_microkorg2_release(e.a);                       /* velocity 0: the MIDI reading of a note-on */
      else {
        microkorg2_vel[e.a & 127u] = (uint8_t)e.b;
        microkorg2_vel_last = (uint8_t)e.b;
      }
      break;
    case FQ_NOTE_OFF:
      fluyt_microkorg2_release(e.a);
      break;
    case FQ_GATE_ON:
      if (e.b > 0)
        microkorg2_vel_last = (uint8_t)e.b;
      break;
    case FQ_GATE_OFF:
      break;                                          /* nothing: no note was started by the gate */
    case FQ_ALL_OFF:
      trk_all_off(&trk[0]);
      break;
    case FQ_PARAM:
      fluyt_apply_row(e.a, e.b);
      break;
    case FQ_PRESET:
      apply_preset_to(&trk[0], (uint32_t)e.a);
      fluyt_preset = (uint8_t)((uint32_t)e.a % fluyt_num_presets());
      fluyt_rows_from_track();
      break;
    case FQ_BEND:
      break;
    case FQ_AT:                                       /* mod.c's AT source: channel aftertouch, 0..127 */
      trk[0].at = (uint8_t)(e.b < 0 ? 0 : e.b > 127 ? 127 : e.b);
      break;
    }
  }
  fluyt_microkorg2_open_env();   /* last, so that a preset's own envelope never reaches a render: the
                                  * events are the only thing that can move these, and the envelope the
                                  * instrument plays with is the one this block renders */
}

/* ---------------------------------------------------------- the render --- */

/* One voice's control tick, into its own buffer (cleared here: the engines add into it, as they do
 * into track_render's out). This is track_render's voice loop, for one voice and one lane. */
static void fluyt_microkorg2_voice(const struct fluyt_microkorg2_tick *k, uint32_t i, int32_t *out)
{
  track_t *t = &trk[0];
  const engine_t *e = ENGINES[t->engine];
  const int16_t *p = t->p;
  voice_t *v = &t->v[i];
  vmod_t m;
  int32_t env, pitch, bend, bend16, bend_fine;
  uint32_t f;

  for (f = 0; f < k->n; f++)
    out[f] = 0;
  if (!v->active)
    return;
  {                                  /* this voice's bend, eased towards what the runtime has it at
                                      * (voice.c:527): the same ~6 ms with an exact landing, the
                                      * target taken from the context at the head of the block */
    int32_t d = microkorg2_bend_target[i] - microkorg2_bend_q8[i];
    microkorg2_bend_q8[i] += d > 0 ? (d < 4 ? d : (d + 3) / 4) : (d > -4 ? d : (d - 3) / 4);
  }
  bend = microkorg2_bend_q8[i];
  bend16 = bend >= 0 ? bend / 16 : -((-bend + 15) / 16);
  bend_fine = (bend - bend16 * 16) * 2367 / 2560;

  env = env_tick(t, v);
  if (e->ownenv && v->active) {                       /* the engine's envelopes (FM6): they end the voice */
    if (e->done(t, v)) {
      v->active = v->gate = 0;
      v->stage = 0;
      v->env = v->env_out = 0;
      return;
    }
    v->env = 1 << 24;                                 /* (the ADSR's release never ends it) */
    env = 32767;
  }
  if (e->amp)                                         /* the engine's own amplitude curve */
    env = e->amp(t, v, env);
  m.envq15 = e->ownenv ? 0 : env;
  m.amp1 = e->ownenv ? env : mulq15(env, v->vel * 258);
  if (p[P_LD_AMP])
    m.amp1 = mulq15(m.amp1, 32767 - mulq15((k->lfo + 32768) >> 1, p[P_LD_AMP] * 258));
  m.amp0 = v->env_out;
  v->env_out = m.amp1;
  if (v->pitch_cur != v->pitch16) {                   /* glide */
    int32_t d = v->pitch16 - v->pitch_cur;
    int32_t st = v->gstep ? v->gstep : (int32_t)(ENV_LIN[(p[P_GLIDE] ? p[P_GLIDE] : 40) & 127] >> 14);   /* slide: ~45 ms */
    if (st < 1)
      st = 1;
    v->pitch_cur += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
  }
  if (!env && !m.amp0 && v->stage == 2 && !e->sampled)
    return;                                           /* held at a silent sustain: nothing to render */
  pitch = v->pitch_cur + k->tune + bend16 + ((k->lfo * p[P_LD_PIT] * 3) >> 15) + ((m.envq15 * p[P_ED_PIT] * 3) >> 15);
  m.pitch16 = clamp(pitch, 0, 2047);
  m.inc = pitch_inc(m.pitch16);
  m.fine = v->fine + k->tune_fine + bend_fine;
  if (v->fine + k->tune_fine + bend_fine)             /* residual below 1/16 semitone */
    m.inc += (uint32_t)((int32_t)(m.inc >> 12) * (v->fine + k->tune_fine + bend_fine));
  m.cutoff = ((k->lfo * p[P_LD_FLT]) >> 7) + ((m.envq15 * p[P_ED_FLT]) >> 7);
  if (v->vel > 110)                                   /* accent opens the filter with the env */
    m.cutoff += (m.envq15 * 24) >> 7;
  m.shape = (64 << 8) + ((k->lfo * p[P_LD_SHP]) >> 7) + ((m.envq15 * p[P_ED_SHP]) >> 7);
  if (mod.on)                                         /* the modulation matrix (mod.c) */
    mod_voice(t, v, &m, v->fine + k->tune_fine + bend_fine);
  e->render(t, v, out, k->n, &m);
}

/* One control tick of the part: what track_render does once for all of its voices (voice.c:517-545)
 * and then the voices themselves. The LFO's value is the one from before the tick, as it is there:
 * the tick and the engines' per-part work both come after it. */
static void fluyt_microkorg2_tick(const fluyt_microkorg2_voice_t *voices, uint32_t nvoices)
{
  track_t *t = &trk[0];
  const engine_t *e = ENGINES[t->engine];
  struct fluyt_microkorg2_tick k;
  uint32_t i;
  k.n = CTL;
  k.lfo = mulq15(t->lfo_val, t->lfo_fade);
  k.tune = song.g[G_TUNE] >= 0 ? song.g[G_TUNE] * 16 / 100 : -((-song.g[G_TUNE] * 16 + 99) / 100);
  k.tune_fine = (song.g[G_TUNE] * 16 - k.tune * 100) * 2367 / 16000;
  track_lfo_tick(t);
  if (e->block)                                       /* the engine's per-part work (WHEEL: bars, rotor) */
    e->block(t);
  for (i = 0; i < nvoices; i++)
    if (voices[i].index < NVOICE)
      fluyt_microkorg2_voice(&k, voices[i].index, microkorg2_buf[voices[i].index]);
}

/* fx.c master_out, for one voice: the DC blocker, the limiter and the soft clip, with this voice's
 * own state. A voice on this platform is the whole signal, so this is fx.c's master and not its
 * mix_part - no level and no pan of ours (they are the instrument's). It is the same arithmetic, so
 * one voice rendered here is the same samples as one voice rendered on the drumlogue, once the
 * drumlogue's mix has taken the level out; tools/microkorg2sim.c holds the two to each other. */
static inline int32_t fluyt_microkorg2_master(uint32_t i, int32_t x)
{
  int32_t a;
  x = fluyt_dc_block(x, &microkorg2_dc[i], &microkorg2_dcerr[i]);
  a = x < 0 ? -x : x;
  if (a > microkorg2_lim[i])
    microkorg2_lim[i] += (a - microkorg2_lim[i]) >> 2;
  else if (microkorg2_lim[i] > FLUYT_LIM_T)
    microkorg2_lim[i] -= ((microkorg2_lim[i] - FLUYT_LIM_T) >> 12) + 1;
  if (microkorg2_lim[i] > FLUYT_LIM_T) {
    int32_t g = (int32_t)(((uint32_t)FLUYT_LIM_T << 15) / (uint32_t)microkorg2_lim[i]);   /* < 32768 */
    x = ((x >> 4) * g) >> 11;
  }
  return softclip(x);
}

/* The bridge's block render for the microKORG2. The events first (the ones the control thread left),
 * then the runtime's pitch for every voice of the slice, then the voices it started, then the
 * samples in CTL-sized ticks. */
void fluyt_microkorg2_block(const fluyt_microkorg2_voice_t *voices, uint32_t nvoices, uint32_t frames, uint32_t stride)
{
  float *lane[NVOICE];
  uint32_t i;
  if (!voices || !frames)
    return;                                           /* an empty slice still drains: the events, the
                                                       * rows and the presets are the bridge's, and a
                                                       * block with no voice in it is still a block */
  if (nvoices > NVOICE)
    nvoices = NVOICE;                                 /* the runtime has eight voices, as Felucca has */
  if (!stride)
    stride = 1u;
  fluyt_microkorg2_drain();
  for (i = 0; i < nvoices; i++)                       /* what each voice is playing, before any of them */
    fluyt_microkorg2_follow(&voices[i]);                     /* is started: a fresh voice needs it to land */
  for (i = 0; i < nvoices; i++)
    if (voices[i].fresh)
      fluyt_microkorg2_fresh(&voices[i]);
  for (i = 0; i < nvoices; i++)
    lane[i] = voices[i].lane;
  while (frames) {
    uint32_t n;
    if (!microkorg2_have) {
      fluyt_microkorg2_tick(voices, nvoices);
      microkorg2_have = CTL;
      microkorg2_sent = 0;
    }
    n = frames < microkorg2_have ? frames : microkorg2_have;
    for (i = 0; i < nvoices; i++) {
      uint32_t vi = voices[i].index < NVOICE ? voices[i].index : NVOICE;   /* row NVOICE: silence */
      const int32_t *b = microkorg2_buf[vi] + microkorg2_sent;
      uint32_t f;
      for (f = 0; f < n; f++)
        lane[i][f * stride] = (float)fluyt_microkorg2_master(vi, b[f]) * (1.0f / 32768.0f);
      lane[i] += n * stride;
    }
    microkorg2_sent += n;
    microkorg2_have -= n;
    frames -= n;
  }
}
