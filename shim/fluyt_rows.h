/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as drumlogue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* The panel rows, one FLUYT_ROW per row, expanded twice from a unit's rows.def: by the bridge (for
 * the parameter each row moves) and by the unit's header.c (for what the drumlogue is told about
 * it). rows.def is generated from Felucca's own descriptors by tools/gen_rows.c, so the labels,
 * ranges and defaults are Felucca's, never invented; tools/check_rows.c holds the two to each
 * other. */
#ifndef FLUYT_ROWS_H
#define FLUYT_ROWS_H

#include <stdint.h>

/* What the drumlogue is told a row is; header.c maps it onto k_unit_param_type_*.
 *
 * Almost every row of Felucca's is an index into one of its tables - 0..127 into LEVEL_Q12,
 * TIME_MS_X10, LFO_HZ_X100, -64..63 of a percent, a value of an enum - so almost every row is
 * FLUYT_T_STR: the device asks unit_get_param_str_value() and is shown Felucca's own
 * param_format() of the value ("72", "-32%", "12ms", "POLY"), which is the same reading the FM-1's
 * display gives. A type of its own is for the rows whose number really is the unit: */
enum {
  FLUYT_T_STR,      /* Felucca's own formatting of the value (F_PCT, F_BIPCT, F_TIME, F_DB,
                       F_LFOHZ, F_CUTOFF, F_SEMI, F_ENUM and F_INT alike) */
  FLUYT_T_PCT,      /* 0..100 is a percentage: the device shows its own percent */
  FLUYT_T_ONOFF,    /* 0/1 is a switch (F_ONOFF): the device shows OFF/ON */
};

/* FLUYT_ROW(param, label, min, max, default, type, frac)
 *   param    the P_* of Felucca's core.h for track parameters, P_E0+k for the engine's own
 *   label    Felucca's own label (TP[] / the engine's edit[]), at most 12 characters
 *   min/max/default  as Felucca's descriptor has them: Felucca's own numbers, which is what
 *            tools/check_rows.c holds this file to. The device is told the same row from zero up
 *            (see fluyt_header.c), and the bridge translates between the two.
 *   frac     decimals for a numeric display (0 for every row today: FLUYT_T_STR parses its own) */

#endif /* FLUYT_ROWS_H */
