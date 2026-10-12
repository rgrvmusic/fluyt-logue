#!/bin/bash
# SPDX-License-Identifier: MIT
# Fluyt - Felucca's engines as logue units.
# Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
# Another check script in the shape of the ones written for Regroove | RGRV's own logue units; see
# docs/LICENSING.md. What it builds - and the tools it builds - include Felucca, which is
# GPL-3.0-only:
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments.
#
# The host checks, for all nine microKORG2 units: no container, no device, no cross compiler. The
# twin of tools/hostcheck.sh, and the two are deliberately the same script twice: they check the two
# platforms' own panels and the two platforms' own unit files, and they share the bridge, Felucca and
# the engine-level tool (tools/hostsim.c, which hostcheck.sh runs and this one does not need to: it
# has nothing of a platform in it).
#
#   tools/hostcheck-microkorg2.sh                          every unit
#   tools/hostcheck-microkorg2.sh fluyt_fm6_microkorg2
#
# Two programs are built per unit, out of the unit's own sources and the unit's own config.mk (so
# there is never a second copy of a -D to disagree with the first):
#
#   check_rows_microkorg2   the panel, held to Felucca's own parameter descriptors: the 13 slots, no
#                           parameter twice, no envelope row, Felucca's own text at both ends of
#                           every row, the declaration the device reads counted from zero, the
#                           envelope the bridge holds open (every preset of the engine included),
#                           and the engine's four HOME knobs as the first four rows. Printed the way
#                           the device will show it.
#   microkorg2sim           the sound: the microKORG2 path's block against the drumlogue's own
#                           track_render, sampled at the same ticks. Fails if a lane, a block size or
#                           a sample has drifted. Built with -fsanitize=address,undefined.
#
# What is not built here is shim/fluyt_microkorg2_unit.c: it includes unit_osc.h, whose helpers are
# the device's own (NEON), so a host compiler can have neither. The osc ABI itself is covered by the
# container build (build-microkorg2.sh), and the device is Gerard's.
#
# Both tools are built the way the unit is: one engine, compiled into the bridge's single translation
# unit exactly as Felucca's own felucca.c compiles its files.

set -e

cd "$(dirname "${BASH_SOURCE[0]}")/.."

LOGUE_SDK_PATH="${LOGUE_SDK_PATH:-$HOME/Projects/logue/logue-sdk}"
MK2_COMMON="$LOGUE_SDK_PATH/platform/microkorg2/common"
PLATFORM_COMMON="$LOGUE_SDK_PATH/platform/common"

if [ ! -f "$PLATFORM_COMMON/unit.h" ] || [ ! -f "$MK2_COMMON/runtime.h" ]; then
    echo "Error: the microKORG2 SDK headers are not at $MK2_COMMON" >&2
    echo "       Set LOGUE_SDK_PATH to your logue-sdk checkout." >&2
    exit 1
fi

UNITS="fluyt_analog_microkorg2 fluyt_trio_microkorg2 fluyt_phase_microkorg2 \
       fluyt_lofi_microkorg2 fluyt_wheel_microkorg2 fluyt_noise_microkorg2 \
       fluyt_formant_microkorg2 fluyt_drum_microkorg2 fluyt_fm6_microkorg2"

if [ $# -gt 0 ]; then
    UNITS="$@"
fi

# The unit's config.mk holds the UDEFS, and they are what make the bridge this unit's: the engine,
# its Felucca number, its panel, and the target the bridge renders for. Reading them out of the file
# rather than repeating them here is the point - a unit whose config.mk is wrong fails the check,
# rather than the check agreeing with a copy of the same mistake.
udefs_of() {
    sed -n 's/^UDEFS[[:space:]]*\(+*\)=[[:space:]]*//p' "$1/config.mk" | tr '\n' ' '
}

# The sanitizers are what turn Felucca's fixed-point arithmetic into something checked rather than
# something trusted, but they need their runtime installed (Fedora: dnf install libasan libubsan).
# Where it is not, the checks still run - only that one layer of them is missing, and it says so
# rather than failing to build.
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
if ! printf 'int main(void){return 0;}' | cc $SAN -x c - -o /dev/null 2>/dev/null; then
    echo "Note: no libasan/libubsan here, so the host checks run without the sanitizers." >&2
    SAN=""
fi

# Felucca's files are each a compilation unit of their own, with more in them than one engine calls;
# the unit's own build says the same in USE_CWARN.
CWARN="-W -Wall -Wextra -Wno-unused-function -Wno-unused-variable"

mkdir -p build
status=0

for unit in $UNITS; do
    [ -d "$unit" ] || { echo "no such unit: $unit" >&2; exit 1; }
    udefs="$(udefs_of "$unit")"
    echo "========================================"
    echo "$unit"
    echo "========================================"
    for tool in check_rows_microkorg2 microkorg2sim; do
        san=""
        [ "$tool" = microkorg2sim ] && san="$SAN"
        # eval, because those UDEFS are written the way make hands them to the shell: the escape in
        # -DFLUYT_UNIT_NAME=\"F-ANALOG\" is what keeps the name one word. A shell that expanded them
        # without interpreting them (an ordinary "$udefs") would pass the backslashes through.
        #
        # -I$unit is where rows.def is (FLUYT_ROWS is a relative include, as it is in the unit's own
        # build), and the two SDK directories are the osc module's own: unit.h, and the platform
        # definitions the module is declared with, from platform/common, and runtime.h - the
        # microKORG2's own (the kMk2 enums, the target, the block context) - from
        # platform/microkorg2/common. unit_osc.h is the SDK's and only shim/fluyt_microkorg2_unit.c
        # may include it.
        eval "cc -O1 $CWARN -o build/$tool.$unit tools/$tool.c \
              -I. -I$unit -Ishim -Ishim/gen -Ifelucca/firmware/src \
              -I'$MK2_COMMON' -I'$PLATFORM_COMMON' \
              $udefs $san -lm"
        if "build/$tool.$unit"; then
            :
        else
            echo "FAILED: $tool ($unit)"
            status=1
        fi
    done
done

echo ""
if [ $status -eq 0 ]; then
    echo "All $(( $(echo $UNITS | wc -w) )) microKORG2 units pass the host checks."
else
    echo "Some host checks FAILED." >&2
fi
exit $status
