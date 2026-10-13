Fluyt
=====

### Felucca's engines, as logue units

[Felucca](https://github.com/hugelton/Felucca) is an open firmware for the M-VAVE FM-1, a small
synthesizer built around a JieLi pi32v2 DSP. Its sound engines are plain integer C, self-contained,
and each one is a different way of 'whistling'. Fluyt takes nine of them and ships each as its own
unit: one for the [KORG drumlogue](https://www.korg.com/us/products/drums/drumlogue/), and one for
the [KORG microKORG2](https://www.korg.com/us/products/synthesizers/microkorg2/) as well.

One engine, one unit, one file to install. Fluyt ANALOG is Felucca's ANALOG engine on the
drumlogue; Fluyt FM6 is its six-operator FM; and so on for the nine. Each is played on the panel,
from the keyboard, or from the drumlogue's sequencer; on the microKORG2 the same nine are
oscillators inside the instrument's own voice, played from its keyboard and shaped by its EG, its
filter and its amp.

- Felucca is a git submodule here and is **never modified**: everything around it is Fluyt's.
- The panel is **generated from Felucca's own parameter tables**, so a label, range or default
  cannot drift from the engine it belongs to.
- The numbers are Felucca's own: the same envelope tables, the same LFO, the same fixed-point DSP,
  rendered at 48 kHz, the rate both of these instruments run at.


#### The units

On the drumlogue, one unit per engine, up to 20 rows over five pages (see [The drumlogue
panel](#the-drumlogue-panel)):

| Unit | Felucca engine | Presets | What it is |
| --- | --- | --- | --- |
| `fluyt_analog_drumlogue` | ANALOG | 12 | Two-oscillator virtual analog with a resonant filter |
| `fluyt_trio_drumlogue` | TRIO | 5 | Three-oscillator (paraphonic) analog |
| `fluyt_phase_drumlogue` | PHASE | 6 | Phase-distortion oscillator |
| `fluyt_lofi_drumlogue` | LOFI | 5 | Aliased, low-bit pulse |
| `fluyt_wheel_drumlogue` | WHEEL | 5 | Tonewheel organ with a rotor |
| `fluyt_noise_drumlogue` | NOISE | 4 | Filtered noise and sample-rate crush |
| `fluyt_formant_drumlogue` | FORMANT | 4 | Formant (vocal) synthesis |
| `fluyt_drum_drumlogue` | DRUM | 1 | The drum kit, General MIDI numbered |
| `fluyt_fm6_drumlogue` | FM6 | 8 | Six-operator FM (an msfa port, Apache-2.0) |

And on the microKORG2, the same nine as osc modules, 13 rows over four pages (see [The microKORG2
panel](#the-microkorg2-panel)):

| Unit | Shown as | Felucca engine | Presets | What it is |
| --- | --- | --- | --- | --- |
| `fluyt_analog_microkorg2` | `F-ANALOG` | ANALOG | 12 | Two-oscillator virtual analog with a resonant filter |
| `fluyt_trio_microkorg2` | `F-TRIO` | TRIO | 5 | Three-oscillator (paraphonic) analog |
| `fluyt_phase_microkorg2` | `F-PHASE` | PHASE | 6 | Phase-distortion oscillator |
| `fluyt_lofi_microkorg2` | `F-LOFI` | LOFI | 5 | Aliased, low-bit pulse |
| `fluyt_wheel_microkorg2` | `F-WHEEL` | WHEEL | 5 | Tonewheel organ with a rotor |
| `fluyt_noise_microkorg2` | `F-NOISE` | NOISE | 4 | Filtered noise and sample-rate crush |
| `fluyt_formant_microkorg2` | `F-FORMNT` | FORMANT | 4 | Formant (vocal) synthesis |
| `fluyt_drum_microkorg2` | `F-DRUM` | DRUM | 1 | The drum kit, General MIDI numbered |
| `fluyt_fm6_microkorg2` | `F-FM6` | FM6 | 8 | Six-operator FM (an msfa port, Apache-2.0) |

The instrument shows eight characters at most, which is why `FLUYT ANALOG` is `F-ANALOG` there; the
drumlogue has room for the long name and uses it. A microKORG2 unit sits inside a voice that
already has an EG, a filter and an amp, so it is an oscillator to the instrument rather than a
synth: the platform's `k_unit_module_osc` target, and pages of four rows like the drumlogue's.

Not here: Felucca's sample-based engines (SAMPLE, GRAIN, SLICE) - they need the FM-1's sample
material - and PHYS, which is a bigger port.

The unit ids are `0x464C5901`..`0x464C5909` ("FLY", 1-9) under the developer id `0x72677276` - "rgrv",
as used across Regroove | RGRV units - on both platforms, and the drumlogue shows them as FLUYT
ANALOG, FLUYT TRIO, and so on.


#### Building

```sh
git submodule update --init --depth 1
./build.sh
```

`build.sh` does three things, and needs `python3`, a C compiler for the host, and `podman` with the
logue SDK container image:

1. Regenerates Felucca's lookup tables at 48 kHz into `shim/gen/` (gitignored). Felucca
   generates them for its own 44.1 kHz; the drumlogue runs at 48 kHz, and everything the tables hold
   - pitch, envelopes, LFO rates, cutoff, levels, decay, the soft clip - would be 8.8% out if they
   were not regenerated. They are regenerated when they are missing, older than the generator, or
   for another rate; `FORCE_TABLES=1 ./build.sh` does it regardless.
2. Regenerates each unit's `rows.def` and `config.mk` from Felucca's own descriptors (see
   below). Both are checked in; they are only rewritten when something actually changed.
3. Builds each unit in the container and copies the `.drmlgunit` into `bin/`.


```sh
./build.sh fluyt_fm6_drumlogue     # just one
DEBUG=1 ./build.sh                 # the SDK's -Og -ggdb3 build
LOGUE_SDK_PATH=/path/to/logue-sdk ./build.sh
```

Install the files in `bin/` the way any drumlogue unit is installed (the drumlogue's own unit
loader), then pick the unit on the device.

The microKORG2 units build the same way, in the same container image and out of the same `shim/`,
the same Felucca and the same 48 kHz tables:

```sh
./build-microkorg2.sh                    # all nine
./build-microkorg2.sh fluyt_fm6_microkorg2
```

`build-microkorg2.sh` is those three steps run against the microKORG2's own environment in the
image, and takes the same `LOGUE_SDK_PATH`, `DEBUG` and `FORCE_TABLES`. What differs is where the
SDK's Makefile is run from (the platform's own directory), the panels it asks for (`gen_rows.c`,
told `microkorg2`), and where the artifacts land: `bin/*.mk2unit`, beside the drumlogue's
`*.drmlgunit` and never over them. Felucca is mounted read-only throughout, as it is in `build.sh`.

Install those the way any microKORG2 unit is installed: connect the instrument over USB, hold the
first option button while turning it on to put it in USB mass storage mode, and copy the `.mk2unit`
into a `SlotN` folder under `Units/Oscs` on the drive it appears as. Then pick the oscillator - it is
in the list as `F-ANALOG`, `F-TRIO`, and so on. `readelf -x .unit_header bin/fluyt_analog.mk2unit`
shows what the instrument reads: the name, the osc target, the developer id, and the row count.


#### The host checks

No container, no device:

```sh
tools/hostcheck.sh
```

Two programs are built per unit, out of that unit's own sources and its own `config.mk`:

- `check_rows` holds the panel to Felucca's own parameter descriptors - every row's label, range
  and default, the row as the unit header declares it to the device, no parameter on the panel twice,
  the envelope first, and no row this engine has nothing behind (a filter row on a unit whose engine
  renders no filter, a row the panel is meant to leave off) - and moves every row at its lowest, its
  default and its highest value, checking the parameter Felucca's engine holds and the text the
  device is shown after each. It prints the panel the way the device will show it, with Felucca's own
  formatting of each row at its lowest, its default and its highest value. This is the quickest way to
  see that a unit is the engine it claims to be.
- `hostsim` is a drumlogue that is a host program: it loads every preset, plays a note the way
  the sequencer does (a gate on the engine's own note), at block sizes the runtime may ask for
  (including ones that are not a multiple of Felucca's 32-sample control block), and reports the peak
  and RMS of what came out - so silence, a NaN, or a sample outside ±1.0 is caught before the device
  is involved. It also exercises the panel's own path: a row set from the control thread, a preset
  load, bend, channel pressure, the sequencer's gate, and all-notes-off.

`hostsim` is built with `-fsanitize=address,undefined` where those runtimes are installed (Fedora:
`dnf install libasan libubsan`); where they are not, the checks still run and the script says so.
Felucca's own build uses neither sanitizers nor `-fwrapv`, so this is the only place its fixed-point
arithmetic is checked rather than trusted.

The microKORG2 units have their own pair, because their panel is a different panel and their render
is a different call:

```sh
tools/hostcheck-microkorg2.sh
```

- `check_rows_microkorg2` is `check_rows` for the osc panel: the 13 slots, no parameter on the
  panel twice, no envelope row, the engine's own four HOME knobs first, Felucca's own text at both
  ends of every row, the declaration the instrument reads counted from zero, and the envelope the
  bridge holds open - checked for every preset of the engine as well, since a preset carries its own
  ADSR and the panel cannot reach it to put it back. It prints the panel the way the instrument will
  show it.
- `microkorg2sim` is a microKORG2 that is a host program: it renders every case **twice**, once
  through Felucca's own `track_render` with the drumlogue's block sizes and once through the
  microKORG2's per-voice call, each voice into its own lane at the stride the runtime's layout uses,
  and holds the two to each other - the same samples, not similar ones. It is the check that the
  restated per-voice loop (`shim/fluyt_microkorg2.c` restates voice.c:546-598) is still Felucca's.
  Around that: every lane written including the silent ones, nothing outside ±1.0, block sizes the
  runtime may actually ask for (1, 7, 13, 33, 74, 128 frames - it is not obliged to hand whole
  control ticks), and every preset played.

All of these checks build the units' sources here. The shipped `.drmlgunit` - the file the
drumlogue loads, 32-bit ARM - is a different thing to run, and that is what
[docs/reginecheck.md](docs/reginecheck.md) covers: ReGINE, Regroove | RGRV's own logue runtime,
loads the built file itself and renders it, so the file that reaches the device has run before. It
is the drumlogue's file: ReGINE's `dlhost` is a drumlogue host, and a microKORG2 `.mk2unit` is a
different loader's file (see [docs/microkorg2.md](docs/microkorg2.md)).

```sh
tools/reginecheck.sh                      # all nine, wherever dlhost runs
tools/reginecheck.sh --host user@board    # ... or on another machine over ssh
```

That script drives ReGINE's `dlhost`, so it needs a licensed runtime and a 32-bit ARM to run it on
(see the doc). What it adds over the two host checks is everything on the drumlogue's own side of
the unit: the header the device reads, the text the unit answers for each row, the sequencer's gate,
and every row rendered at both ends of its range and compared - so a knob that is on the panel and
wired to nothing is caught. Two pairs are expected to come back identical and the unit's own
`config.mk` says which and why: FM6's operator envelopes are its amplitude (`ownenv`) and DRUM's hit
is (`oneshot`), so neither reads the track's ADSR.


#### The drumlogue panel

A drumlogue unit declares at most 24 parameters, and the device shows them four to a page. Fluyt's
panel is this engine's own sound: the envelope first, then the engine, then the voice mode and the
LFO.

| Page | Rows |
| --- | --- |
| 1 | ATK · DEC · SUS · REL |
| 2-3 | the engine's own four HOME knobs, then its remaining `P_E0..P_E7` |
| 4 | the envelope's filter amount (FLT), the voice mode (VCE), the LFO's rate and wave |
| 5 | where the LFO goes: PIT · FLT · SHP · AMP |

20 rows for ANALOG, TRIO, NOISE, FORMANT and FM6; 19 for PHASE and LOFI; 17 for WHEEL and DRUM. The
count follows the engine: PHASE's eighth parameter is one of Felucca's inert rows (a label of `-` and
no range), WHEEL and DRUM render no filter, so they have no FLT row and no LFO filter amount, LOFI is
not shaped, so it has no SHP amount, and where one of the engine's four HOME knobs is a parameter the
walk would show again (ANALOG's cutoff, say) the panel closes up rather than showing it twice.

What is not on the panel is the FM-1 around the engine. Its level and pan are the drumlogue's own -
the unit renders into its part's volume and pan - and its mute is the sequencer's, so those three
were three of the 24 slots spent on knobs the device already has; glide, the LFO's phase and fade, and
the envelope's pitch and shape amounts lost their slot to the LFO's four destinations. That is the
whole rule: a row is a knob this engine's sound has room for.

Almost every row is `k_unit_param_type_strings`, which means the device asks the unit how to write
the value and gets **Felucca's own formatting** - `SAW`, `-4.0dB`, `2.1ms`, `+98%` - rather than a
number whose unit the device would have to guess. Every row of all nine panels is in fact, because
the rows the device draws better itself (a true percentage, a switch) were the mix rows this panel
leaves off; the generator still knows how to ask for one. The labels are Felucca's too, which is why
the LFO page reads `PIT` and `FLT` and the page before it reads `FLT` and `VCE`: the FM-1 shows a
group at a time, and so does the device.

Every row's range, default and label comes from Felucca's `params.c` (`TP[]`) and the engine's own
`edit[]` table - the very tables the FM-1's display reads - so there is nothing to invent and nothing
to keep in step by hand.

A row of that kind is declared to the device from zero up, with the row's own zero in the middle
where Felucca's is: `PIT` is declared `0..127` with its center at 64, not `-64..63`. The runtime
holds a row whose text the unit supplies in a byte, so a row declared below zero is one it cannot
hold - the value comes back wrapped (`-1` as `255`), which reads as the row's *maximum*: the whole
lower half of such a row shows its top, and its own negative default shows the same, so `PIT` read
`+98%` below zero and TRIO's `INT3`, whose default is `-12st`, read `+24st` at rest. The panel still
shows Felucca's own numbers - `-100%` .. `0%` .. `+98%`, `-24st` .. `+24st` - because the bridge
translates between the row the device holds and the parameter Felucca has: `rows.def` keeps Felucca's
range, the header declares it counted from zero, and `tools/check_rows.c` holds both to Felucca's
descriptor, moving every row at its lowest, its default and its highest value and reading the
parameter and the text back.


#### The microKORG2 panel

A microKORG2 osc module declares at most 13 parameters, and the instrument shows them four to a
page. It is the same walk, the same generator and the same rule about zero; there is simply less of
it, because this instrument's voice already has an EG, a filter and an amp around the module:

| Rows | What |
| --- | --- |
| 1-8 | the engine: up to eight, its own four HOME knobs where the walk shows them first, then the rest of `P_E0..P_E7` |
| 9-13 | the LFO: RATE · WAVE · PIT, then the engine's shape amount (SHP) where it has one, then its amplitude amount (AMP) |

13 rows for ANALOG, TRIO, NOISE, FORMANT and FM6 - the engine's eight, then RATE, WAVE, PIT, SHP and
AMP. 12 for PHASE, LOFI, WHEEL and DRUM, which have one row less of the engine or one destination
less of the LFO: PHASE's eighth parameter is one of Felucca's inert rows (a label of `-` and no
range), and LOFI, WHEEL and DRUM have no shape amount to reach (WHEEL and DRUM render no filter
either, so nothing here reads their cutoff - neither panel has a filter row for them). As on the
drumlogue, where a HOME knob is a parameter the walk would show again (ANALOG's cutoff, say) the
panel closes up rather than showing it twice.

What the drumlogue panel has and this one does not:

- **The envelope.** `ATK`, `DEC`, `SUS`, `REL` and the three amounts it feeds (the filter, the
  pitch, the shape) are gone. The instrument has an EG of its own and that is the envelope this
  sound should be shaped by, so Felucca's is **held open** instead: attack 0, decay 0, sustain and
  release full, the three amounts 0 - written at the end of every block's drain, after that block's
  events. A preset carries its own envelopes and `apply_preset_to` writes them, so it is re-opened
  after a preset load too: a preset can change everything the panel can show, and nothing else.
- **The voice mode.** On the drumlogue, `VCE` (POLY/UNISON/MONO) is the unit's own decision, because
  a drumlogue unit renders its part itself. On the microKORG2 the instrument's allocator plays the
  voices and hands the module up to eight of them, one per lane - device voice *i* is Felucca's
  voice *i* of `trk[0]` - so there is nothing left for a row to decide.
- **The LFO's filter amount (`FLT`).** On the drumlogue the panel is the only thing that can move
  the cutoff, so the LFO needs a row that reaches it. On the microKORG2 the instrument's own LFO
  reaches this unit's `CUT` row through the virtual patch, and the one modulation the instrument
  cannot reach is the engine's internal shape amount - which is why `SHP` stays and `FLT` goes.

Two things stay the same, and are worth saying because they are the ones a port usually breaks:
level and pan are still the instrument's own (there is no mix row on either panel, and the lane
holds the voice through Felucca's master chain and nothing of ours), and a row the unit formats
itself is still declared from zero up - the runtime holds it in a byte on this platform too, so the
whole lower half of a bipolar row would otherwise read as its maximum.

The instrument's allocator is not the drumlogue's, and two engines have a cap of their own:
FORMANT's `poly` is 4, and FM6 has six operator sets per part. What the port does about that is let
the instrument decide and stay correct at the edges: FM6's seventh and eighth voices have no
operator set and fall silent rather than reading past one, and nothing here reads a voice that was
never started. [docs/microkorg2.md](docs/microkorg2.md) has the details.


#### How it fits together

```
felucca/                     the submodule, pristine (v1.0.2)
shim/
  fluyt_engine.c             the unit's single translation unit: Felucca's files, compiled as they
                             are, plus the bridge (events, the panel, the mix, the float output)
  fluyt_unit.c               the drumlogue's unit_* entry points, one line each over the bridge
  fluyt_header.c             the unit header (name, id, the panel's rows), built from rows.def
  fluyt_microkorg2.c         the microKORG2's render: the per-voice body of Felucca's track_render,
                             restated once for one voice per lane (it names the voice.c lines)
  fluyt_microkorg2_unit.c    the osc ABI entry points (the microKORG2's unit_render/note_on/...)
  fluyt_microkorg2_header.c  the osc unit header: 13 rows, the osc target, the preset count
  fluyt_engines.c            the engine table Felucca's engines.c would have supplied
  fluyt_preset.c             loading a preset, and the state a track starts in (from ui.c)
  fluyt_queue.h              the one-producer ring between the control thread and the audio thread
  fluyt_fmt.h                the four formatters Felucca's libc.c supplies (it is not compiled in)
  gen/                       the 48 kHz tables, generated by build.sh (gitignored)
fluyt_*_drumlogue/           nine units: the SDK's Makefile, a generated config.mk and rows.def
fluyt_*_microkorg2/          the same nine for the microKORG2 (the SDK's dummy-osc Makefile)
tools/gen_rows.c             writes those rows.def/config.mk out of Felucca's descriptors, for
                             either platform (`drumlogue` or `microkorg2`)
tools/check_rows.c           the host check of the drumlogue panel
tools/hostsim.c              the host check of the sound (platform-neutral, so it is shared)
tools/hostcheck.sh           builds and runs both, for all nine
tools/check_rows_microkorg2.c  the host check of the microKORG2 panel
tools/microkorg2sim.c        a microKORG2 that is a host program: the osc block against track_render
tools/hostcheck-microkorg2.sh  builds and runs both, for all nine
tools/reginecheck.sh         runs the built units through ReGINE's dlhost (ARM)
build.sh                     tables -> panels -> the container build
build-microkorg2.sh          the same, against the microKORG2's platform directory
docs/LICENSING.md            what is whose: Felucca, the SDK, these units
docs/reginecheck.md          running a built unit on ARM, with ReGINE
docs/microkorg2.md           the microKORG2 platform: the lanes, the open envelope, the build
```

Felucca is one translation unit (`firmware/src/felucca.c` includes its files in order), and so is a
Fluyt unit: the tables, `core.h`, `dsp.c`, the engine, `params.c`, `mod.c`, `voice.c`, then the
bridge. What is not here is the FM-1 around those files - its UI, sequencer, arpeggiator, MIDI,
storage, mixer and effects. A drumlogue unit is one part that a keyboard plays, and that is what the
bridge renders.

Notes, rows, presets and bend arrive from the drumlogue's control thread and are queued; the audio
thread drains the queue at the head of a block and applies them. Felucca's voices are not writable
from two threads at once, so nothing else crosses.

The microKORG2's side of the bridge is the same queue, the same rows and the same presets; what is
different is what a render call is. `unit_render` is called once per block over the voices the
runtime says it covers, and each voice has its own buffer in that block, so the bridge renders one
voice per lane instead of eight voices into one part - which is the one place the two platforms'
code differs at all, the eight lines at the end of `shim/fluyt_engine.c` that name the target and
include the second render instead of the first.


#### Fidelity, and the three constants that stay at 44.1 kHz

Everything the generated tables drive is exact at 48 kHz. Three places have a 44.1 kHz constant
written into their own C, which is Felucca's source and is not ours to change; at 48 kHz - the rate
both of these instruments run at - those places run about 8.8% off:

| Where | What |
| --- | --- |
| `firmware/src/eng_noise.c:148` | the sample-rate crush rate (`97391u` ≈ 65536/44100) |
| `firmware/src/eng_formant.c:73-74` | the formant coefficients (`3044`, `4781`) |
| `firmware/src/fm6_core.c:89` | msfa's `ACCURATE_ENVELOPE` envelope table, indexed at 44.1 kHz |

So: FLUYT NOISE's crush is 8.8% coarser or finer than the FM-1's, FLUYT FORMANT's vowels sit about a
semitone high, and FLUYT FM6's envelopes are 8.8% faster. Everything else - pitch, the ADSR, the
LFO, cutoff, levels, the drum decays, the soft clip - is the FM-1's, at the rate the instrument
runs at.

Two smaller notes:

- Master level. A drumlogue unit's output is Felucca's own mix (its `LEVEL_Q12` table, pan, its DC
  blocker, limiter and soft clip), converted to the float the drumlogue takes with an exact
  `1/32768`. A preset peaks around 0.3-0.6 through the panel's default LVL; the soft clip bounds it
  just under 1.0. `tools/hostsim.sh` prints the measured peaks. A microKORG2 unit has no level or
  pan of its own - the instrument's are the instrument's - so a lane is the voice through the
  master chain alone, and the four voices of a chord sum where the instrument sums them: peaks of
  50% a voice land at 200% of one lane, and the instrument's own volume, amp and effects are what
  the player hears. `tools/microkorg2sim.c` prints both numbers.
- Pitch bend is ±2 semitones, written into Felucca's own MIDI state the way its `midi_control.c`
  writes it. On the microKORG2 the instrument's bend and glide reach the unit as the float pitch of
  each voice, so that path is the instrument's.


#### Why Fluyt?

The name is symbolic, as fluyt is a kind of Dutch vessel, mostly used for trade during the 16th
century. Just like in the old days, I hope this 'trading' will happen between the communities. And,
the word also means whistle.


#### Licence

Fluyt is **GPL-3.0-only**, like Felucca. Felucca is a submodule, unmodified, at v1.0.2:
Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments. `fm6_core.c` is a port of
msfa/Dexed (Apache-2.0). See [docs/LICENSING.md](docs/LICENSING.md).

The build scripts (`build.sh`, `build-microkorg2.sh`, `tools/hostcheck.sh`,
`tools/hostcheck-microkorg2.sh`, `tools/reginecheck.sh`) are the exception: they
come from Regroove | RGRV's own logue build scripts and are **MIT** licensed. Each unit's `Makefile`
is the logue SDK's, unmodified (the microKORG2 units use the SDK's `dummy-osc` Makefile), and are BSD
3-Clause (KORG INC.). See
[docs/LICENSING.md](docs/LICENSING.md) and `LICENSES/`.


#### Support

Support development by becoming a [Patron](https://patreon.rgrv.nl/) or buying me a [Ko-fi](https://kofi.rgrv.nl).
