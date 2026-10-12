#!/bin/bash
# SPDX-License-Identifier: MIT
# Fluyt - Felucca's engines as logue units.
# Copyright (C) 2026 Gerard Braad (Regroove | RGRV).
# Another build script in the shape of the ones written for Regroove | RGRV's own logue units; see
# docs/LICENSING.md. What it runs - the units in bin/, which carry Felucca - is GPL-3.0-only:
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments.
#
# reginecheck.sh -- run the built units through ReGINE's dlhost and check what comes back.
#
# tools/hostcheck.sh builds the units' sources and checks them on this machine. This is the other
# half: it runs the file that ships - bin/<unit>.drmlgunit, a 32-bit ARM shared object - through
# ReGINE, Regroove | RGRV's own logue runtime, which is where the drumlogue's side of the unit is
# real: the header the device reads, the panel's own text, the note and gate events of a sequencer
# track, the mix. A unit that passes here has loaded, initialised, answered for its panel and
# rendered on ARM, which is most of what the device does with it. What this covers, and what it
# leaves to the device, is in docs/reginecheck.md.
#
# ReGINE is not part of this repository (it lives in the regroovelizer tree), and dlhost is armhf:
# it runs natively on a 32-bit ARM Linux, under qemu-arm with an armhf sysroot on a desktop, or on
# another machine over ssh (--host, which is how a Raspberry Pi is used here). Nothing is checked
# if no dlhost can be found, and that is said rather than passed over.
#
# What is checked, and why each check is here:
#
#   loads     every unit loads and initialises, `--info` exits 0, and the module the report names
#             is the one the unit's own config.mk builds for. Exit 2 is dlhost saying the file is
#             not a unit it can load, which is the whole of a header the SDK would refuse.
#             A Developer Version build of ReGINE loads only its licensee's units and refuses the
#             rest at load, before unit_init: that refusal is reported as a skip with the line
#             dlhost printed, because it is the licence working, not the unit failing, and nothing
#             below it can be seen through a host that will not load the file.
#   name      the name the device will show is the one config.mk declares. It is read out of the
#             shipped file, not out of rows.def, so a build that lost a -D is caught here.
#   ids       dev_id is 0x72677276 ("rgrv", what every Regroove | RGRV unit declares) and unit_id
#             is this unit's own slot in the Fluyt family.
#   presets   the preset count of the engine, from config.mk.
#   panel     every row, against the unit's own rows.def: the same labels in the same order, the
#             device's type, and the numbers the row is declared with. That last part is the
#             reason this check exists at all: a row whose text the unit supplies is held by the
#             runtime in a byte, so every row has to be declared from zero up (min 0, the span as
#             Felucca's own width, Felucca's zero in the center). A row declared below zero reads
#             back as its own maximum - the whole lower half of a bipolar row showing its top,
#             which is how PIT answered "+98%" from -64 to 0 before this was fixed. The check is
#             against rows.def because that is the only place the panel is written down; a second
#             copy of it here would be a second thing to be wrong.
#   read-backs every row whose text the unit supplies answers something, at a value inside the
#             row's own range. A `(null)` or an empty answer is a row the device would show
#             blank. The text is Felucca's own formatting, reached through the device's own call.
#   pit       the LFO's pitch amount, moved to its bottom, its middle and its top with --raw:
#             "-100%", "0%" and "+98%". Those three are Felucca's words for the row's own -64..63,
#             and they are the bug this panel's rule came from, so they are pinned rather than
#             inferred. The two ends are also the first pair of renders below.
#   render    a real note (the unit's own gate note from config.mk: 60 for the eight melodic
#             engines, 36 for FLUYT DRUM) for --seconds: it exits 0, it is not silence, it is
#             finite, it stays inside the +/-1.0 the device takes, and the WAV it wrote is the
#             length the report says. A unit that loads and initialises but renders silence is
#             the failure this catches.
#   gate      the same render through gate_on/gate_off instead of note_on/note_off - the path
#             the drumlogue's sequencer uses. The bridge renders the gate as the engine's own
#             note, so the two have to agree to the sample: a gate that renders differently is
#             either a different note or a different voice, and both are wrong.
#   knobs     every envelope row, the LFO's amplitude amount, and - where the engine renders a
#             filter - the envelope's filter amount, each rendered at its own bottom and at its own
#             top and compared as the two WAVs. A row that is on the panel but wired to nothing
#             passes every check above and fails here. Two of these pairs are meant to come back
#             identical, and the unit's own config.mk says which: FM6's operator envelopes are its
#             amplitude (ownenv) and DRUM's hit is (oneshot), so neither reads the track's ADSR and
#             no ATK, DEC, SUS or REL can move either - and on FM6 there is no envelope for the
#             envelope's FLT to scale. The rows stay on those panels because the panel is Felucca's
#             and Felucca's own editor shows the envelope group for every engine. An identical pair
#             with no such reason behind it is a failure, and so is a pair that moves on a unit
#             whose config.mk says it cannot (a stale flag). REL needs --hold to be reachable at
#             all: a note that is never released has no release in it.
#
# Every render is left in out/dlhost/, with the report dlhost printed for it: that is the evidence,
# and `bin/out.wav` per unit is the one to listen to.
#
# Usage:
#   tools/reginecheck.sh                       all nine units
#   tools/reginecheck.sh fluyt_fm6             one, by unit name or folder name
#   tools/reginecheck.sh --seconds 5           longer renders
#   tools/reginecheck.sh --host user@board     run dlhost on another machine over ssh
#
# Environment:
#   DLHOST          the dlhost binary to run. Otherwise the regroovelizer tree's own build is
#                   looked for, and the first candidate that actually runs is used.
#   DLHOST_HOST     an ssh target to run dlhost on (an ARM machine, the Pi). Copying the units
#                   there is all this script does on the remote side.
#   DLHOST_REMOTE   the dlhost to run there, resolved by its shell. Default: dlhost, from PATH.
#   QEMU, SYSROOT   how to run an armhf dlhost here: `qemu-arm-static -L <sysroot>` by default,
#                   and `QEMU=` for a host that runs it natively. Dropped by itself when the
#                   binary runs here anyway, which is what a 32-bit ARM board does.
#   REGROOVELIZER   the regroovelizer checkout. Defaults to the sibling of this repo, then
#                   ~/Projects/gbraad-music/regroovelizer.
#   SECONDS_FULL    how long the note render is, and the pairs are SECONDS_FULL/5.

set -u

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
BIN=$ROOT/bin
OUT=$ROOT/out/dlhost
TIMEOUT=${TIMEOUT:-120}

# The header above is the usage text, so it is printed from its own source rather than written out
# a second time: everything from the line that names this script to the first line that is not a
# comment. The licence lines above it stay out of it.
usage() {
    awk 'NR == 1 { next }
         /^# reginecheck\.sh/ { on = 1 }
         on { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
}

ALL="fluyt_analog fluyt_trio fluyt_phase fluyt_lofi fluyt_wheel \
     fluyt_noise fluyt_formant fluyt_drum fluyt_fm6"

WANT=()
SECONDS_OPT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --seconds) SECONDS_OPT="${2:?--seconds takes a value}"; shift ;;
        --host)    DLHOST_HOST="${2:?--host takes an ssh target}"; shift ;;
        -h|--help) usage; exit 0 ;;
        -*)        echo "reginecheck: unknown option '$1'" >&2; usage >&2; exit 1 ;;
        *)         WANT+=("$1") ;;
    esac
    shift
done
SECONDS_FULL=${SECONDS_OPT:-${SECONDS_FULL:-2.0}}
SECONDS_PAIR=$(awk -v s="$SECONDS_FULL" 'BEGIN { printf "%.2f", s/5 }')
DLHOST_HOST=${DLHOST_HOST:-}

# fluyt_trio / fluyt_trio_drumlogue / bin/fluyt_trio.drmlgunit are all the same unit here.
unit_name() {
    local a
    a=$(basename "$1")
    a=${a%.drmlgunit}
    a=${a%_drumlogue}
    printf '%s' "$a"
}

[ ${#WANT[@]} -gt 0 ] || read -r -a WANT <<<"$ALL"
for i in "${!WANT[@]}"; do WANT[$i]=$(unit_name "${WANT[$i]}"); done

# --------------------------------------------------------------- the unit's own truth ---
# The panel and the unit's numbers are read out of the unit's own rows.def and config.mk, the same
# way tools/hostcheck.sh reads its UDEFS: there is never a second copy of a -D here to disagree
# with the first, and a unit whose config.mk is wrong fails rather than being agreed with.
udefs_of() {
    sed -n 's/^UDEFS[[:space:]]*\(+*\)=[[:space:]]*//p' "$1/config.mk" | tr '\n' ' '
}

# Those UDEFS are written the way make hands them to the shell: -DFLUYT_UNIT_NAME=\"FLUYT\ TRIO\"
# is one word to the compiler. Undo both escapes before comparing anything to it, and take the C
# string's own quotes off: what the header carries is the name, not its spelling in a -D.
udef() {
    printf '%s\n' "$1" |
        sed -e 's/\\"/"/g' -e "s/\\\\ /$(printf '\001')/g" |
        tr ' ' '\n' | sed -n "s/^-D$2=//p" | head -1 |
        sed -e 's/^"//' -e 's/"$//' | tr '\001' ' '
}

# The same, for a value that is a number in C (0x464C5901U), where the suffix is not part of it.
# Only where what is left is a number as well: FLUYT_UNIT_NAME is \"FLUYT\ WHEEL\", which ends in an
# L that is a letter of the name rather than a suffix, and stripping it here would check every unit
# called WHEEL against 'FLUYT WHEE'.
udef_num() {
    local v t
    v=$(udef "$1" "$2")
    t=${v%[uUlL]}
    case "$t" in
        ''|*[!0-9xXa-fA-F]*) printf '%s' "$v" ;;
        *) printf '%s' "$t" ;;
    esac
}

# param|label|felucca-min|felucca-max|felucca-default, one per row, in panel order. The parameter is
# Felucca's own name for the row (P_ATK, P_ED_FLT), which is what tells the four ADSR rows and the
# envelope's filter amount apart from a row that happens to be called the same thing.
rows_of() {
    sed -n 's/^FLUYT_ROW(\([A-Z_0-9]*\), *"\([^"]*\)", *\(-\{0,1\}[0-9][0-9]*\), *\(-\{0,1\}[0-9][0-9]*\), *\(-\{0,1\}[0-9][0-9]*\),.*/\1|\2|\3|\4|\5/p' \
        "$1/rows.def"
}

row_param() { printf '%s' "${1%%|*}"; }
row_rest() { local r=${1#*|}; printf '%s' "${r#*|}"; }   # min|max|default, past the parameter and label
row_label() { local r=${1#*|}; printf '%s' "${r%%|*}"; }
row_span() { local mn mx df; IFS='|' read -r mn mx df <<<"$(row_rest "$1")"; printf '%s' "$(( mx - mn ))"; }

# ------------------------------------------------------------- which dlhost, where ---
REG=${REGROOVELIZER:-}
if [ -z "$REG" ]; then
    # Checked by a file rather than by the directory existing: an empty directory of the right
    # name is what a half-finished checkout leaves behind.
    for c in "$ROOT/../regroovelizer" "$HOME/Projects/gbraad-music/regroovelizer"; do
        if [ -f "$c/tools/dlhost/dlhost_main.c" ]; then REG=$(cd "$c" && pwd); break; fi
    done
fi

QEMU=${QEMU-qemu-arm-static}
SYSROOT=${SYSROOT:-${REG:+$REG/logue/rgnthost_drumlogue/test/sysroot}}
if [ -n "$QEMU" ] && ! command -v "$QEMU" >/dev/null 2>&1; then QEMU=""; fi

# An armhf binary on a desktop needs qemu and an armhf sysroot; on a 32-bit ARM board it needs
# neither. Rather than guess from uname, ask the binary: if it runs here, qemu is not used,
# whatever it was set to.
DLHOST=${DLHOST:-}
runs_here() {
    "$1" --help >/dev/null 2>&1 && { QEMU=""; return 0; }
    if [ -n "$QEMU" ] && [ -n "$SYSROOT" ] &&
       "$QEMU" -L "$SYSROOT" "$1" --help >/dev/null 2>&1; then return 0; fi
    return 1
}

SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=10)

REMOTE_TMP=""
if [ -n "$DLHOST_HOST" ]; then
    # The remote end is dlhost's own machine: the units are copied there and every render happens
    # there. DLHOST_REMOTE is left to that machine's PATH, which is where a board's own build is.
    DLHOST_REMOTE=${DLHOST_REMOTE:-dlhost}
    if ! REMOTE_TMP=$(ssh "${SSH_OPTS[@]}" "$DLHOST_HOST" 'mktemp -d /tmp/fluyt-dlhost.XXXXXX'); then
        echo "reginecheck: cannot reach $DLHOST_HOST over ssh" >&2
        exit 1
    fi
    trap 'ssh "${SSH_OPTS[@]}" "$DLHOST_HOST" "rm -rf $REMOTE_TMP" 2>/dev/null' EXIT
    MISSING=0
    for n in "${WANT[@]}"; do [ -f "$BIN/$n.drmlgunit" ] || MISSING=1; done
    if [ $MISSING = 1 ]; then
        echo "reginecheck: some units are not built; run ./build.sh first" >&2
        exit 1
    fi
    scp -q "${SSH_OPTS[@]}" "$BIN"/*.drmlgunit "$DLHOST_HOST:$REMOTE_TMP/" || {
        echo "reginecheck: cannot copy the units to $DLHOST_HOST" >&2; exit 1; }
else
    if [ -z "$DLHOST" ]; then
        for c in "$REG/tools/dlhost/out/dlhost" "$REG"/build-armhf*/dlhost \
                 "$REG/logue/selfhost-test/out/dlhost"; do
            [ -x "$c" ] || continue
            if runs_here "$c"; then DLHOST=$c; break; fi
        done
    else
        runs_here "$DLHOST" || { echo "reginecheck: $DLHOST does not run here" >&2; exit 1; }
    fi
    if [ -z "$DLHOST" ]; then
        echo "reginecheck: no dlhost found that runs here, and none was given." >&2
        echo "             dlhost is armhf: build ReGINE for arm-linux-gnueabihf and give it as" >&2
        echo "             DLHOST=, run this on an ARM machine with DLHOST_HOST=host, or set" >&2
        echo "             QEMU=/SYSROOT= for an armhf sysroot. See docs/reginecheck.md." >&2
        exit 1
    fi
    echo "dlhost     : $DLHOST${QEMU:+ (under $QEMU -L $SYSROOT)}"
fi

mkdir -p "$OUT"

# ------------------------------------------------------------------------- the runner ---
# Nothing of the unit's own path is repeated in the call: the unit under test is $RUN_UNIT, which
# is our own bin/ locally and the copy in the remote temp directory there.
RUN_UNIT=""
dl() {
    local log=$1; shift
    local rc
    if [ -n "$DLHOST_HOST" ]; then
        # printf %q, because this string is handed to the remote shell: the arguments carry a
        # path and `--raw <row>=<value>`, and both have to arrive as one word each.
        ssh "${SSH_OPTS[@]}" "$DLHOST_HOST" \
            "$(printf '%q ' timeout "$TIMEOUT" "$DLHOST_REMOTE" --unit "$RUN_UNIT" "$@")" \
            >"$log" 2>&1
    elif [ -n "$QEMU" ]; then
        timeout "$TIMEOUT" "$QEMU" -L "$SYSROOT" "$DLHOST" --unit "$RUN_UNIT" "$@" >"$log" 2>&1
    else
        timeout "$TIMEOUT" "$DLHOST" --unit "$RUN_UNIT" "$@" >"$log" 2>&1
    fi
    rc=$?
    [ $rc -eq 124 ] && echo "reginecheck: timed out after ${TIMEOUT}s" >>"$log"
    return $rc
}

# The report's own numbers. peak reads `nan` when anything in the buffer was not a number, which
# is why it is compared as a number below and not just printed.
metric() { awk -v k="$2" '$1 == k { print $3; exit }' "$1"; }
frames_of() { sed -n 's/^rendered *: *\([0-9][0-9]*\) frames.*/\1/p' "$1" | head -1; }
field() { sed -n "s/^$2 *: *//p" "$1" | head -1; }

# The two tables in the report the checks read, as `index|label|type|min|max|init|center` and
# `index|label|value|text`. Both are matched by their shape rather than by awk's fields: the row
# number is `[ 0]` up to `[ 9]` and `[10]` from there on, so a field number is not the same thing
# for two rows of the same table.
panel_of() {
    sed -n 's/^ *\[ *\([0-9][0-9]*\)\] *\([^ ]*\) *\([^ ]*\) *min *\(-\{0,1\}[0-9][0-9]*\) *max *\(-\{0,1\}[0-9][0-9]*\) *init *\(-\{0,1\}[0-9][0-9]*\) *center *\(-\{0,1\}[0-9][0-9]*\).*/\1|\2|\3|\4|\5|\6|\7/p' "$1"
}
rb_of() {
    sed -n 's/^ *\[ *\([0-9][0-9]*\)\] *\([^ ]*\) *= *\(-\{0,1\}[0-9][0-9]*\) *"\(.*\)"$/\1|\2|\3|\4/p' "$1"
}

PASSED=0; FAILED=0; SKIPPED=0
pass() { printf '  [PASS] %s\n' "$1"; PASSED=$((PASSED + 1)); }
fail() { printf '  [FAIL] %s\n' "$1"; FAILED=$((FAILED + 1)); }
skip() { printf '  [SKIP] %s\n' "$1"; SKIPPED=$((SKIPPED + 1)); }
say()  { printf '  [note] %s\n' "$1"; }

TABLE=()

for name in "${WANT[@]}"; do
    folder=$ROOT/${name}_drumlogue
    file=$BIN/$name.drmlgunit

    printf '\n== %s\n' "$name"
    if [ ! -d "$folder" ] || [ ! -f "$file" ]; then
        fail "$name: no $name/ folder or no $file (run ./build.sh)"
        TABLE+=("$(printf '%-14s %-6s %s' "$name" "-" "not built")")
        continue
    fi

    udefs=$(udefs_of "$folder")
    exp_name=$(udef "$udefs" FLUYT_UNIT_NAME)
    exp_id=$(udef_num "$udefs" FLUYT_UNIT_ID)
    exp_presets=$(udef_num "$udefs" FLUYT_UNIT_PRESETS)
    exp_gate=$(udef_num "$udefs" FLUYT_GATE_NOTE)
    exp_cutoff=$(udef_num "$udefs" FLUYT_ENGINE_CUTOFF)
    # what the engine's own code says about itself, out of the generator's UNITS[]: whether the
    # track's ADSR is its amplitude at all, and the row its own filter opening comes from
    exp_ownenv=$(udef_num "$udefs" FLUYT_ENGINE_OWNENV)
    exp_oneshot=$(udef_num "$udefs" FLUYT_ENGINE_ONESHOT)
    exp_tone=$(udef "$udefs" FLUYT_ENGINE_TONE)
    exp_module=$(sed -n 's/^PROJECT_TYPE *:= *//p' "$folder/config.mk" | head -1)
    mapfile -t ROWS < <(rows_of "$folder")
    exp_rows=${#ROWS[@]}

    for v in exp_name exp_id exp_presets exp_gate exp_cutoff exp_module exp_ownenv exp_oneshot; do
        if [ -z "${!v}" ]; then
            fail "$name: config.mk does not declare $v, so there is nothing to check against"
        fi
    done
    [ "$exp_rows" -gt 0 ] || fail "$name: rows.def has no rows"

    RUN_UNIT=$file
    [ -n "$DLHOST_HOST" ] && RUN_UNIT=$REMOTE_TMP/$(basename "$file")

    # ---- loads, and everything the header says about itself
    L=$OUT/$name.info.log
    dl "$L" --info; rc=$?
    if [ $rc -ne 0 ]; then
        if grep -q 'declares dev_id\|belonging to another developer' "$L"; then
            skip "$name: the licence refused it at load ($(field "$L" drumlogue | cut -c1-130))"
            say "  that is this dlhost's own licence working, not the unit failing; nothing below"
            say "  it can be seen through a host that will not load the file"
            TABLE+=("$(printf '%-14s %-6s %s' "$name" "-" "licence")")
        else
            fail "$name: --info exited $rc ($(head -1 "$L"))"
            TABLE+=("$(printf '%-14s %-6s %s' "$name" "-" "load failed")")
        fi
        continue
    fi
    got_name=$(field "$L" name)
    if [ "$got_name" = "$exp_name" ]; then
        pass "name: $got_name"
    else
        fail "$name: the header says '$got_name', config.mk declares '$exp_name'"
    fi
    got_ids=$(field "$L" ids)
    want_ids=$(printf 'dev 0x72677276 unit 0x%08x' "$((exp_id))")
    if [ "$got_ids" = "$want_ids" ]; then
        pass "ids: $got_ids"
    else
        fail "$name: the header says '$got_ids', expected '$want_ids'"
    fi
    got_module=$(field "$L" module)
    if [ "$got_module" = "$exp_module" ]; then
        pass "module: $got_module"
    else
        fail "$name: the header's module is '$got_module', config.mk builds '$exp_module'"
    fi
    got_presets=$(field "$L" presets)
    if [ "$got_presets" = "$exp_presets" ]; then
        pass "presets: $got_presets"
    else
        fail "$name: the header says $got_presets presets, config.mk declares $exp_presets"
    fi
    got_params=$(field "$L" params)
    if [ "$got_params" = "$exp_rows" ]; then
        pass "rows: $got_params declared"
    else
        fail "$name: the header declares $got_params rows, rows.def has $exp_rows"
    fi

    # ---- the panel, against the unit's own rows.def
    i=0; bad=0
    while IFS='|' read -r idx label type min max init center; do
        if [ -z "${ROWS[$i]:-}" ]; then
            fail "$name: the header has more rows than rows.def ($((i + 1)))"
            bad=$((bad + 1)); i=$((i + 1)); continue
        fi
        IFS='|' read -r emn emx edf <<<"$(row_rest "${ROWS[$i]}")"
        elabel=$(row_label "${ROWS[$i]}")
        wmax=$((emx - emn)); winit=$((edf - emn)); wcenter=$((0 - emn))
        if [ "$label" != "$elabel" ] || [ "$type" != strings ] || [ "$min" != 0 ] ||
           [ "$max" != "$wmax" ] || [ "$init" != "$winit" ] || [ "$center" != "$wcenter" ]; then
            fail "panel [$i]: the device reads '$label' $type $min..$max init $init center $center; rows.def has $elabel 0..$wmax init $winit center $wcenter"
            bad=$((bad + 1))
        fi
        i=$((i + 1))
    done < <(panel_of "$L")
    [ $i -eq "$exp_rows" ] || {
        fail "$name: the header declares $i rows, rows.def has $exp_rows"; bad=$((bad + 1)); }
    if [ $bad -eq 0 ]; then
        pass "panel: $exp_rows rows, every one declared from zero up with Felucca's zero in the center"
    fi

    # ---- the rows the checks below move have to be on the panel
    # PIT is the LFO's pitch amount (the walk always ends PIT [FLT] [SHP] AMP) and FLT the
    # envelope's filter amount, which is the same label the LFO's filter destination carries -
    # the first of the two is the envelope's, and that is the one the knobs check moves. TONE is
    # LOFI's own filter row, closed for that one pair (see inert_reason below).
    PIT=-1; AMP=-1; FLT=-1; TONE=-1; ATK=-1; DEC=-1; SUS=-1; REL=-1
    for idx in "${!ROWS[@]}"; do
        case "$(row_label "${ROWS[$idx]}")" in
            PIT) [ $PIT -lt 0 ] && PIT=$idx ;;
            AMP) AMP=$idx ;;
            FLT) [ $FLT -lt 0 ] && FLT=$idx ;;
        esac
        case "$(row_param "${ROWS[$idx]}")" in
            P_ATK) ATK=$idx ;;
            P_DEC) DEC=$idx ;;
            P_SUS) SUS=$idx ;;
            P_REL) REL=$idx ;;
        esac
        if [ -n "$exp_tone" ] && [ "$(row_param "${ROWS[$idx]}")" = "$exp_tone" ]; then TONE=$idx; fi
    done
    [ $PIT -ge 0 ] || fail "$name: no PIT row, so the LFO's pitch amount is not on the panel"
    [ $AMP -eq $((${#ROWS[@]} - 1)) ] || say "the LFO's AMP is row $AMP, not the last row"
    if [ "$exp_cutoff" = 1 ] && [ $FLT -lt 0 ]; then
        fail "$name: this engine renders a filter, but no row carries its amount"
    fi
    if [ -n "$exp_tone" ] && [ $TONE -lt 0 ]; then
        fail "$name: config.mk names $exp_tone as this engine's own filter row, and the panel has no such row"
    fi
    for idx in $ATK $DEC $SUS $REL; do
        [ $idx -ge 0 ] || fail "$name: the panel is missing an ADSR row, so there is nothing to move"
    done

    # ---- the note render: not silent, finite, inside +/-1.0, and the WAV it says it wrote
    RUN_WAV=$OUT/$name.wav
    L=$OUT/$name.render.log
    if [ -n "$DLHOST_HOST" ]; then
        dl "$L" --note "$exp_gate" --seconds "$SECONDS_FULL" --out "$REMOTE_TMP/$name.wav"
        rc=$?
        [ $rc -eq 0 ] && scp -q "${SSH_OPTS[@]}" "$DLHOST_HOST:$REMOTE_TMP/$name.wav" "$RUN_WAV"
    else
        dl "$L" --note "$exp_gate" --seconds "$SECONDS_FULL" --out "$RUN_WAV"
        rc=$?
    fi
    peak=$(metric "$L" peak); rms=$(metric "$L" rms)
    if [ $rc -ne 0 ]; then
        fail "$name: the note render exited $rc ($(tail -1 "$L"))"
    elif [ "$peak" = "nan" ]; then
        fail "$name: the note render produced non-finite samples ($(grep -m1 non-finite "$L"))"
    elif [ -z "$peak" ] || [ -z "$rms" ]; then
        fail "$name: the note render reported no peak or rms"
    else
        ok=$(awk -v p="$peak" 'BEGIN { exit !(p > 0.0 && p <= 1.0) }' && echo yes || echo no)
        if [ "$ok" = yes ] && [ "$(awk -v r="$rms" 'BEGIN { exit !(r > 0.0) }' && echo yes)" = yes ]; then
            pass "render: note $exp_gate, ${SECONDS_FULL}s, peak $peak rms $rms"
        else
            fail "$name: the note render came back at peak $peak rms $rms (silence, or past the +/-1.0 the device takes)"
        fi
    fi
    if [ -f "$RUN_WAV" ]; then
        want_bytes=$((44 + $(frames_of "$L") * 4))
        got_bytes=$(stat -c %s "$RUN_WAV")
        if [ "$got_bytes" = "$want_bytes" ]; then
            pass "wav: $(frames_of "$L") frames, $got_bytes bytes, as the report says"
        else
            fail "$name: the WAV is $got_bytes bytes, the report's $(frames_of "$L") frames are $want_bytes"
        fi
    else
        fail "$name: no WAV at $RUN_WAV"
    fi

    # ---- the read-backs of that same render: every row answers, inside its own range
    bad=0; seen=0
    while IFS='|' read -r idx label value text; do
        seen=$((seen + 1))
        # Read as a number before it is used as an array subscript: bash would take a word there
        # for a variable of its own, and a parse that went wrong would fail as `DTN: unbound
        # variable` rather than as the panel it is.
        case "$idx" in
            ''|*[!0-9]*) fail "read-backs: '$idx' is not a row number"; bad=$((bad + 1)); continue ;;
        esac
        if [ -z "${ROWS[$idx]:-}" ]; then
            fail "read-backs [$idx]: a row that is not in rows.def"; bad=$((bad + 1)); continue
        fi
        elabel=$(row_label "${ROWS[$idx]}")
        wmax=$(row_span "${ROWS[$idx]}")
        if [ "$label" != "$elabel" ]; then
            fail "read-backs [$idx]: the unit answers for '$label', rows.def has $elabel"
            bad=$((bad + 1))
        fi
        if [ -z "$text" ] || [ "$text" = "(null)" ] || [ "$text" = "(null) " ]; then
            fail "read-backs [$idx]: $label answered '$text'"
            bad=$((bad + 1))
        fi
        if [ "$value" -lt 0 ] || [ "$value" -gt "$wmax" ] 2>/dev/null; then
            fail "read-backs [$idx]: $label holds $value, outside its declared 0..$wmax"
            bad=$((bad + 1))
        fi
    done < <(rb_of "$L")
    if [ "$seen" != "$exp_rows" ]; then
        fail "$name: $seen read-backs for $exp_rows rows"
    elif [ $bad -eq 0 ]; then
        pass "read-backs: $seen rows answer with Felucca's own text, none empty"
    fi

    # ---- the strings-row rule, as three texts
    # The three renders move the LFO's pitch amount to its own bottom, zero and top. Felucca's
    # words for those are -100%, 0% and +98%: the row is declared 0..127 with its zero at 64, and
    # if the declaration were counted from -64 the first of the three would read "+98%" as well.
    IFS='|' read -r pmn pmx pdf <<<"$(row_rest "${ROWS[$PIT]:-0|0|0|0|0}")"
    pit_center=$(( 0 - pmn )); pit_top=$(( pmx - pmn ))
    pit_text=""
    for v in 0 "$pit_center" "$pit_top"; do
        [ $PIT -ge 0 ] || break
        L=$OUT/$name.pit$v.log
        dl "$L" --raw "$PIT=$v" --note "$exp_gate" --seconds "$SECONDS_PAIR"
        pit_text="$pit_text$(rb_of "$L" | awk -F'|' -v i="$PIT" '$1 == i { print $4 }') "
    done
    if [ $PIT -lt 0 ]; then
        :                                       # already failed above: there is no row to move
    elif [ "$pit_text" = "-100% 0% +98% " ]; then
        pass "pit: the LFO's pitch amount answers -100% / 0% / +98% at its bottom, its zero and its top"
    else
        fail "pit: the LFO's pitch amount answers '$pit_text', not '-100% 0% +98%'"
    fi

    # ---- the gate path: the sequencer's own events, which have to render the same note
    L=$OUT/$name.gate.log
    dl "$L" --note "$exp_gate" --gate --seconds "$SECONDS_FULL"
    rc=$?
    gpeak=$(metric "$L" peak); grms=$(metric "$L" rms)
    if [ $rc -ne 0 ]; then
        fail "$name: the gate render exited $rc ($(tail -1 "$L"))"
    elif [ "$gpeak" = "$peak" ] && [ "$grms" = "$rms" ]; then
        pass "gate: gate_on renders the same note as note_on, peak $gpeak rms $grms"
    else
        fail "$name: gate_on renders peak $gpeak rms $grms, note_on peak $peak rms $rms"
    fi

    # ---- knobs: a row that is on the panel and wired to nothing passes everything above
    # Two renders, the row at its declared bottom and at its declared top, compared as the WAVs
    # themselves - not peak and rms, which two different sounds can agree on. On the remote side the
    # comparison happens there and nothing is copied back; locally the two files are compared here.
    wav_path() {  # wav_path <basename>
        if [ -n "$DLHOST_HOST" ]; then printf '%s' "$REMOTE_TMP/$1"; else printf '%s' "$OUT/$1"; fi
    }
    same_file() {  # same_file <a> <b>, both as dlhost's own machine sees them
        if [ -n "$DLHOST_HOST" ]; then
            ssh "${SSH_OPTS[@]}" "$DLHOST_HOST" "cmp -s $(printf '%q %q' "$1" "$2")"
        else
            cmp -s "$1" "$2"
        fi
    }

    # A row the engine's own code says cannot move the sound, by the Felucca parameter behind it and
    # the flags this unit's config.mk carries from the generator. Prints the reason, or nothing.
    # These rows are on the panel because the panel is Felucca's, and Felucca's own editor shows the
    # envelope group for every engine - so a pair that comes back identical here is the engine
    # behaving, not a row that has come unwired. Anything not named here and identical is a failure.
    inert_reason() {  # inert_reason <felucca-param>
        case "$1" in
            P_ATK|P_DEC|P_SUS|P_REL)
                if [ "$exp_ownenv" = 1 ]; then
                    printf '%s: the operator envelopes are this voice (ownenv), not the track ADSR' "$exp_name"
                elif [ "$exp_oneshot" = 1 ]; then
                    printf '%s: the hit is this voice (oneshot) and holds the ADSR at full' "$exp_name"
                fi
                ;;
            P_ED_FLT)
                if [ "$exp_ownenv" = 1 ]; then
                    printf '%s: ownenv holds the track envelope at zero, so FLT has nothing to scale' "$exp_name"
                fi
                ;;
        esac
    }

    pair() {  # pair <what> <row> <low> <high> [extra dlhost args...]
        local what=$1 row=$2 low=$3 high=$4
        shift 4
        local lop=$OUT/$name.$what.$low.log hip=$OUT/$name.$what.$high.log
        local lowav higav rclo rchi a b reason
        lowav=$(wav_path "$name.$what.$low.wav"); higav=$(wav_path "$name.$what.$high.wav")
        dl "$lop" --raw "$row=$low" --note "$exp_gate" --seconds "$SECONDS_PAIR" "$@" --out "$lowav"
        rclo=$?
        dl "$hip" --raw "$row=$high" --note "$exp_gate" --seconds "$SECONDS_PAIR" "$@" --out "$higav"
        rchi=$?
        a="$(metric "$lop" peak) $(metric "$lop" rms)"
        b="$(metric "$hip" peak) $(metric "$hip" rms)"
        reason=$(inert_reason "$(row_param "${ROWS[$row]}")")
        if [ $rclo -ne 0 ] || [ $rchi -ne 0 ]; then
            fail "$what: the two renders of row $row exited $rclo and $rchi ($(tail -1 "$lop"))"
            return
        fi
        if same_file "$lowav" "$higav"; then
            if [ -n "$reason" ]; then
                pass "$what: row $row identical at both ends, as the engine's own code says - $reason"
            else
                fail "$what: row $row ($(row_label "${ROWS[$row]}")) is byte-identical at $low and $high - nothing behind it"
            fi
        elif [ -n "$reason" ]; then
            fail "$what: row $row moves the sound, but config.mk says it cannot ($reason) - the flag is stale"
        else
            pass "$what: row $row moves the sound, $low ($a) -> $high ($b)"
        fi
    }

    pair "atk" "$ATK" 0 "$(row_span "${ROWS[$ATK]}")"
    pair "dec" "$DEC" 0 "$(row_span "${ROWS[$DEC]}")"
    pair "sus" "$SUS" 0 "$(row_span "${ROWS[$SUS]}")"
    # REL is only in the render if the note is released: without --hold dlhost never lets it go, and
    # then no release time can matter. Half the pair's own length in, so both the held part and the
    # tail are in each of these two.
    pair "rel" "$REL" 0 "$(row_span "${ROWS[$REL]}")" \
         --hold "$(awk -v s="$SECONDS_PAIR" 'BEGIN { printf "%.2f", s/2 }')"
    pair "amp" "$AMP" 0 "$(row_span "${ROWS[$AMP]}")"
    if [ "$exp_cutoff" = 1 ] && [ $FLT -ge 0 ]; then
        # Where the engine opens its own filter from a row of its own (LOFI's TONE), close that one
        # first: otherwise the envelope's amount is added to a filter that is already wide open, and
        # the pair comes back identical for a reason that has nothing to do with the FLT row.
        tone=()
        [ $TONE -ge 0 ] && tone=(--raw "$TONE=0")
        pair "flt" "$FLT" 0 "$(row_span "${ROWS[$FLT]}")" ${tone[0]+"${tone[@]}"}
    fi

    TABLE+=("$(printf '%-14s %-6s %-10s %-10s %s' "$name" "$exp_rows" "${peak:-?}" "${rms:-?}" "$exp_name")")
done

# ------------------------------------------------------------------------------- results ---
echo ""
printf '%-14s %-6s %-10s %-10s %s\n' UNIT ROWS PEAK RMS NAME
printf '%-14s %-6s %-10s %-10s %s\n' ---- ---- ---- --- ----
for row in "${TABLE[@]}"; do printf '%s\n' "$row"; done

echo ""
if [ $FAILED -eq 0 ]; then
    printf '%d units: %d checks passed, 0 failed' "${#WANT[@]}" "$PASSED"
    [ $SKIPPED -gt 0 ] && printf ', %d skipped' "$SKIPPED"
    printf '. Renders and reports are in out/dlhost/.\n'
    exit 0
fi
printf '%d units: %d passed, %d FAILED' "${#WANT[@]}" "$PASSED" "$FAILED"
[ $SKIPPED -gt 0 ] && printf ', %d skipped' "$SKIPPED"
printf '.\n' >&2
exit 1
