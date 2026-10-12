#!/bin/bash
# SPDX-License-Identifier: MIT
# Fluyt - Felucca's engines as drumlogue units.
# Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
# This build script follows the one written for Regroove | RGRV's own logue units (rfx/logue,
# regroovelizer) and is licensed permissively, like it: MIT, see docs/LICENSING.md and LICENSES/MIT.txt.
# The units it builds are GPL-3.0-only, because Felucca is: Felucca is Copyright (C) 2026 Leo
# Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only). See docs/LICENSING.md.
#
# Build every Fluyt unit with the logue SDK container, the way rfx/logue and regroovelizer do.
#
#   ./build.sh                          all nine units
#   ./build.sh fluyt_analog_drumlogue   just the ones named
#   FORCE_TABLES=1 ./build.sh           regenerate the 48 kHz tables even if they look current
#   DEBUG=1 ./build.sh                  the SDK's -Og -ggdb3 build
#
# Three things happen, in this order:
#
#   1. Felucca's lookup tables are regenerated for 48 kHz into shim/gen/ (gitignored). Felucca
#      generates them for its own 44.1 kHz; the drumlogue runs at 48 kHz, and everything the tables
#      hold - pitch, envelopes, LFO rates, cutoff, levels, decay, the soft clip - would be 8.8% off
#      if they were not. The submodule is never written to: the output goes here.
#   2. Each unit's rows.def and config.mk are regenerated from Felucca's own descriptors by
#      tools/gen_rows.c. They are checked in, so this only rewrites them when something changed.
#   3. Each unit is built in the container, which is where the SDK's cross toolchain lives.
#
# Felucca itself is a submodule and is read-only throughout: it is mounted into the container at
# /fluyt together with the shim, and the unit being built is mounted separately, writable, because
# that is where the SDK puts build/ and the .drmlgunit.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

LOGUE_SDK_PATH="${LOGUE_SDK_PATH:-$HOME/Projects/logue/logue-sdk}"
PLATFORM_PATH="$LOGUE_SDK_PATH/platform"
CONTAINER_IMAGE="${CONTAINER_IMAGE:-ghcr.io/gbraad-logue/logue-sdk/dev-env:latest}"

# the rate the tables are generated for: the drumlogue's own, and the reason step 1 exists
FLUYT_FS=48000

UNITS="fluyt_analog_drumlogue fluyt_trio_drumlogue fluyt_phase_drumlogue \
       fluyt_lofi_drumlogue fluyt_wheel_drumlogue fluyt_noise_drumlogue \
       fluyt_formant_drumlogue fluyt_drum_drumlogue fluyt_fm6_drumlogue"

if [ $# -gt 0 ]; then
    UNITS="$@"
fi

echo "========================================"
echo "Building Fluyt units for Drumlogue"
echo "========================================"
echo "Platform:    $PLATFORM_PATH"
echo "Felucca:     $SCRIPT_DIR/felucca"
echo "Tables:      $FLUYT_FS Hz"
echo "Container:   $CONTAINER_IMAGE"
echo ""

if [ ! -d "$PLATFORM_PATH/drumlogue" ]; then
    echo "Error: Drumlogue platform not found at $PLATFORM_PATH/drumlogue" >&2
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
# when the header is there, is newer than the generator, and is for the rate we build at.
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
    # felucca_tables.h is what every unit includes; the FM6 factory patches are FM6's alone.
    grep -q "^#define FS $FLUYT_FS\$" shim/gen/felucca_tables.h ||
        { echo "Error: the tables came out for another rate" >&2; exit 1; }
fi

# ------------------------------------------------------------- 2. the panels ---

# The panels and the config.mk that go with them are generated from Felucca's descriptors, so they
# cannot drift from it. Generating them here rather than only once means a Felucca update shows up
# in a diff rather than in a wrong panel.
echo "Panels:"
mkdir -p build/rows bin
cc -O1 -o build/gen_rows tools/gen_rows.c -Ishim -Ishim/gen -Ifelucca/firmware/src \
    -DFLUYT_ALL_ENGINES
i=0
for unit in $UNITS; do
    # the unit's place in tools/gen_rows.c's UNITS[] (and shim/fluyt_engines.c's FLUYT_FAMILY[])
    case "$unit" in
        fluyt_analog_drumlogue)  i=0 ;;
        fluyt_trio_drumlogue)    i=1 ;;
        fluyt_phase_drumlogue)   i=2 ;;
        fluyt_lofi_drumlogue)    i=3 ;;
        fluyt_wheel_drumlogue)   i=4 ;;
        fluyt_noise_drumlogue)   i=5 ;;
        fluyt_formant_drumlogue) i=6 ;;
        fluyt_drum_drumlogue)    i=7 ;;
        fluyt_fm6_drumlogue)     i=8 ;;
        *) echo "  $unit: no such unit (see tools/gen_rows.c)"; exit 1 ;;
    esac
    [ -d "$unit" ] || { echo "  $unit: no such directory"; exit 1; }
    # a unit is its Makefile and the two files below, nothing else: the code is the shim's
    [ -f "$unit/Makefile" ] || cp "$PLATFORM_PATH/drumlogue/dummy-synth/Makefile" "$unit/Makefile"
    build/gen_rows build/rows "$i" >/dev/null          # what the panel should be, off to one side
    for f in rows.def config.mk; do
        if ! cmp -s "build/rows/$f" "$unit/$f"; then
            cp "build/rows/$f" "$unit/$f"
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

    # The container user (uid 584287) creates build/ and .dep/ inside the unit directory, so that
    # directory has to be world-writable.
    chmod 777 "$unit" 2>/dev/null || true

    # The repo is mounted read-only at /fluyt, the shim and Felucca with it: the only writable thing
    # in there is the unit's own directory, mounted a second time. SHIM_PATH and FELUCCA_PATH are
    # passed rather than baked into config.mk so that the same unit builds from this directory and
    # from a container that mounted the repo somewhere else.
    podman run --rm \
        -v "$PLATFORM_PATH:/workspace:rw" \
        -v "$SCRIPT_DIR/$unit:/workspace/drumlogue/$unit:rw" \
        -v "$SCRIPT_DIR:/fluyt:ro" \
        -h logue-sdk \
        $CONTAINER_IMAGE \
        /app/cmd_entry bash -c "
            source /app/drumlogue/environment && \
            cd /workspace/drumlogue/$unit && \
            make clean $marg SHIM_PATH=/fluyt/shim FELUCCA_PATH=/fluyt/felucca && \
            make install $marg SHIM_PATH=/fluyt/shim FELUCCA_PATH=/fluyt/felucca
        "

    find "$SCRIPT_DIR/$unit" -maxdepth 1 -name "*.drmlgunit" -exec cp {} "$SCRIPT_DIR/bin/" \;
    echo "✓ $unit built"
}

for unit in $UNITS; do
    build_unit "$unit"
done

echo ""
echo "========================================"
echo "Units in bin/:"
ls -1 bin/*.drmlgunit 2>/dev/null | sed 's|^|  |'
echo "========================================"
echo "Install with the drumlogue's own unit loader (logue-cli / the device's file transfer),"
echo "then pick the unit on the device: they appear as FLUYT ANALOG, FLUYT TRIO, ..."
