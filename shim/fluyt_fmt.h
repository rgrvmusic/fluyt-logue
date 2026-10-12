/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as drumlogue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see LICENSING.md. */
/* The routines of Felucca's own libc.c that the files a unit compiles call by name: the four string
 * helpers params.c's param_format() formats with, and the random source voice.c draws its S&H LFO
 * and its RAND modulation from. Felucca's libc.c itself is not compiled into a unit: the drumlogue
 * links a real libc, and libc.c's memcpy/memset/memcmp are non-static, so they would take the
 * device's own away from every other file. Only what is needed is here, with the names and
 * signatures Felucca's files expect (they are compiled unmodified).
 *
 * One visible difference from the FM-1's display: Felucca's fmt_fix writes ".5" where this writes
 * "0.5" (it has no room for a leading zero where the drumlogue has). The random source is the same
 * xorshift from the same seed, though its sequence is not a thing to reproduce note for note. */
#ifndef FLUYT_FMT_H
#define FLUYT_FMT_H

#include <stdint.h>

static unsigned str_len(const char *s)
{
  unsigned n = 0;
  while (s[n])
    n++;
  return n;
}

static void str_cpy(char *d, const char *s, unsigned max)
{
  unsigned i = 0;
  if (!max)
    return;
  while (i + 1u < max && s[i]) {
    d[i] = s[i];
    i++;
  }
  d[i] = 0;
}

/* a value, with `decimals` digits after the point. Felucca's own display has no room for a leading
 * zero and writes ".5"; the drumlogue's has, and writes "0.5". */
static void fmt_fix(char *b, int32_t v, int decimals)
{
  static const uint32_t POW[4] = {1u, 10u, 100u, 1000u};
  uint32_t p = POW[decimals > 3 ? 3 : decimals], u = (uint32_t)(v < 0 ? -v : v), w;
  unsigned n = 0, k;
  char t[12];
  if (v < 0)
    b[n++] = '-';
  for (w = decimals ? u / p : u, k = 0; w && k < 11u; w /= 10u)
    t[k++] = (char)('0' + w % 10u);
  do
    b[n++] = k ? t[--k] : '0';
  while (k);
  if (decimals) {
    uint32_t f = u % p;
    b[n++] = '.';
    for (k = (unsigned)decimals; k--;)
      b[n++] = (char)('0' + (f / (p /= 10u)) % 10u);
  }
  b[n] = 0;
}

static void fmt_int(char *b, int32_t v)
{
  fmt_fix(b, v, 0);
}

static uint32_t rng_state = 0x1234567u;
static uint32_t rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

#endif /* FLUYT_FMT_H */
