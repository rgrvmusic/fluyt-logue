# Fluyt on the microKORG2

Fluyt ships Felucca's nine data-free engines as logue units. There are two platforms' worth of
them: `fluyt_*_drumlogue` (up to 20 rows) and `fluyt_*_microkorg2` (13 rows). This file is the
second one's platform notes - what a microKORG2 unit is, what the port restates, and what it
deliberately leaves to the instrument. The panels themselves are described in
[../README.md](../README.md#the-microkorg2-panel); the licensing is in
[LICENSING.md](LICENSING.md).

## What a microKORG2 unit is here

A microKORG2 osc module sits *inside* a voice that already has an EG, a filter and an amp. The
instrument plays the notes, allocates the voices, adds the LFO and the virtual patch, and hands the
module one call per block. So Fluyt's nine units are the instrument's oscillators rather than
instruments of their own:

- the unit's header target is the platform's osc module (`k_unit_module_osc`, which the SDK writes
  as `UNIT_TARGET_PLATFORM | k_unit_module_osc`, and `readelf -x .unit_header` shows as `0x0704`),
  not the synth module a self-contained instrument would use;
- **the instrument's allocator is the one that plays the voices.** Felucca's own `voice_alloc`,
  its voice budget, its steal and its poly/mono/unison stack are not in this path at all: the
  runtime says which of its voices started this block, and that voice *is* Felucca's voice of the
  same number in `trk[0]`;
- the unit has 13 rows, no envelope, and no voice mode (see the README for the walk and the three
  differences).

Nothing of Felucca was changed for any of this. Everything the port needs lives in `shim/`, and the
submodule is read-only in the build, as it is for the drumlogue.

## The call, and the lanes

The osc ABI is one entry point for the whole block:

```c
void unit_render(const float *in, float *out, uint32_t frames);
```

with the runtime's own context hanging off the unit's runtime descriptor
(`microkorg2_desc.hooks.runtime_context`), because that is the only place it is handed in 2.1.0.
What the context says, and what this port does with it:

| The context | What it is here |
| --- | --- |
| `ctxt->trigger` | one bit per voice. It is a **level**, not an edge: a bit that stays high is a voice that is still being held, so the edge (`trigger & ~last_trigger`) is what says a voice *started* this block. `shim/fluyt_microkorg2_unit.c` keeps the previous value for exactly that. |
| `ctxt->pitch[voice]` | the voice's note as a **float MIDI number**, which is where the instrument's bend and its glide reach the engine. The unit follows it once per block (`fluyt_microkorg2_follow`), and it is the only pitch the engine sees: the osc ABI has no bend callback (2.1.0 deprecates `unit_pitch_bend`), and Felucca's own MIDI bend state is not this path's. |
| `ctxt->voiceOffset`, `ctxt->voiceLimit` | the voices this call covers, and `ctxt->outputStride` how a voice's samples step. |
| `GetBufferOffset(ctxt, voice, frames)` | where a voice's lane starts. The voices are packed **four to a group**: the offset is `bufferOffset + (voice >> 2) * (frames << 2)`, and inside a group a voice's first sample is at `+ (voice & 3)`, stepping by `outputStride`. So a frame's run holds four voices, and a block of two groups holds eight. |
| velocity | **not in the context.** It arrives through `unit_note_on(note, velocity)`, so the bridge keeps the velocity each key was started with (and the last velocity it saw) and uses it when the matching voice starts. A key the callbacks did not name plays at 100, the same default Regroove \| RGRV's own units use for a retrigger. |
| note-off | **not in the context either**, and no voice-steal notification. A release arrives by note (`unit_note_off`), and it releases every voice holding that note, the way the drumlogue's own path finds it. |

`unit_gate_on(velocity)` carries a velocity and no note, because on this platform a gate is not what
starts a voice: the trigger bit is. It only ever says how hard, which is what the instrument's own
arpeggiator and sequencer need it for.

Every lane of the slice is written on every call, silence included - the runtime does not clear a
lane for us. A block is any length: Felucca ticks its envelopes, its LFO and its modulation once per
32 samples (a CTL tick), and a block that is not a whole number of ticks carries its remainder into
the next call, exactly as the drumlogue path does.

## One voice per lane, and the one restatement

Felucca's `track_render` (`firmware/src/voice.c:513-605`) loops its eight voices and mixes them into
one part buffer. The microKORG2 has one voice per lane instead, so `shim/fluyt_microkorg2.c`
restates the *body* of that loop - the envelope tick, the `ownenv`/`done` branch, the amp with
velocity and the LFO's AMP destination, glide, the pitch/tune/bend/LFO/envelope modulation, the
cutoff with the accent, the shape, `mod_voice`, and `e->render` - once, for one voice, into that
voice's lane. The per-block prologue (the LFO's fade and the song's TUNE) stays once per block, as
it is in `track_render`.

A restatement is a copy, and a copy drifts, so `tools/microkorg2sim.c` renders every case both ways
and holds the samples to each other - see [the host checks](../README.md#the-host-checks). What the
port does not restate, and does not need to:

- **the engine switch** (`t->xf`, `t->pe_old`, `engine_block`): a unit carries the one engine it was
  built with, so a part never switches and never fades;
- **the voice budget** (`voice_room`, `voice_kill`, the stage-4 give-up): the instrument's allocator
  decides which voice a new note takes, and it does not take one from this unit;
- **the mix** (`fx.c mix_part`'s `LEVEL_Q12`, the pan, the master level): level and pan are on the
  instrument's own panel, one per timbre, so a lane holds the voice and Felucca's master chain alone
  (the DC blocker, the limiter and the soft clip, `fx.c master_out`), with that chain's state kept
  per voice, since a voice is its own part-sized signal here.
- **`P_VOICE` and the mono/legato/unison stack**: a voice is one the instrument started, which makes
  this path always POLY - what `trk_vmode()` answers for `P_VOICE` 0, and what an engine of hits
  wants anyway.

## The envelope is held open

The whole envelope group is off this panel, and Felucca's ADSR is held open in its place, so that
the instrument's own EG is the envelope the player shapes:

| Row | Value | Why |
| --- | --- | --- |
| `P_ATK` | 0 | Felucca's fastest attack (full in about 1.3 ms at 48 kHz) |
| `P_DEC` | 0 | and its fastest decay, so the sustain is reached at once |
| `P_SUS` | 127 | its longest hold: while a key is down the voice is at full |
| `P_REL` | 127 | its slowest release, so the release the instrument's EG plays is the audible one |
| `P_ED_FLT`, `P_ED_PIT`, `P_ED_SHP` | 0 | there is nothing here for the envelope to modulate: the LFO's own PIT is a row of this panel, the filter's is the instrument's to patch, and the engine's shape amount is the LFO's row (`P_LD_SHP`) |

Felucca's envelope still runs (`env_tick`), which is what gives a note-off its shape, but its release
and the instrument's multiply, so the longer of the two is the one still open: with `REL` at 127 the
instrument's EG is what is heard. DRUM (`oneshot`) and FM6 (`ownenv`) never read the track's ADSR
anyway, and both flags still reach `check_rows_microkorg2` and the drumlogue panel's own check, so
the two platforms cannot disagree about the same engine.

The values are written **at the end of every block's drain**, not once at init: a preset carries its
own envelopes and `apply_preset_to` writes them, so a load would otherwise put `ATK`/`DEC`/`SUS`/`REL`
back under a panel that cannot show them. Opening it after the events means the events are the only
thing that can move these rows, and the envelope the instrument plays with is the one this block
renders.

## Presets

Felucca's factory presets are the unit's preset list. The osc ABI has selectors for them
(`unit_get_preset_index`, `unit_get_preset_name`, `unit_load_preset`), so all of an engine's presets
are reachable on the instrument - the same ones the drumlogue panel offers, with the same names.

A preset is applied through the same queue as everything else and lands in the block's drain, then
the envelope group is re-opened (above). So the rule is: **a preset can change everything the panel
can show - the engine's own parameters and the LFO - and nothing else.** That is what the host check
holds every preset to, on every unit.

## Building

```sh
git submodule update --init --depth 1
./build-microkorg2.sh                    # all nine
./build-microkorg2.sh fluyt_fm6_microkorg2
DEBUG=1 ./build-microkorg2.sh            # the SDK's -Og -ggdb3 build
FORCE_TABLES=1 ./build-microkorg2.sh     # regenerate the 48 kHz tables regardless
LOGUE_SDK_PATH=/path/to/logue-sdk ./build-microkorg2.sh
```

The script is `build.sh`'s three steps against the microKORG2's own environment in the same
container image, and takes the same variables:

1. **The tables.** Felucca generates its lookup tables for 44.1 kHz; both of these instruments run at
   48 kHz, so they are regenerated into `shim/gen/` (gitignored). The microKORG2 unit refuses any
   other rate (`unit_init`'s `desc->samplerate != 48000`), because the tables are what fixes it.
2. **The panels.** `tools/gen_rows.c` is told `microkorg2` and writes each unit's `rows.def` and
   `config.mk` out of Felucca's own descriptors. Both are checked in, and are rewritten only when
   something actually changed.
3. **The units.** The SDK's own Makefile (`microkorg2/dummy-osc`'s, unmodified, copied into each unit
   directory) is run in the container with `PROJECT_TYPE := osc`, the platform directory mounted at
   `/workspace`, the unit directory inside it, and the repository mounted read-only at `/fluyt`. The
   `.mk2unit` files are collected into `bin/`.

Felucca is never written to: it is mounted read-only, and `git -C felucca status --porcelain` is
empty after a build.

## Fidelity notes

- **Render order is Felucca's own, and it matters.** The drumlogue renders its eight voices in
  index order; the microKORG2 hands the unit the voices of the block, and the bridge expands them
  into the same index order before rendering. That is not cosmetic: some engines keep state that is
  shared across voices (FORMANT draws its breath noise from one generator for the whole engine), so
  a different order is a different sound. `tools/microkorg2sim.c` hands its voices over in index
  order for the same reason.
- **Felucca's per-engine voice cap is not applied.** The instrument's allocator is the one that
  decides how many voices are playing, up to `kMk2MaxVoices` (8) - which is Felucca's own `NVOICE`,
  so the mapping is one to one. Two engines have a cap of their own: FORMANT's `poly` is 4, and FM6
  has six operator sets per part. The drumlogue reads that cap in its allocator and steals a voice
  when the notes go past it, which is audible (a stolen voice glides from the note it was playing to
  the new one); this platform has no stealing and the unit does not invent one, so the instrument
  simply starts another of its voices. The one cap that still bites is FM6's structural one: its
  seventh and eighth voices have no operator set (`fm6_note_of` returns NULL past `FM6_POLY`), so
  they fall silent rather than reading past the table.
  `tools/microkorg2sim.c` skips a case whose note count is over an engine's cap and says so, so a
  genuine difference is never mistaken for this one.
- **48 kHz, and the three 44.1 kHz constants.** The tables are exact at 48 kHz; three places keep a
  44.1 kHz constant in their own C, which is Felucca's source and not ours to change (NOISE's crush
  rate, FORMANT's coefficients, FM6's envelope table). See
  [the README](../README.md#fidelity-and-the-three-constants-that-stay-at-441-khz).
- **Level and pan are the instrument's.** A lane is the voice through Felucca's master chain alone,
  at `1/32768`. Four voices of a chord therefore sum in the instrument's voice mixer, not in ours:
  peaks of 50% a voice land at 200% of a single lane, and what the player hears is the instrument's
  own volume, amp and effects.
- **No tempo.** `unit_set_tempo` is a no-op: the engine has no clock of its own (the FM-1's
  sequencer, arpeggiator and slicer are not part of a unit), and neither Felucca's LFO nor its
  envelopes are tempo-relative.

## What is not covered here

`shim/fluyt_microkorg2_unit.c` includes the SDK's `unit_osc.h`, whose helpers are the device's own
(NEON), so a host compiler cannot build it: the osc ABI itself is covered by the container build
(`build-microkorg2.sh`) and by the instrument. Everything else - the bridge, the panels, the
restated per-voice render - is covered by `tools/hostcheck-microkorg2.sh`, which needs no container
and no device. ReGINE's `dlhost` loads a drumlogue `.drmlgunit`; a microKORG2 `.mk2unit` is a
different loader's file, and running one outside the instrument is the developer's, not this
repository's.
