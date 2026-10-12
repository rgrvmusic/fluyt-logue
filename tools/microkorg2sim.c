/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as logue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* A microKORG2 that is a host program: it renders the same case through both of the bridge's
 * renders and holds them to each other.
 *
 * tools/hostsim.c plays a unit the way the drumlogue's runtime does; this plays it the way the
 * microKORG2's does, and what it checks is the one thing a port can get wrong without hearing it:
 * shim/fluyt_microkorg2.c restates the per-voice body of Felucca's track_render (voice.c:546-598), line by
 * line, because on this platform there is one voice per lane rather than eight voices into one part.
 * A restatement is a copy, and a copy drifts. So every case is rendered twice:
 *
 *   1. the drumlogue's way - track_render, four control ticks of it, which is what fluyt_render
 *      renders into the part before its mix. What is compared is what track_render left in the part
 *      buffer: the level, the pan and the drumlogue's master chain are the drumlogue's own, and a
 *      microKORG2 unit has none of them (the instrument's level and pan are the instrument's);
 *   2. the microKORG2's way - a block over the voices the runtime would have started, each into its
 *      own lane, summed back into one buffer.
 *
 * and the samples have to be the same samples, not similar ones: the same arithmetic on the same
 * state is exact in fixed point, so a difference is the restatement.
 *
 * Each pass runs in its own child process (run_pass below), which is the one thing here that wants
 * explaining. Felucca keeps state in file-scope statics and so do some engines on purpose (WHEEL's
 * tonewheels free-run, FM6 keeps an LFO and its operators per part), and that state outlives a
 * render. Two renders in one process therefore cannot both start from the state a case starts from,
 * and the comparison would be between two different states, which would hide exactly the drift it is
 * looking for. A child inherits its parent's memory whole, and the parent never renders behind a
 * pair of children: the two passes of a case are forked one after the other, so they see the same
 * state, whatever the parent has done before them.
 *
 * The rest is the platform's own: the lanes (the geometry of the SDK's README, sentinels where the
 * unit must not write), silence where nothing was started, the block sizes the runtime may use (it is
 * not obliged to hand whole control ticks), and every preset - played, then held to the two rules
 * this platform adds (the envelope stays open, and nothing leaves +-1.0).
 *
 * Built with -fsanitize=address,undefined by tools/hostcheck-microkorg2.sh, like its drumlogue twin. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../shim/fluyt_engine.c"

#define SR 48000u
#define TICK CTL                          /* 32: one control tick, which is what track_render renders */
#define PASS_TICKS 4u
#define PASS_FRAMES (PASS_TICKS * TICK)   /* the frames a case is held to (a shape sums to this) */

/* ------------------------------------------------------------- one pass --- */

/* What a pass renders, and what it sends back. The parent fills the case in before the child starts:
 * the notes, the shape of the calls, and - for the microKORG2's pass - the voices the drumlogue's
 * allocator started, which is the one thing the runtime tells a unit about the notes it plays. */
struct pass {
  uint8_t notes[NVOICE];              /* the case: the notes, in the order they were played */
  uint8_t vels[NVOICE];
  uint32_t nnotes;
  uint8_t have[NVOICE], note[NVOICE]; /* what the drumlogue's allocator did with them */
  int32_t part[PASS_FRAMES];          /* the drumlogue's own: track_render's output */
  int32_t row[NVOICE][PASS_FRAMES];   /* the microKORG2's own: the voices before the master */
  float lane[NVOICE][PASS_FRAMES];    /*             .. and what it wrote, one voice per row */
  float lanes[NVOICE * PASS_FRAMES];  /*             .. or at a stride, in one block (see lane_of) */
  uint32_t shape[8], nshape;          /* the calls it makes: shape[i] frames each */
  uint32_t stride;                    /* outputStride: 1, or 4 as the layout's voice groups have it */
  uint32_t frames;                    /* what the pass rendered, in all */
  int failed;                         /* the pass's own complaint, or 0 */
};

/* The case's note-ons, from the state the child inherited. Both passes play them, in this order: a
 * generator the whole part shares (FORMNT draws its breath noise per note-on, mod.c draws RAND into
 * t->m_rnd) is drawn from once per note-on, so the two passes have to draw in the same order for
 * their states to be the same ones. */
static void case_notes(const struct pass *p)
{
  uint32_t i;
  fluyt_init((float)SR);
  for (i = 0; i < p->nnotes; i++)
    fluyt_note_on(p->notes[i], p->vels[i]);
}

/* The drumlogue's render of the case: Felucca's own track_render, one control tick at a time, with
 * the envelope held open and the voice mode forced to POLY - this platform's panel has no envelope
 * rows and no voice mode, and shim/fluyt_microkorg2.c is always poly (the instrument's allocator decides,
 * which is the same decision a poly drumlogue makes). */
static void pass_reference(struct pass *p)
{
  uint32_t i, t;
  case_notes(p);
  fluyt_microkorg2_open_env();
  trk[0].p[P_VOICE] = V_POLY;
  fluyt_drain();
  /* The voice map is read here, before the ticks: it is what the runtime would hand the unit (a
   * trigger bit per voice), not what is left of it afterwards. An allocator at its cap steals a voice
   * during the drain, and a stolen voice is a voice the runtime started and the unit is told about -
   * so it is taken here rather than after the render, where a note that was replaced by the next one
   * would be dropped from the comparison (Felucca's caps are the engines' own: FORMNT plays four
   * voices, FM6 six, and a case may overshoot either). */
  for (i = 0; i < NVOICE; i++) {
    p->have[i] = (uint8_t)(trk[0].v[i].active ? 1u : 0u);
    p->note[i] = trk[0].v[i].note;
  }
  for (t = 0; t < PASS_TICKS; t++)
    track_render(&trk[0], p->part + t * TICK, TICK);
}

/* The geometry the runtime describes (the microKORG2 SDK's README, "Oscillator Output"): the lane of
 * voice v starts at bufferOffset + (v >> 2) * (frames << 2) + (v & 3), and eight voices take two runs
 * of four. A unit never computes this itself - GetBufferOffset of unit_osc.h does, and
 * shim/fluyt_microkorg2_unit.c is the only file that may include it - so the tool says it out loud and holds
 * the unit to it. */
static uint32_t lane_of(uint32_t voice, uint32_t frames)
{
  return (voice >> 2) * (frames << 2) + (voice & 3);
}

/* The microKORG2's render of the same case, in the same order (see case_notes): the voices the
 * drumlogue's allocator started, each started in the block it starts in, by its trigger bit.
 *
 * The slice is the voices in index order, which is the order the runtime hands them in
 * (voiceOffset .. voiceOffset + voiceLimit) and the order track_render walks its own in. That order
 * is not cosmetic: an engine may share state across the voices of a part and draw from it as it
 * renders them (FORMNT's breath noise is one xorshift for the whole engine), so a unit that renders
 * the same voices in another order draws a different sequence from the same generator. */
static void pass_microkorg2(struct pass *p)
{
  fluyt_microkorg2_voice_t slice[NVOICE];
  uint32_t i, k = 0, c, off = 0;
  case_notes(p);
  for (i = 0; i < NVOICE; i++)
    if (p->have[i]) {
      slice[k].pitch = (float)p->note[i];
      slice[k].index = (uint8_t)i;
      slice[k].fresh = 1;                 /* the runtime's trigger bit: set in the block the voice
                                           * starts in, and in no other - see the loop below */
      k++;
    }
  if (!k) {
    p->failed = 1;                    /* nothing was started: there is no case to compare */
    return;
  }
  for (c = 0; c < p->nshape; c++) {
    uint32_t f = p->shape[c], step = p->stride > 1u ? p->stride : 1u;
    for (i = 0; i < k; i++) {
      uint32_t v = slice[i].index;
      /* at a stride the block is one call (see check_lanes): its lanes lie in the one layout */
      slice[i].lane = p->stride > 1u ? p->lanes + lane_of(v, f) : p->lane[v] + off;
      slice[i].fresh = (uint8_t)(c == 0);  /* every voice of this case starts in the first block, as
                                            * the runtime's trigger bits say it did */
    }
    fluyt_microkorg2_block(slice, k, f, p->stride);
    for (i = 0; i < k; i++) {
      uint32_t v = slice[i].index;
      memcpy(p->row[v] + off, microkorg2_buf[v], f * sizeof(int32_t));   /* the tick's own samples */
    }
    off += f * step;
  }
  p->frames = off;
}

/* One pass, in a child process: the child renders and writes its payload back through a pipe, and
 * the parent's own copy of the payload stays as the parent left it. */
static void run_pass(void (*fn)(struct pass *), struct pass *p)
{
  int fd[2];
  pid_t pid;
  size_t got = 0;
  ssize_t n;
  if (pipe(fd) != 0) {
    perror("pipe");
    exit(2);
  }
  pid = fork();
  if (pid < 0) {
    perror("fork");
    exit(2);
  }
  if (pid == 0) {
    close(fd[0]);
    fn(p);
    if (write(fd[1], p, sizeof *p) != (ssize_t)sizeof *p)
      _exit(3);             /* _exit: the parent's own buffers are not this process's to flush */
    _exit(0);
  }
  close(fd[1]);
  while (got < sizeof *p && (n = read(fd[0], (char *)p + got, sizeof *p - got)) > 0)
    got += (size_t)n;
  close(fd[0]);
  if (waitpid(pid, NULL, 0) < 0 || got != sizeof *p) {
    printf("FAIL: a pass did not come back (child %d)\n", (int)pid);
    exit(2);
  }
}

static uint32_t bad;

static double peak_of(const int32_t *b, uint32_t n)
{
  uint32_t i;
  double p = 0.0;
  for (i = 0; i < n; i++)
    if (fabs((double)b[i]) > p)
      p = fabs((double)b[i]);
  return p / 32768.0;
}

static void case_set(struct pass *p, const uint8_t *notes, const uint8_t *vels, uint32_t n)
{
  memset(p, 0, sizeof *p);
  memcpy(p->notes, notes, n * sizeof *notes);
  memcpy(p->vels, vels, n * sizeof *vels);
  p->nnotes = n;
  p->stride = 1u;
}

/* the voices this engine plays at once: its own cap (engine_t.poly, 0 = all eight) */
static uint32_t engine_cap(void)
{
  uint32_t c = ENGINES[FLUYT_ENGINE_INDEX]->poly;
  return c && c < NVOICE ? c : NVOICE;
}

/* one case, rendered both ways and held to each other. The lanes are summed: on the drumlogue the
 * eight voices are one part buffer, and a per-voice render that adds up to it is the same render.
 *
 * A case with more notes than the engine has voices is skipped, and named as skipped: there the
 * drumlogue's allocator steals a voice mid-phrase, which is not a thing this platform has (the
 * instrument decides, and a unit is never told a voice was taken back), so the two renders are of
 * two different sounds - one of them with a stolen voice gliding to its new note. Felucca's caps are
 * the engines' own (FORMNT four voices, FM6 six, DRUM one per lane) and the FM-1 has eight. */
static void check_case(const char *what, const uint8_t *notes, const uint8_t *vels, uint32_t n)
{
  static struct pass ref, mk;
  uint32_t i, f, nv = 0;
  if (n > engine_cap()) {
    printf("  %-16s skipped: this engine plays %u voices at once, and the case has %u notes\n", what,
           (unsigned)engine_cap(), (unsigned)n);
    return;
  }
  case_set(&ref, notes, vels, n);
  ref.shape[0] = ref.shape[1] = ref.shape[2] = ref.shape[3] = TICK;
  ref.nshape = PASS_TICKS;            /* a tick at a time: the runtime may hand any number of frames */
  run_pass(pass_reference, &ref);
  for (i = 0; i < NVOICE; i++)
    nv += ref.have[i];
  if (!nv) {
    printf("  FAIL: %s: the drumlogue's allocator started no voice\n", what);
    bad++;
    return;
  }
  mk = ref;                           /* the case, and the voice map the microKORG2 is then handed */
  memset(mk.part, 0, sizeof mk.part);
  memset(mk.row, 0, sizeof mk.row);
  memset(mk.lane, 0, sizeof mk.lane);
  run_pass(pass_microkorg2, &mk);
  if (mk.failed) {
    printf("  FAIL: %s: the microKORG2's pass started no voice\n", what);
    bad++;
    return;
  }
  for (f = 0; f < PASS_FRAMES; f++) {
    int32_t sum = 0;
    for (i = 0; i < NVOICE; i++)
      if (ref.have[i])
        sum += mk.row[i][f];
    if (sum != ref.part[f]) {
      printf("  FAIL: %s: frame %u: %d against the drumlogue's %d\n", what, (unsigned)f, (int)sum,
             (int)ref.part[f]);
      bad++;
      return;
    }
  }
  printf("  %-16s %u voice%s, peak %5.1f%% (raw)  identical over %u ticks\n", what, (unsigned)nv,
         nv == 1u ? " " : "s", 100.0 * peak_of(ref.part, PASS_FRAMES), (unsigned)PASS_TICKS);
}

/* ---- the lanes ---- */

/* Every sample of every voice in its own place, and nothing written anywhere else. The reference is
 * the same case at stride 1 (a stride is a layout, not a sound: check_case holds the samples), and
 * the block is filled with a value the render cannot produce - the soft clip bounds a lane at +-1.0 -
 * so a write outside a lane is a write on a sentinel. */
static void check_lanes(void)
{
  static const uint8_t notes[4] = {60u, 64u, 67u, 71u};
  static const uint8_t vels[4] = {100u, 100u, 100u, 100u};
  static struct pass plain, laid, mk;
  uint32_t i, f;

  case_set(&plain, notes, vels, 4u);
  plain.shape[0] = TICK;              /* one tick is enough for a layout */
  plain.nshape = 1u;
  run_pass(pass_reference, &plain);
  for (i = 0; i < NVOICE; i++) {
    uint32_t k, known = 0;
    if (!plain.have[i])
      continue;
    for (k = 0; k < 4u; k++)          /* the four notes went where they went: no assumption here
                                       * (DRUM plays them as lanes, and a kit has fewer of those), but
                                       * every voice that started is holding one of them */
      if (plain.note[i] == notes[k])
        known = 1u;
    if (!known) {
      printf("  FAIL: lane geometry: voice %u holds note %u, which the case did not play\n",
             (unsigned)i, plain.note[i]);
      bad++;
      return;
    }
  }

  laid = plain;
  memset(laid.row, 0, sizeof laid.row);
  laid.stride = 4u;                   /* the layout of eight voices: two runs of four */
  laid.shape[0] = TICK;
  laid.nshape = 1u;
  for (f = 0; f < NVOICE * PASS_FRAMES; f++)
    laid.lanes[f] = -2.0f;
  run_pass(pass_microkorg2, &laid);
  if (laid.failed) {
    printf("  FAIL: lane geometry: the laid-out pass started no voice\n");
    bad++;
    return;
  }

  mk = plain;                         /* the same case, one voice per row, to hold each lane to */
  mk.shape[0] = TICK;
  mk.nshape = 1u;
  memset(mk.row, 0, sizeof mk.row);
  memset(mk.lane, 0, sizeof mk.lane);
  run_pass(pass_microkorg2, &mk);
  if (mk.failed) {
    printf("  FAIL: lane geometry: the reference pass started no voice\n");
    bad++;
    return;
  }
  for (i = 0; i < NVOICE; i++) {
    if (!plain.have[i])
      continue;
    for (f = 0; f < TICK; f++) {
      float got = laid.lanes[lane_of(i, TICK) + f * 4u];
      if (got != mk.lane[i][f]) {
        printf("  FAIL: voice %u frame %u is %g, its stride-1 render says %g\n", (unsigned)i,
               (unsigned)f, (double)got, (double)mk.lane[i][f]);
        bad++;
        return;
      }
    }
  }
  for (f = 0; f < NVOICE * PASS_FRAMES; f++) {   /* and nothing else was written */
    uint32_t j, w = 0;
    if (laid.lanes[f] == -2.0f)
      continue;
    for (j = 0; j < NVOICE && !w; j++) {
      uint32_t base;
      if (!plain.have[j])
        continue;
      base = lane_of(j, TICK);
      if (f >= base && f < base + TICK * 4u && ((f - base) % 4u) == 0u)
        w = 1u;
    }
    if (!w) {
      printf("  FAIL: index %u of the block is in no voice's lane\n", (unsigned)f);
      bad++;
      return;
    }
  }
  printf("  %-16s 8 voices, stride 4: every lane written, nothing else\n", "lane geometry");
}

/* ---- silence ---- */

/* Nothing started: a lane whose trigger bit never went high is digital silence, and stays it. The
 * runtime does not clear a lane itself, so that is a unit's job. Plus an empty block, which the
 * runtime may hand: a block with no voice in it is still a block, and the events still arrive in it. */
static void check_silence(void)
{
  static struct pass p;
  fluyt_microkorg2_voice_t none;
  uint32_t f;

  case_set(&p, (const uint8_t[]){60u}, (const uint8_t[]){100u}, 1u);
  p.shape[0] = 64u;
  p.nshape = 1u;
  run_pass(pass_reference, &p);
  memset(p.row, 0, sizeof p.row);
  memset(p.lane, 0, sizeof p.lane);
  run_pass(pass_microkorg2, &p);
  for (f = 0; f < 64u; f++)           /* voice 1 was never started: its lane is Felucca's own out */
    if (p.lane[1][f] != 0.0f) {
      printf("  FAIL: an untriggered voice wrote %g at frame %u\n", (double)p.lane[1][f],
             (unsigned)f);
      bad++;
      return;
    }
  if (peak_of(p.row[0], 64u) <= 0.0) {
    printf("  FAIL: the triggered voice is silent\n");
    bad++;
    return;
  }
  memset(&none, 0, sizeof none);
  none.pitch = 60.0f;
  none.index = 0u;
  fluyt_microkorg2_block(&none, 0u, TICK, 1u);            /* a block with no voice in it */
  fluyt_load_preset(0u);
  fluyt_microkorg2_block(&none, 0u, 64u, 1u);             /* and the events still land in one */
  printf("  %-16s one lane silent, the other not; an empty block is harmless\n", "silence");
}

/* ---- block sizes ---- */

/* The runtime's frames_per_buffer is its own business, and Felucca ticks once per 32 samples, so a
 * block that is not a whole tick carries over: two calls of 32 frames are one call of 64, sample for
 * sample, and an odd size neither loses nor repeats anything. */
static void check_blocks(void)
{
  static const uint32_t ODD[] = {1u, 7u, 13u, 33u, 74u, 128u};
  static struct pass one, two, odd;
  uint32_t i, s, f;

  case_set(&one, (const uint8_t[]){60u, 61u}, (const uint8_t[]){100u, 100u}, 2u);
  one.shape[0] = 64u;
  one.nshape = 1u;
  run_pass(pass_reference, &one);
  memset(one.row, 0, sizeof one.row);
  memset(one.lane, 0, sizeof one.lane);
  run_pass(pass_microkorg2, &one);

  two = one;
  memset(two.row, 0, sizeof two.row);
  memset(two.lane, 0, sizeof two.lane);
  two.shape[0] = two.shape[1] = 32u;
  two.nshape = 2u;
  run_pass(pass_microkorg2, &two);

  for (i = 0; i < NVOICE; i++)
    for (f = 0; f < 64u; f++)
      if (one.lane[i][f] != two.lane[i][f]) {
        printf("  FAIL: voice %u frame %u: one block of 64 is not two of 32 (%g against %g)\n",
               (unsigned)i, (unsigned)f, (double)one.lane[i][f], (double)two.lane[i][f]);
        bad++;
        return;
      }
  for (s = 0; s < sizeof ODD / sizeof ODD[0]; s++) {
    odd = one;
    memset(odd.row, 0, sizeof odd.row);
    memset(odd.lane, 0, sizeof odd.lane);
    odd.shape[0] = ODD[s];
    odd.nshape = 1u;
    run_pass(pass_microkorg2, &odd);
    for (i = 0; i < NVOICE; i++)
      for (f = 0; f < ODD[s]; f++)
        if (!isfinite(odd.lane[i][f]) || odd.lane[i][f] > 1.0f || odd.lane[i][f] < -1.0f) {
          printf("  FAIL: %u frames left %g in lane %u\n", (unsigned)ODD[s],
                 (double)odd.lane[i][f], (unsigned)i);
          bad++;
          return;
        }
  }
  printf("  %-16s one block of 64 is two of 32; 1, 7, 13, 33, 74 and 128 frames are all sane\n",
         "block sizes");
}

/* ---- every preset ---- */

/* The presets, played the way the instrument plays them: the load, then a chord on the lanes. Two
 * rules of this platform are checked here rather than in a comment - the envelope is open again after
 * a load (a preset may not close it: the instrument's EG is the envelope), and nothing leaves +-1.0,
 * whatever the preset's level (the master chain's soft clip bounds a lane, as it bounds the
 * drumlogue's out). */
#define PRESET_BLOCKS 32u
#define PRESET_FRAMES (PRESET_BLOCKS * 64u)

static void check_presets(void)
{
  static const uint8_t notes[4] = {60u, 64u, 67u, 52u};
  static float lane[4][PRESET_FRAMES];
  uint32_t np = fluyt_num_presets(), i, f, b, k;
  double loudest = 0.0, quietest = 1.0;
  uint32_t silent = 0, ugly = 0;

  for (i = 0; i < np; i++) {
    fluyt_microkorg2_voice_t slice[4];
    double p = 0.0;
    fluyt_load_preset((uint8_t)i);
    fluyt_init((float)SR);            /* the load is an event: it lands in the first block below */
    for (k = 0; k < 4u; k++) {
      fluyt_note_on(notes[k], 100u);
      slice[k].lane = lane[k];
      slice[k].pitch = (float)notes[k];
      slice[k].index = (uint8_t)k;
      slice[k].fresh = 1;
    }
    for (b = 0; b < PRESET_BLOCKS; b++)
      fluyt_microkorg2_block(slice, 4u, 64u, 1u);
    if (trk[0].p[P_ATK] || trk[0].p[P_DEC] || trk[0].p[P_SUS] != 127 || trk[0].p[P_REL] != 127) {
      printf("    FAIL: preset %u closed the envelope (ATK %d DEC %d SUS %d REL %d)\n", (unsigned)i,
             trk[0].p[P_ATK], trk[0].p[P_DEC], trk[0].p[P_SUS], trk[0].p[P_REL]);
      bad++;
    }
    for (k = 0; k < 4u; k++) {
      for (f = 0; f < PRESET_FRAMES; f++) {
        float x = lane[k][f];
        if (!isfinite(x) || x > 1.0f || x < -1.0f) {
          ugly++;
          continue;
        }
        if (fabs((double)x) > p)
          p = fabs((double)x);
      }
      fluyt_note_off(notes[k]);
    }
    if (p == 0.0)
      silent++;
    else {
      if (p > loudest)
        loudest = p;
      if (p < quietest)
        quietest = p;
    }
  }
  if (ugly) {
    printf("  FAIL: %u samples outside +-1.0\n", (unsigned)ugly);
    bad++;
  }
  if (silent == np) {
    printf("  FAIL: every preset is silent\n");
    bad++;
  }
  printf("  %-16s %u presets, no envelope closed, lanes peaked between %.3f and %.3f%s\n", "presets",
         (unsigned)np, quietest, loudest, silent ? " (some silent)" : "");
}

int main(void)
{
  static const uint8_t chord[3] = {60u, 64u, 67u};
  static const uint8_t note1[1] = {60u};
  static const uint8_t note4[4] = {48u, 55u, 60u, 64u};
  static const uint8_t note5[5] = {36u, 43u, 48u, 55u, 60u};
  static const uint8_t vel1[1] = {100u};
  static const uint8_t vel2[2] = {100u, 100u};
  static const uint8_t vel_soft[1] = {20u};
  static const uint8_t vel_hard[1] = {127u};

  printf("%s: %u rows, %u presets, %u Hz\n", FLUYT_UNIT_NAME, (unsigned)fluyt_num_params(),
         (unsigned)fluyt_num_presets(), (unsigned)SR);
  if (!fluyt_num_presets() || !fluyt_num_params()) {
    printf("FAIL: the unit has no panel or no preset\n");
    return 1;
  }
  printf("  (a case's peak is of the part before the drumlogue's master chain - what the two renders\n"
         "   are held to, and what a lane of this unit is - so above 100%% is a sum of voices, not a\n"
         "   clipping one: the unit's own limiter is what a lane gets instead)\n");

  check_case("one note", note1, vel1, 1u);
  check_case("a fifth", (const uint8_t[]){48u, 55u}, vel2, 2u);
  check_case("a chord", chord, (const uint8_t[]){100u, 100u, 100u}, 3u);
  check_case("four notes", note4, (const uint8_t[]){100u, 100u, 100u, 100u}, 4u);
  check_case("a low fifth", (const uint8_t[]){36u, 43u}, vel2, 2u);
  check_case("five notes", note5, (const uint8_t[]){100u, 100u, 100u, 100u, 100u}, 5u);
  check_case("soft velocity", note1, vel_soft, 1u);
  check_case("accent", note1, vel_hard, 1u);
  check_lanes();
  check_silence();
  check_blocks();
  check_presets();

  printf("%s: %s\n", FLUYT_UNIT_NAME, bad ? "FAILED" : "ok");
  return bad ? 1 : 0;
}
