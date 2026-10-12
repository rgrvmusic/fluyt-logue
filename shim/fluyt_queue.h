/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as drumlogue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* Notes, rows, presets and bend, from the thread that plays them to the thread that renders.
 * The drumlogue calls a unit's note, gate, parameter and preset entry points from its control
 * thread, and unit_render from the audio thread; Felucca's voices, envelopes and preset load are
 * not writable from two threads at once, so the control thread only ever appends an event here and
 * Render applies them at the head of a block. One producer, one consumer, no locks: the writer
 * stores the event and then the index (RING_PUBLISH, core.h), the reader reads the index first. */
#ifndef FLUYT_QUEUE_H
#define FLUYT_QUEUE_H

#include <stdint.h>

enum {
  FQ_NOTE_ON,
  FQ_NOTE_OFF,
  FQ_GATE_ON,
  FQ_GATE_OFF,
  FQ_ALL_OFF,
  FQ_PARAM,
  FQ_PRESET,
  FQ_BEND,
  FQ_AT,
};

typedef struct {
  int32_t b;                     /* the event's value (velocity, parameter value, bend, ..) */
  uint8_t type, a;               /* .. and what it is (the parameter or the note) */
} fluyt_event_t;

#define FQ_SIZE 256u               /* a burst of notes and a knob's stream; a block drains all of it */

static fluyt_event_t fq[FQ_SIZE];
static volatile uint32_t fq_w, fq_r;

/* the control thread; 0 = the queue was full and the event is dropped (it cannot be: FQ_SIZE is far
 * more than a drumlogue buffer's worth of playing) */
static int fq_push(uint8_t type, uint8_t a, int32_t b)
{
  uint32_t w = fq_w, n = w + 1u < FQ_SIZE ? w + 1u : 0u;
  if (n == fq_r)
    return 0;
  fq[w].b = b;
  fq[w].type = type;
  fq[w].a = a;
  RING_PUBLISH();
  fq_w = n;
  return 1;
}

/* the audio thread */
static int fq_pop(fluyt_event_t *e)
{
  uint32_t r = fq_r;
  if (r == fq_w)
    return 0;
  *e = fq[r];
  fq_r = r + 1u < FQ_SIZE ? r + 1u : 0u;
  return 1;
}

#endif /* FLUYT_QUEUE_H */
