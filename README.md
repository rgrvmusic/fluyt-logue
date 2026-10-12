Fluyt
=====

### Felucca's engines, as drumlogue units

[Felucca](https://github.com/hugelton/Felucca) is an open firmware for the M-VAVE FM-1, a small
synthesizer built around a JieLi pi32v2 DSP. Its sound engines are plain integer C, self-contained,
and each one is a different way of 'whistling'. Fluyt takes nine of them and ships each as its own
[KORG drumlogue](https://www.korg.com/us/products/drums/drumlogue/) unit.

One engine, one unit, one file to install. Fluyt ANALOG is Felucca's ANALOG engine on the
drumlogue; Fluyt FM6 is its six-operator FM; and so on for the nine. Each is played on the panel,
from the keyboard, or from the drumlogue's sequencer.

- Felucca is a git submodule here and is **never modified**: everything around it is Fluyt's.
- The panel is **generated from Felucca's own parameter tables**, so a label, range or default
  cannot drift from the engine it belongs to.
- The numbers are Felucca's own: the same envelope tables, the same LFO, the same fixed-point DSP,
  rendered at the drumlogue's 48 kHz.


#### The units

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

Not here: Felucca's sample-based engines (SAMPLE, GRAIN, SLICE) - they need the FM-1's sample
material - and PHYS, which is a bigger port.

The unit ids are `0x464C5901`..`0x464C5909` ("FLY", 1-9) under the developer id `0x72677276` - "rgrv",
as used across Regroove | RGRV units - and the device shows them as FLUYT ANALOG, FLUYT TRIO, and so
on.


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


#### The host checks

No container, no device:

```sh
tools/hostcheck.sh
```

Two programs are built per unit, out of that unit's own sources and its own `config.mk`:

- **`check_rows`** holds the panel to Felucca's own parameter descriptors - every row's label, range
  and default, the row as the unit header declares it to the device, no parameter on the panel twice,
  the envelope first - and moves every row at its lowest, its default and its highest value, checking
  the parameter Felucca's engine holds and the text the device is shown after each. It prints the
  panel the way the device will show it, with Felucca's own formatting of each row at its lowest, its
  default and its highest value. This is the quickest way to see that a unit is the engine it claims
  to be.
- **`hostsim`** is a drumlogue that is a host program: it loads every preset, plays a note the way
  the sequencer does (a gate on the engine's own note), at block sizes the runtime may ask for
  (including ones that are not a multiple of Felucca's 32-sample control block), and reports the peak
  and RMS of what came out - so silence, a NaN, or a sample outside ±1.0 is caught before the device
  is involved. It also exercises the panel's own path: a row set from the control thread, a preset
  load, bend, channel pressure, the sequencer's gate, and all-notes-off.

`hostsim` is built with `-fsanitize=address,undefined` where those runtimes are installed (Fedora:
`dnf install libasan libubsan`); where they are not, the checks still run and the script says so.
Felucca's own build uses neither sanitizers nor `-fwrapv`, so this is the only place its fixed-point
arithmetic is checked rather than trusted.


#### The panel

A drumlogue unit declares at most 24 parameters, and the device shows them four to a page. Fluyt's
panel is envelope-first and each parameter appears once:

| Page | Rows |
| --- | --- |
| 1 | ATK · DEC · SUS · REL |
| 2-3 | the engine's own four HOME knobs, then its eight `P_E0..P_E7` |
| 4 | the filter and pitch envelope amounts, then the LFO's rate and shape |
| 5 | LVL · PAN · VOICE · GLIDE |
| 6 | MUTE, then the LFO's shape, phase, fade and its destinations |

The order is a walk with two rules - the envelope first, and a parameter that has been shown is not
shown again - so a knob that is already a row (ANALOG's cutoff, say) closes the panel up rather than
appearing twice, and the rows that fall off the end are the ones a player is least likely to miss.
The 24th row of ANALOG is therefore FADE, where a unit with fewer engine knobs would still be
showing an LFO destination.

Almost every row is `k_unit_param_type_strings`, which means the device asks the unit how to write
the value and gets **Felucca's own formatting** - `SAW`, `-4.0dB`, `2.1ms`, `+98%` - rather than a
number whose unit the device would have to guess. The two exceptions are the switch (MUTE) and a
parameter that is a true percentage.

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


#### How it fits together

```
felucca/                     the submodule, pristine (v1.0.2)
shim/
  fluyt_engine.c             the unit's single translation unit: Felucca's files, compiled as they
                             are, plus the bridge (events, the panel, the mix, the float output)
  fluyt_unit.c               the drumlogue's unit_* entry points, one line each over the bridge
  fluyt_header.c             the unit header (name, id, the 24 rows), built from rows.def
  fluyt_engines.c            the engine table Felucca's engines.c would have supplied
  fluyt_preset.c             loading a preset, and the state a track starts in (from ui.c)
  fluyt_queue.h              the one-producer ring between the control thread and the audio thread
  fluyt_fmt.h                the four formatters Felucca's libc.c supplies (it is not compiled in)
  gen/                       the 48 kHz tables, generated by build.sh (gitignored)
fluyt_*_drumlogue/           nine units: the SDK's Makefile, a generated config.mk and rows.def
tools/gen_rows.c             writes those rows.def/config.mk out of Felucca's descriptors
tools/check_rows.c           the host check of the panel
tools/hostsim.c              the host check of the sound
tools/hostcheck.sh           builds and runs both, for all nine
build.sh                     tables -> panels -> the container build
```

Felucca is one translation unit (`firmware/src/felucca.c` includes its files in order), and so is a
Fluyt unit: the tables, `core.h`, `dsp.c`, the engine, `params.c`, `mod.c`, `voice.c`, then the
bridge. What is not here is the FM-1 around those files - its UI, sequencer, arpeggiator, MIDI,
storage, mixer and effects. A drumlogue unit is one part that a keyboard plays, and that is what the
bridge renders.

Notes, rows, presets and bend arrive from the drumlogue's control thread and are queued; the audio
thread drains the queue at the head of a block and applies them. Felucca's voices are not writable
from two threads at once, so nothing else crosses.


#### Fidelity, and the three constants that stay at 44.1 kHz

Everything the generated tables drive is exact at 48 kHz. Three places have a 44.1 kHz constant
written into their own C, which is Felucca's source and is not ours to change; at the drumlogue's
48 kHz those places run about 8.8% off:

| Where | What |
| --- | --- |
| `firmware/src/eng_noise.c:148` | the sample-rate crush rate (`97391u` ≈ 65536/44100) |
| `firmware/src/eng_formant.c:73-74` | the formant coefficients (`3044`, `4781`) |
| `firmware/src/fm6_core.c:89` | msfa's `ACCURATE_ENVELOPE` envelope table, indexed at 44.1 kHz |

So: FLUYT NOISE's crush is 8.8% coarser or finer than the FM-1's, FLUYT FORMANT's vowels sit about a
semitone high, and FLUYT FM6's envelopes are 8.8% faster. Everything else - pitch, the ADSR, the
LFO, cutoff, levels, the drum decays, the soft clip - is the FM-1's, at the drumlogue's rate.

Two smaller notes:

- Master level. The output is Felucca's own mix (its `LEVEL_Q12` table, pan, its DC blocker,
  limiter and soft clip), converted to the float the drumlogue takes with an exact `1/32768`. A
  preset peaks around 0.3-0.6 through the panel's default LVL; the soft clip bounds it just under
  1.0. `tools/hostsim.sh` prints the measured peaks.
- Pitch bend is ±2 semitones, written into Felucca's own MIDI state the way its `midi_control.c`
  writes it.


#### Why Fluyt?

The name is symbolic, as fluyt is a kind of Dutch vessel, mostly used for trade during the 16th
century. A famous fluyt vessel, Duyfken discovered Australia, ... the undiscovered country. But the
word also means whistle. 


#### Licence

Fluyt is **GPL-3.0-only**, like Felucca. Felucca is a submodule, unmodified, at v1.0.2:
Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments. `fm6_core.c` is a port of
msfa/Dexed (Apache-2.0). See [LICENSING.md](LICENSING.md).

The build scripts (`build.sh`, `tools/hostcheck.sh`) are the exception: they come from Regroove |
RGRV's own logue build scripts and are **MIT** licensed. Each unit's `Makefile` is the
logue SDK's, unmodified, and are BSD 3-Clause (KORG INC.). See [LICENSING.md](LICENSING.md) and
`LICENSES/`.


#### Support

Support development by becoming a [Patron](https://patreon.rgrv.nl/) or buying me a [Ko-fi](https://kofi.rgrv.nl).
