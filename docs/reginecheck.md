# Running a Fluyt unit on ARM with ReGINE


The step between the host checks and the device. `tools/hostcheck.sh` builds the units' sources
natively, on whatever machine you are on, and that is how the panel and the sound are checked - but
it cannot run the shipped file, because a `.drmlgunit` is a 32-bit ARM shared object and a desktop
is not. ReGINE is what runs the file itself, on 32-bit ARM, off the device.

[ReGINE](https://rgrv.nl/regine/) is Regroove | RGRV's own logue runtime (it was called NTHost
before it grew past the NTS-1), and it is not part of this repository. Developers of Logue units can
obtain a license, or a user can purchase a hosting runtime. Its drumlogue host is `dlhost` - load the
unit, call `unit_init`, render blocks, write a WAV, report what came out. `dlsynthd` is the same
backend with MIDI in and audio out instead of a WAV, for playing a unit by hand rather than
measuring it.

Nothing here is emulation of the unit: the unit's own ARM code runs on the ARM core, off the
device. What ReGINE stands in for is the machine around it (the host, the clock, the panel's own
writes, the note events), which is exactly the part a unit is not.


## Why it has to be 32-bit ARM

A `.drmlgunit` is an armhf ELF shared object. `src/runtime/drumlogue_abi.h` refuses a 64-bit target
with an `#error`, and CMake probes for that refusal rather than for a list of architectures, so
`dlhost` exists in a build made by a **32-bit ARM compiler** and in no other. On the board that is
the board's own armhf userland; off it, `qemu-arm` with an armhf sysroot. On an x86_64 or arm64
configure the probe fails and the target is simply not added (the rest of the runtime is
unaffected), so a missing `dlhost` in a build tree means the compiler was not armhf, not that
something is broken.

The `regroovelizer` tree's `docs/README-RASPI.md` and `build-raspi.sh` are how that is set up on a
board, and they are what to build for a first run here; `qemu-arm` is how it is done on a desktop,
and it is also what the tree's own build container uses. Either way `dlhost` is one binary to run.


## The developer's licence, in brief

Developers of Logue units that are registered with a `dev_id` can apply for developers license
which grants them a non-transferable right to use the hosting runtime for development purposes,
such as CI/CD. For more information, please use our [contact form](https://rgrv.nl/contact).


## The check

Build the units here first, so `bin/` holds the files being tested:

```sh
./build.sh
```

Then, on the board (or under `qemu-arm -L <armhf sysroot>`), with `<dlhost>` being the built binary
(`build/dlhost`, or `build/dlhost-rgrv` in a Developer Version build; `make install` puts it in the
tree's `bin/`):

```sh
# 1. the panel, as the device reads it: every row, as the unit declares it
<dlhost> --unit bin/fluyt_trio.drmlgunit --info

# 2. the sound: a note, two seconds, a WAV, peak and rms
<dlhost> --unit bin/fluyt_trio.drmlgunit --note 60 --seconds 2 --out out/trio.wav
```

`--info` loads the unit, initialises it, prints its header and stops - no note, no render, no
output. What it prints is the `name` the device will show, the ids, the module, how many presets,
then every row with the `min`, `max`, `init` and `center` the unit declared, and the type name the
device will use to draw it. That is the panel, read out of the shipped file rather than out of
`rows.def`: the file the device loads, answering for itself.

Every run that renders prints a read-backs list as well, after initialising and before the
first block: for each row whose text the unit itself supplies, the value the unit holds and the
text it supplies for it. That is Felucca's own formatting, read through the device's own call - and
it is after any `--param`/`--raw` has been applied, so a read-back is also how a moved row is
confirmed rather than assumed.

A render's report ends in `rendered` (frames, blocks, rate, channels), `peak` (per channel) and
`rms`. For a synth that was sent a note, `peak` at 0.0 is silence and a failure, not a quiet
sound; `peak` reading `nan` means the unit's arithmetic went non-finite and says how many samples
did. A peak above 1.0 is the unit's own output past the ±1.0 the device takes, which `hostsim`
treats as a failure on the host and which is worth the same suspicion here. The exit codes say all
of it without reading the report:

| Exit | What it means |
| --- | --- |
| 0 | loaded, initialised, rendered, finite |
| 1 | usage error |
| 2 | the unit could not be loaded (not a unit, wrong architecture, refused by the licence) |
| 3 | `unit_init` refused the descriptor (the `k_unit_err_*` name is printed) |
| 4 | the WAV could not be written |
| 5 | the output contained NaN or infinity |


### The whole sweep: `tools/reginecheck.sh`

The two commands above are how a single question is asked. Everything they show, asked of all nine
units and checked against the units' own `rows.def` and `config.mk`, is
[`tools/reginecheck.sh`](../tools/reginecheck.sh):

```sh
tools/reginecheck.sh                      # all nine, with dlhost found here
tools/reginecheck.sh fluyt_fm6            # one, by unit or folder name
tools/reginecheck.sh --host user@board    # dlhost is on another machine: ssh, and run it there
```

It needs no arguments to find a local `dlhost` (it looks in the regroovelizer tree's own build
directories), and `DLHOST=` names one outright. Over ssh it copies the units to a temporary
directory on that machine and every render happens there, which is how a Raspberry Pi is used: the
board's own armhf `dlhost`, its own filesystem, nothing transferred back but the WAVs and the
reports. `DLHOST_HOST=` or `--host` is the whole of it - no address is written down anywhere in this
repository. `--seconds` lengthens the one long render; the pairs stay at a fifth of it.

What it checks, and where each one is in the report:

| Check | What it reads |
| --- | --- |
| `loads` | `--info` exits 0. Exit 2 is a file this runtime will not load |
| `name`, `ids`, `module`, `presets`, `rows` | the header, against the unit's own `config.mk` and `rows.def` |
| `panel` | every row's label, type and the numbers it is declared with, against `rows.def` |
| `render` | a note at the unit's own gate note (`FLUYT_GATE_NOTE`): not silent, finite, inside ±1.0 |
| `wav` | the file it wrote is the length its own report says |
| `read-backs` | every row answers with Felucca's own text, at a value inside its declared range |
| `pit` | the LFO's pitch amount answers `-100%` / `0%` / `+98%` at its bottom, its zero and its top |
| `gate` | `--gate` renders the same note as `--note`, to the sample |
| `knobs` | every envelope row, the LFO's amplitude and the envelope's filter amount, at both ends of their range, compared as the two WAVs |

The last one is the point of the whole thing. Peak and rms are too blunt - two different sounds can
agree on both - so the two renders are compared **as files**, byte for byte (on the remote machine
when it is remote, so nothing is transferred to compare). A row that is on the panel and wired to
nothing comes back byte-identical, and that is a failure.

Two pairs are meant to come back identical, and the unit's own `config.mk` says which and why, so
the check reads that rather than keeping its own list:

- **FM6** (`FLUYT_ENGINE_OWNENV=1`): its operator envelopes *are* the voice's amplitude
  (`eng_fm6.c:367`, `.ownenv = 1`), so the track's ADSR is held at full and no ATK, DEC, SUS or REL
  can move it - and with the track envelope pinned at zero, the envelope's `FLT` has nothing to
  scale either. Five rows.
- **DRUM** (`FLUYT_ENGINE_ONESHOT=1`): `drum_amp` takes the ADSR as a parameter named `adsr` and
  never reads it (`eng_drum.c:211-229`, `.oneshot = 1` at `eng_drum.c:301`) - "the hit, not the
  ADSR, which still runs, held at full so a release never ends it". Four rows.

Both keep those rows on the panel because the panel is Felucca's, and Felucca's own editor shows the
envelope group for every engine. The check says so in the pass line rather than hiding it. And the
same flag cuts the other way: a pair that *does* move on a unit whose `config.mk` claims it cannot is
a failure too, because that flag has gone stale.

A third case is not a flag but a row of the engine's own: **LOFI** opens its lowpass from its own
`TONE` (`eng_lofi.c:128`, `lpk = 3000 + (p[P_E7] << 8)`, clamped at 32767), and `TONE`'s default of
127 already saturates that, so the envelope's `FLT` is added to a filter that is already wide open.
`config.mk` carries that row as `FLUYT_ENGINE_TONE`, and the check closes it for the `flt` pair -
without that, the pair comes back identical for a reason that has nothing to do with the `FLT` row.

`REL` needs `--hold` to be reachable at all: a note that is never released has no release in it, and
on these engines the release time then cannot matter. The check releases each `rel` render halfway
through, so both the held part and the tail are in both of the two files.


### What to look for in these nine units

The two together cover the panel: `--info` says how each row is **declared**, the read-backs of a
render say what the unit **answers** for it, and the pair is where Fluyt's own row rule shows. The
rows are declared to the device from zero up (see the README), because a row whose text the unit
formats is held by the runtime in a byte, so a declared negative is one the device cannot keep -
a bug that showed as the whole lower half of a bipolar row reading `+98%`, its top. What that looks
like here, per unit:

- Every row's `min` is `0`, in the header table; a bipolar row's `center` is where Felucca's own
  zero is, so `PIT` is declared `0..127` with `center 64` - and it answers `0%` at rest, `-100%` at
  the bottom and `+98%` at the top. Two ends and a middle, not one value three times.
- TRIO's `INT3` is declared `init 12` and answers `-12st`: Felucca's default of `-12` semitones,
  counted from the bottom of a `-24..24` row. Declared `init` and its read-back agree, or one of the
  two is wrong.
- No row answers `(null)`: every row of these nine is `strings`, so every one of them owes the
  device a text.
- The rows follow the engine, so a unit with a filter shows `FLT` and a unit without one does not:
  WHEEL and DRUM have 17 rows, LOFI and PHASE 19, the other five 20.

| Unit | Rows | Presets |
| --- | --- | --- |
| FLUYT ANALOG | 20 | 12 |
| FLUYT TRIO | 20 | 5 |
| FLUYT PHASE | 19 | 6 |
| FLUYT LOFI | 19 | 5 |
| FLUYT WHEEL | 17 | 5 |
| FLUYT NOISE | 20 | 4 |
| FLUYT FORMANT | 20 | 4 |
| FLUYT DRUM | 17 | 1 |
| FLUYT FM6 | 20 | 8 |

`--note 60` is middle C, and it plays all nine: eight of them are melodic, and on FLUYT DRUM a note
outside the GM kit's 35..81 is wrapped into 36..47 (eng_drum.c), so 60 is the kick - which is also
the note the sequencer's gate plays (`FLUYT_GATE_NOTE`, 36 for that unit and 60 for the other
eight). A silent render on FLUYT DRUM is a failure like any other, then, and not the kit being a
kit.


### Moving a row

Two grammars, and for these units the difference matters:

```sh
<dlhost> --unit bin/fluyt_trio.drmlgunit --note 60 --seconds 2 --param "RES=1023"
<dlhost> --unit bin/fluyt_trio.drmlgunit --note 60 --seconds 2 --raw 9=12
```

- `--param <id>=<v>` takes a **knob position**, `0..1023` (the device's own resolution), scaled into
  the row's declared range.
- `--raw <id>=<v>` writes the declared value through unscaled, which for these units is the row's
  own offset into Felucca's range: `--raw 9=12` is row 10 of TRIO at the top of its lower half,
  which is `INT3` at `-12st`.

`<id>` is an index or a name from the unit's own header. Prefer the index: a panel can carry
Felucca's label twice - `FLT` is the envelope's filter amount on page 4 and the LFO's on page 5 -
and a name then picks the first one.

A row that is on the panel but wired to nothing renders identically at both ends, which is what a
pair of renders catches and a single one cannot:

```sh
# the filter has to be audible: two renders, one at each end of the row
<dlhost> --unit bin/fluyt_analog.drmlgunit --note 60 --seconds 2 --raw 4=0
<dlhost> --unit bin/fluyt_analog.drmlgunit --note 60 --seconds 2 --raw 4=127
```

Row 5 of FLUYT ANALOG is `CUT` (Felucca's cutoff, `0..127`), so those two renders are the filter
closed and open. The same pair on the LFO's `PIT` row (row 17) moves the pitch, and on `AMP` (row
20) the tremolo depth. Comparing the two WAVs byte for byte is the check; peak and rms are too
blunt, since two different sounds can agree on both. A pair that comes back identical is a knob to
verify - unless it is one of the rows above that the engine's own code says cannot respond.


### What has been run

All nine units have been through `tools/reginecheck.sh` against the files in `bin/`, on 32-bit ARM
(a Raspberry Pi 4 running the armhf `dlhost` natively, over ssh), at the 2 second render the script
defaults to. 151 checks passed and none failed: nine loads, nine headers matching their own
`config.mk`, nine panels of 17 to 20 rows each matching `rows.def`, nine note renders (peak 0.34 to
0.57, all finite and inside ±1.0), nine gates agreeing with their note to the sample, and 52 pairs
of renders - every envelope row, the LFO's amplitude and, on the seven engines with a filter, the
envelope's filter amount. The nine `atk`/`dec`/`sus`/`rel` pairs on FLUYT FM6 and FLUYT DRUM are the
ones the `ownenv`/`oneshot` rows above predict, and they came back identical, as did FLUYT FM6's
`flt`.

