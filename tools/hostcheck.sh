#!/bin/bash
# SPDX-License-Identifier: MIT
# Fluyt - Felucca's engines as logue units.
# Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
# Another build script in the shape of the ones written for Regroove | RGRV's own logue units; see
# docs/LICENSING.md. What it builds - and the tools it builds - include Felucca, which is
# GPL-3.0-only:
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments.
#
# The host checks, for all nine units: no container, no device, no cross compiler.
#
#   tools/hostcheck.sh                 every unit
#   tools/hostcheck.sh fluyt_fm6_drumlogue
#
# Two programs are built per unit, out of the unit's own sources and the unit's own config.mk (so
# there is never a second copy of a -D to disagree with the first):
#
#   check_rows   the panel, held to Felucca's own parameter descriptors, and printed the way the
#                device will show it. Fails if a label, range or default has drifted, or if a
#                parameter is on the panel twice.
#   hostsim      a drumlogue that is a host program: it plays every preset and looks at the samples
#                (silence, NaN, out of range), at the block sizes the runtime may ask for, and at
#                the panel's own event path. Built with -fsanitize=address,undefined.
#
# Both are built the way the unit is: one engine, compiled into the bridge's single translation unit
# exactly as Felucca's own felucca.c compiles its files.

set -e

cd "$(dirname "${BASH_SOURCE[0]}")/.."

LOGUE_SDK_PATH="${LOGUE_SDK_PATH:-$HOME/Projects/logue/logue-sdk}"
COMMON_PATH="$LOGUE_SDK_PATH/platform/drumlogue/common"

if [ ! -f "$COMMON_PATH/unit.h" ]; then
    echo "Error: the drumlogue SDK headers are not at $COMMON_PATH" >&2
    echo "       Set LOGUE_SDK_PATH to your logue-sdk checkout." >&2
    exit 1
fi

UNITS="fluyt_analog_drumlogue fluyt_trio_drumlogue fluyt_phase_drumlogue \
       fluyt_lofi_drumlogue fluyt_wheel_drumlogue fluyt_noise_drumlogue \
       fluyt_formant_drumlogue fluyt_drum_drumlogue fluyt_fm6_drumlogue"

if [ $# -gt 0 ]; then
    UNITS="$@"
fi

# The unit's config.mk holds the UDEFS, and they are what make the bridge this unit's: the engine,
# its Felucca number, and its panel. Reading them out of the file rather than repeating them here is
# the point - a unit whose config.mk is wrong fails the check, rather than the check agreeing with a
# copy of the same mistake.
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
    for tool in check_rows hostsim; do
        san=""
        [ "$tool" = hostsim ] && san="$SAN"
        # eval, because those UDEFS are written the way make hands them to the shell: the escape in
        # -DFLUYT_UNIT_NAME=\"FLUYT\ ANALOG\" is what keeps the name one word. A shell that expanded
        # them without interpreting them (an ordinary "$udefs") would pass the backslashes through
        # and split the name in two.
        eval "cc -O1 $CWARN -o build/$tool.$unit tools/$tool.c \
              -I. -I$unit -Ishim -Ishim/gen -Ifelucca/firmware/src -I'$COMMON_PATH' \
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
    echo "All $(( $(echo $UNITS | wc -w) )) units pass the host checks."
else
    echo "Some host checks FAILED." >&2
fi
exit $status
