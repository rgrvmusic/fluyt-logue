/* SPDX-License-Identifier: GPL-3.0-only
 * Fluyt - Felucca's engines as logue units.
 * Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
 * Felucca is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only);
 * see docs/LICENSING.md. */
/* The unit header a Fluyt microKORG2 unit carries, built from its rows.def. The twin of
 * shim/fluyt_header.c, and the same idea: the panel has exactly one source - the unit's rows.def,
 * which tools/gen_rows.c wrote out of Felucca's own descriptors - and this file only names the
 * device's types for it.
 *
 * What differs from the drumlogue's header, and nothing else does:
 *   - the target is the microKORG2's osc module (k_unit_module_osc), not its synth one: a unit that
 *     sits inside the instrument's own voice - behind its EG, its filter and its amp - is an
 *     oscillator to it;
 *   - there are 13 rows rather than 24 (UNIT_MAX_PARAM_COUNT; the osc ABI's own name for the same
 *     number is UNIT_OSC_MAX_PARAM_COUNT), which is three pages of four and one row over. The rows
 *     that do not fit are not on this panel at all: the envelope group is the instrument's own EG
 *     (shim/fluyt_microkorg2.c holds Felucca's open), and the voice mode has no meaning when the
 *     instrument's allocator is what plays the voices. See tools/gen_rows.c for the walk and
 *     README.md for the whole of the difference;
 *   - the name is 8 characters at most (UNIT_NAME_LEN), which is what "F-ANALOG" and its eight
 *     siblings are for.
 *
 * A row is k_unit_param_type_strings, so the panel shows Felucca's own word for the value ("SINE",
 * "-12.0 dB", "2.00 s") rather than a number the device would have to be told the unit of. The
 * mapping to the other two types is kept for the same reason it is in the drumlogue's header: a mix
 * row of Felucca's is not on a Fluyt panel (see README.md), and this is what a row would say if one
 * ever came back.
 *
 * Nothing of Felucca is here, and nothing of the engine: this file is the same for all nine units,
 * and the four things that differ come from config.mk - the name, the id, how many presets the
 * engine has, and which rows.def is this unit's. */

#include <stdint.h>

#include "unit.h"
#include "runtime.h"             /* microKORG2: UNIT_TARGET_PLATFORM and UNIT_API_VERSION live here */

#include "fluyt_rows.h"          /* FLUYT_T_*: what a row was told to be */

#ifndef FLUYT_ROWS
#error "FLUYT_ROWS must name the unit's rows.def (see a unit's config.mk)"
#endif
#ifndef FLUYT_UNIT_NAME
#error "FLUYT_UNIT_NAME must be the name the device shows (see a unit's config.mk)"
#endif
#ifndef FLUYT_UNIT_ID
#error "FLUYT_UNIT_ID must be this unit's id in the Fluyt family (see a unit's config.mk)"
#endif
#ifndef FLUYT_UNIT_PRESETS
#error "FLUYT_UNIT_PRESETS must be how many presets the engine has (see a unit's config.mk)"
#endif

/* a row's type, as the device knows it. The paste is deliberate: FLUYT_T_STR becomes
 * FLUYT_TYPE_FLUYT_T_STR, the name of the macro below that is the device's own type. (A row type
 * is not a macro, so there is nothing for the preprocessor to expand before the paste.) */
#define FLUYT_TYPE_FLUYT_T_STR   k_unit_param_type_strings
#define FLUYT_TYPE_FLUYT_T_PCT   k_unit_param_type_percent
#define FLUYT_TYPE_FLUYT_T_ONOFF k_unit_param_type_onoff
#define FLUYT_UNIT_TYPE(t)       FLUYT_TYPE_##t

/* the panel's length, and a check that it is a panel the device can hold (a unit declares at most
 * UNIT_MAX_PARAM_COUNT rows; gen_rows.c is bounded by the same number). tools/check_rows_microkorg2.c
 * compiles this file and the bridge together and both count the same rows.def: the first of the two
 * to be included counts it. */
#ifndef FLUYT_PANEL_COUNTED
#define FLUYT_PANEL_COUNTED 1
enum {
  FLUYT_NROWS = 0
#define FLUYT_ROW(p, l, mn, mx, df, t, f) + 1
#include FLUYT_ROWS
#undef FLUYT_ROW
};
#endif

_Static_assert(FLUYT_NROWS > 0, "a unit's rows.def is empty");
_Static_assert(FLUYT_NROWS <= UNIT_MAX_PARAM_COUNT, "a unit declares at most 13 rows");

/* every label has to fit what the device shows (UNIT_PARAM_NAME_LEN characters and a terminator),
 * or the panel would show a name the row does not have. Eight characters here, against the
 * drumlogue's eight as well: Felucca's own labels are five at their longest ("BODY"), which is why
 * nothing has to be shortened for either panel (tools/gen_rows.c checks it). */
#define FLUYT_ROW(p, l, mn, mx, df, t, f) \
  _Static_assert(sizeof(l) <= UNIT_PARAM_NAME_LEN + 1, "row label " #l " is too long for the device");
#include FLUYT_ROWS
#undef FLUYT_ROW

_Static_assert(sizeof(FLUYT_UNIT_NAME) <= UNIT_NAME_LEN + 1, "unit name is too long for the device");

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x72677276U,               /* "rgrv", the developer id the units in rfx/logue use */
    .unit_id = FLUYT_UNIT_ID,
    .version = 0x00010000U,              /* 1.0.0 */
    .name = FLUYT_UNIT_NAME,             /* 8 characters at most: UNIT_NAME_LEN */
    .num_presets = FLUYT_UNIT_PRESETS,
    .num_params = FLUYT_NROWS,           /* 13 at most: the slots past it stay blank */
    .params = {
        /* what a row's own numbers are: the panel moves a parameter through the range Felucca gave
         * it, and starts where Felucca starts it, so a row that has not been touched is the sound's
         * own value - see README.md.
         *
         * A row is declared from zero up, though: the value of a row the unit formats itself is
         * held by the runtime in a byte, and a row declared with a negative minimum is one it
         * cannot hold - the value comes back wrapped (-1 as 255), which reads as the row's maximum,
         * so the whole lower half of a bipolar row would show its top ("+98%" for Felucca's -64..63
         * percent rows) and its own negative default would show the same. Every row of every unit
         * whose text the unit supplies starts at zero for that reason. So min here is 0, the span
         * is Felucca's own width, and the row's zero - where Felucca's value is 0 - is the row's
         * center; rows.def keeps Felucca's numbers, and the bridge (fluyt_engine.c) is what turns a
         * row value into Felucca's and back, so the panel still shows "-100%" .. "0%" .. "+98%".
         *
         * A slot past num_params is the SDK's own blank descriptor, which is what a zero-filled
         * entry of this array is: {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}. The dummy
         * oscillator fills all thirteen by hand; a zero-initialised tail is the same bytes. */
#define FLUYT_ROW(p, l, mn, mx, df, t, f)                                            \
  { 0, (mx) - (mn), -(mn), (df) - (mn), FLUYT_UNIT_TYPE(t), (f),                     \
    k_unit_param_frac_mode_decimal, 0, { l } },
#include FLUYT_ROWS
#undef FLUYT_ROW
    }};
