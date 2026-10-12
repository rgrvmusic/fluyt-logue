#!/bin/bash
# SPDX-License-Identifier: MIT
# Fluyt - Felucca's engines as logue units.
# Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
# This build script follows the one written for Regroove | RGRV's own logue units (rfx/logue,
# regroovelizer) and is licensed permissively, like it: MIT, see docs/LICENSING.md and LICENSES/MIT.txt.
# The units it builds are GPL-3.0-only, because Felucca is: Felucca is Copyright (C) 2026 Leo
# Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only). See docs/LICENSING.md.
#
# Build every Fluyt unit for the microKORG2 with the logue SDK container, the way build.sh does for
# the drumlogue: one engine per unit, and Felucca read-only throughout.
#
#   ./build-microkorg2.sh                             all nine units
#   ./build-microkorg2.sh fluyt_analog_microkorg2     just the ones named
#   FORCE_TABLES=1 ./build-microkorg2.sh              regenerate the 48 kHz tables even if they look
#                                                     current
#   DEBUG=1 ./build-microkorg2.sh                     the SDK's -Og -ggdb3 build
#
# Three things happen, in this order; the first two are build.sh's own steps, and both scripts use
# the same files rather than two copies of them:
#
#   1. Felucca's lookup tables are regenerated for 48 kHz into shim/gen/ (gitignored), which is what
#      the microKORG2 runs at: the unit refuses any other rate (shim/fluyt_microkorg2_unit.c, and the
#      header comment there says why the tables are what fixes it). The drumlogue runs at 48 kHz too,
#      so build.sh's tables are these.
#   2. Each unit's rows.def and config.mk are regenerated from Felucca's own descriptors by
#      tools/gen_rows.c, this time with `microkorg2` as its platform: the engine's own rows first,
#      then the LFO's, thirteen at most, and no envelope - the instrument's own EG is the envelope,
#      and the unit's ADSR is held open instead (shim/fluyt_microkorg2.c).
#   3. Each unit is built in the container, which is where the SDK's cross toolchain lives.
#
# Felucca itself is a submodule and is read-only throughout: it is mounted into the container at
# /fluyt together with the shim, and the unit being built is mounted separately, writable, because
# that is where the SDK puts build/ and the .mk2unit.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

LOGUE_SDK_PATH="${LOGUE_SDK_PATH:-$HOME/Projects/logue/logue-sdk}"
PLATFORM_PATH="$LOGUE_SDK_PATH/platform"
CONTAINER_IMAGE="${CONTAINER_IMAGE:-ghcr.io/gbraad-logue/logue-sdk/dev-env:latest}"

# the rate the tables are generated for: the microKORG2's own, and the reason step 1 exists
FLUYT_FS=48000

UNITS="fluyt_analog_microkorg2 fluyt_trio_microkorg2 fluyt_phase_microkorg2 \
       fluyt_lofi_microkorg2 fluyt_wheel_microkorg2 fluyt_noise_microkorg2 \
       fluyt_formant_microkorg2 fluyt_drum_microkorg2 fluyt_fm6_microkorg2"

if [ $# -gt 0 ]; then
    UNITS="$@"
fi

echo "========================================"
echo "Building Fluyt units for microKORG2"
echo "========================================"
echo "Platform:    $PLATFORM_PATH"
echo "Felucca:     $SCRIPT_DIR/felucca"
echo "Tables:      $FLUYT_FS Hz"
echo "Container:   $CONTAINER_IMAGE"
echo ""

if [ ! -d "$PLATFORM_PATH/microkorg2" ]; then
    echo "Error: microKORG2 platform not found at $PLATFORM_PATH/microkorg2" >&2
    echo "       Set LOGUE_SDK_PATH to your logue-sdk checkout." >&2
    exit 1
fi

if [ ! -f felucca/firmware/src/eng_analog.c ]; then
    echo "Error: Felucca submodule is not checked out here." >&2
    echo "       git submodule update --init --depth 1" >&2
    exit 1
fi

# ---------------------------------------------------------------- 1. tables ---

# The tables are correct for one rate, and the header says which: the regeneration is skipped only
# when the header is there, is newer than the generator, and is for the rate we build at. (The same
# check as build.sh's, on the same two files: the drumlogue and the microKORG2 are both 48 kHz.)
tables_current() {
    [ -z "$FORCE_TABLES" ] &&
        [ -f shim/gen/felucca_tables.h ] &&
        [ -f shim/gen/felucca_fm6.h ] &&
        [ shim/gen/felucca_tables.h -nt felucca/tools/gen_tables.py ] &&
        [ shim/gen/felucca_fm6.h -nt felucca/tools/gen_fm6_patches.py ] &&
        grep -q "^#define FS $FLUYT_FS\$" shim/gen/felucca_tables.h
}

if tables_current; then
    echo "Tables: current ($FLUYT_FS Hz)"
else
    echo "Tables: generating for $FLUYT_FS Hz -> shim/gen/"
    mkdir -p shim/gen
    # FS (and CTL) are module globals that main() and the FM6 tables read, so setting them here is
    # what makes this the 48 kHz tables Felucca would have generated for itself. Note that the paths
    # below are the submodule's: nothing is written into it.
    python3 -c "
import sys
sys.path.insert(0, 'felucca/tools')
import gen_tables
gen_tables.FS = $FLUYT_FS
gen_tables.main('shim/gen/felucca_tables.h')
" >/dev/null
    python3 felucca/tools/gen_fm6_patches.py shim/gen/felucca_fm6.h >/dev/null
    grep -q "^#define FS $FLUYT_FS\$" shim/gen/felucca_tables.h ||
        { echo "Error: the tables came out for another rate" >&2; exit 1; }
fi

# ------------------------------------------------------------- 2. the panels ---

# The panels and the config.mk that go with them are generated from Felucca's descriptors, so they
# cannot drift from it. One generator, two platforms: what this script asks for is the microKORG2
# walk, which keeps the engine's own rows and the LFO's and drops the envelope group (the
# instrument's EG is that), and declares the unit an `osc` module rather than a `synth`.
echo "Panels:"
mkdir -p build bin
cc -O1 -o build/gen_rows tools/gen_rows.c -Ishim -Ishim/gen -Ifelucca/firmware/src \
    -DFLUYT_ALL_ENGINES
i=0
for unit in $UNITS; do
    # the unit's place in tools/gen_rows.c's UNITS[] (and shim/fluyt_engines.c's FLUYT_FAMILY[])
    case "$unit" in
        fluyt_analog_microkorg2)  i=0 ;;
        fluyt_trio_microkorg2)    i=1 ;;
        fluyt_phase_microkorg2)   i=2 ;;
        fluyt_lofi_microkorg2)    i=3 ;;
        fluyt_wheel_microkorg2)   i=4 ;;
        fluyt_noise_microkorg2)   i=5 ;;
        fluyt_formant_microkorg2) i=6 ;;
        fluyt_drum_microkorg2)    i=7 ;;
        fluyt_fm6_microkorg2)     i=8 ;;
        *) echo "  $unit: no such unit (see tools/gen_rows.c)"; exit 1 ;;
    esac
    [ -d "$unit" ] || { echo "  $unit: no such directory"; exit 1; }
    # a unit is its Makefile and the two files below, nothing else: the code is the shim's. The
    # Makefile is the SDK's own dummy-osc one, copied as it is.
    [ -f "$unit/Makefile" ] || cp "$PLATFORM_PATH/microkorg2/dummy-osc/Makefile" "$unit/Makefile"
    mkdir -p "build/rows/microkorg2/$i"
    build/gen_rows "build/rows/microkorg2/$i" "$i" microkorg2 >/dev/null   # what the panel should
                                                                          # be, off to one side
    for f in rows.def config.mk; do
        if ! cmp -s "build/rows/microkorg2/$i/$f" "$unit/$f"; then
            cp "build/rows/microkorg2/$i/$f" "$unit/$f"
            echo "  $unit/$f: updated"
        fi
    done
    echo "  $unit: $(grep -c FLUYT_ROW "$unit/rows.def") rows, \
$(sed -n 's/.*FLUYT_UNIT_PRESETS=\([0-9]*\).*/\1/p' "$unit/config.mk") presets"
done

# ---------------------------------------------------------------- 3. build ---

mkdir -p bin

build_unit() {
    local unit="$1"
    local marg=""
    [ -n "$DEBUG" ] && marg="DEBUG=$DEBUG"

    echo "========================================"
    echo "Building $unit..."
    echo "========================================"

    # The SDK's post-link hook chowns build/ and .dep/ to the uid it reads off the unit directory
    # (stat -c %u .), and the container's own user is what the unit has to end up owned by: naming
    # the user and group here is what keeps that chown to one the build user may make.
    chmod 777 "$unit" 2>/dev/null || true

    # The repo is mounted read-only at /fluyt, the shim and Felucca with it: the only writable thing
    # in there is the unit's own directory, mounted a second time. SHIM_PATH and FELUCCA_PATH are
    # passed rather than baked into config.mk so that the same unit builds from this directory and
    # from a container that mounted the repo somewhere else.
    podman run --rm \
        -v "$PLATFORM_PATH:/workspace:rw" \
        -v "$SCRIPT_DIR/$unit:/workspace/microkorg2/$unit:rw" \
        -v "$SCRIPT_DIR:/fluyt:ro" \
        -h logue-sdk \
        $CONTAINER_IMAGE \
        /app/cmd_entry bash -c "
            source /app/microkorg2/environment && \
            cd /workspace/microkorg2/$unit && \
            make clean $marg USER_ID=user GROUP_ID=user \
                 SHIM_PATH=/fluyt/shim FELUCCA_PATH=/fluyt/felucca && \
            make install $marg USER_ID=user GROUP_ID=user \
                 SHIM_PATH=/fluyt/shim FELUCCA_PATH=/fluyt/felucca
        "

    find "$SCRIPT_DIR/$unit" -maxdepth 1 -name "*.mk2unit" -exec cp {} "$SCRIPT_DIR/bin/" \;
    echo "✓ $unit built"
}

for unit in $UNITS; do
    build_unit "$unit"
done

echo ""
echo "========================================"
echo "Units in bin/:"
ls -1 bin/*.mk2unit 2>/dev/null | sed 's|^|  |'
echo "========================================"
echo "Install the way any microKORG2 unit is installed: USB mass storage mode (the first option"
echo "button held while turning the instrument on), then the .mk2unit into a SlotN folder under"
echo "Units/Oscs on the drive it shows up as. The oscillator then appears as F-ANALOG, F-TRIO, ..."
