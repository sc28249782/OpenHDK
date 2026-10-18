# HandyKaraoke and SoundCraft implementation guidance

## Status and authority

**Status:** Proposed implementation sequence, documented on 2026-10-18.
This source study adds no runtime feature, accepted detailed API or dependency.
[SPECIFICATION.md](SPECIFICATION.md) defines implemented behavior.
[ROADMAP.md](ROADMAP.md) keeps milestone order and existing acceptance gates.
The 0.3.0 library, NCN and application evidence work remains ahead of these
mixer extensions. Historical acceptance records and released contracts do not
advance through this document.

Use HandyKaraoke to identify karaoke workflows. Use SoundCraft to study DSP
building blocks and tests. Use [MIXER-REFERENCE.md](MIXER-REFERENCE.md) for the
separate pinned Buai routing comparison. None defines OpenHDK behavior by itself.

## Pinned evidence

- HandyKaraoke: [source snapshot](https://github.com/sc28249782/HandyKaraoke/tree/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8),
  `7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8`.
- SoundCraft: [source snapshot](https://github.com/storytold/soundcraft/tree/c1bf044a85b55e9aa083624c39fabddbb08dadeb),
  `c1bf044a85b55e9aa083624c39fabddbb08dadeb`; Cargo.toml declares 0.9.0.
- OpenHDK comparison base: `93424bf63a39c153dcf74a66c6f1d76b3fe210d8`.

This review inspected source. It did not build either reference, run their
tests, compare their audio, measure CPU/RAM/latency or audit all dependencies.
Counts below describe the pinned legacy model, not required OpenHDK capacity.

HandyKaraoke source paths at the pinned revision:

| Path | Inspected evidence |
| --- | --- |
| [MidiPlayer.h](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Midi/MidiPlayer.h), [MidiPlayer.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Midi/MidiPlayer.cpp) | Sixteen channel controls, CC7 lock, transpose, drum/snare/bass locks and next-medley preparation. |
| [MidiSynthesizer.h](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Midi/MidiSynthesizer.h), [MidiSynthesizer.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Midi/MidiSynthesizer.cpp), [MidiHelper.h](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Midi/MidiHelper.h) | 26 melodic groups, 16 drum/percussion groups, four VSTi slots and 16 buses; multiple SoundFonts, per-font volume and 11 mapping configurations. |
| [ChannelMixer.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Widgets/ChannelMixer.cpp), [SynthMixerDialog.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Dialogs/SynthMixerDialog.cpp) | Channel and instrument/bus displays receive note velocity for their activity meters. |
| [Equalizer31BandFX.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/BASSFX/Equalizer31BandFX.cpp), [Reverb2FX.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/BASSFX/Reverb2FX.cpp) | BASS-coupled effect wrappers; EQ31 uses fixed centers from 20 Hz to 20 kHz and bandwidth 0.3333 octave. |
| [LyricsWidget.h](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Widgets/LyricsWidget.h), [MedleyLoader.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/MedleyLoader.cpp) | Lyric seek/cursor and temporary next-song lyric state. These are workflow observations, not accepted timing rules. |

SoundCraft source paths and qualifications are recorded in the
[2026-10-18 follow-up](SOUNDCRAFT-REFERENCE.md#2026-10-18-follow-up-source-review).

## Capability comparison and disposition

| Capability | HandyKaraoke reference | SoundCraft contribution | OpenHDK baseline and next decision |
| --- | --- | --- | --- |
| MIDI channel controls | Gain/controller controls, pan, mute/solo, program and CC7 lock. | Parameter descriptors and smoothing patterns. | Runtime gain, mute/solo and flags-only presets already exist. Specify any lock, pan or program override separately; preserve source CC7/CC11. |
| Instrument and drum groups | Routes melodic families and drum-note groups to separate synthesis streams. | Prepared strips and mixer graph concepts. | Grouping and independent PCM outputs are future work. Prove synth output isolation before per-group DSP. |
| Buses | Sixteen legacy bus slots with device/speaker destinations. | Inserts, sends, returns and latency alignment. | Start with a small bounded graph. Do not adopt the legacy count or stream handles as a public model. |
| SoundFonts | Multiple fonts, per-font gain, GM-program mapping, drum-group mapping and 11 configurations. | The inspected sampler plays one sample across keys with 32 voices. | Keep FluidSynth/miniaudio. Define resource identity, selection precedence, missing-resource errors and lifecycle in 0.4.0. The sampler is not an SF2-engine replacement. |
| EQ | Graphic 15/31-band wrappers and master EQ31. | Biquad and parametric EQ 1/7-band implementation. | Parametric EQ first in 0.6.0. A graphic EQ needs its own topology, bandwidth, bounds and tests; seven bands do not reproduce 31-band behavior. |
| Dynamics and effects | BASS-backed compressor, chorus, reverb, echo and other effects. | DSP implementations, smoothing, latency/tail queries and synthetic tests. | Preserve the existing 0.98 linked stereo safety limiter. Add processors in separate slices. Equal names/parameter values do not establish equal sound. |
| Meters | The inspected mixer displays use note velocity. | PCM peak/hold/clip, RMS, loudness and true-peak analysis. | Propose a small master PCM meter first. Label MIDI activity separately; it cannot prove dBFS level or clipping. |
| Karaoke controls | Transpose, speed and drum/snare/bass locks; medley preparation. | MIDI editing helpers and transport concepts. | Preserve current parser/timeline and playback contracts. Overrides need separate policies, note ownership and cleanup tests. |
| Lyrics and library | Lyric cursor/seek, playlist and next-song state. | DAW architecture offers no direct karaoke replacement here. | Continue accepted OpenHDK library/lyric contracts and evidence gates. No legacy database, parser or UI import is selected. |
| Outputs and plugins | Speaker/device routing, VSTi slots and physical MIDI. | Multichannel routing and hosted-plugin architecture. | Follow 0.7.0 single-endpoint routing first. Multiple device clocks, hardware MIDI and isolated VST3 feasibility remain deferred. No Qt, BASS or VST2 import. |

## Boundaries to specify before code

Keep three control domains separate:

1. Source MIDI controllers, including CC7/CC11 and their reset semantics.
2. Runtime user controls, including the current channel gain and mute/solo.
3. Future PCM strip, bus and master gain after synthesis.

Legacy CC7 lock is a proposed override policy, not another name for current
runtime gain. A future override must leave immutable events and diagnostics
unchanged. Define release/reset behavior and interaction with song automation.
Existing flags-only presets must not silently acquire gains or new policies.
Apply master gain once; migration from synth gain needs an explicit rule.

A mixed stereo signal cannot be split into instrument stems by adding labels.
Evaluate backend output capabilities and a bounded number of synthesis
instances. Record CPU/RAM limits, controller fan-out, drum-note classification,
SoundFont sharing and effect-return ownership. Do not choose one synth per
legacy strip without measurement. Start with melodic audio, kick, snare and
remaining percussion only if the reviewed adapter can isolate them.

Note-off must reach the voice destination selected at note-on. Cover overlapping
notes, program/classifier changes, sustain release, reset, stop/seek, route
changes and preparation failure. Define whether changes require stop/reprepare.
Do not promise seamless live routing or live SoundFont replacement.

For transpose, distinguish MIDI-note transformation from PCM pitch shifting.
The legacy implementation exempts channel index 9 (user channel 10). OpenHDK
needs an explicit drum-role and note-range policy before offering this control.
Do not infer that every MIDI source has the same drum layout. Use the accepted
timeline clock for tempo changes; a DSP effect must not redefine lyric time.

## Proposed slices and acceptance gates

These are reviewable proposals within the roadmap milestones. Each slice needs
an accepted detailed contract before implementation. They do not reorder 0.3.0.

| Slice | Milestone | Reviewable result | Evidence before advancing |
| --- | --- | --- | --- |
| A: behavior matrix | 0.4.0 design input | Identify source/controller, runtime and PCM controls; mark preserve/redesign/defer decisions. | Synthetic MIDI cases for controller precedence, resets, source preservation and future note ownership. |
| B: output isolation pilot | 0.4.0 boundary, before 0.5.0 group processing | Compare bounded synth-output strategies; prepare isolated outputs and state off-callback. Full multi-font mapping can follow in separate slices. | Independent group impulses/notes, no cross-group leakage, controllers/note-off ownership, resource failure cleanup and recorded CPU/RAM configuration. |
| C: small master primitives | 0.5.0 preparation | Specify parameter smoothing and master peak/hold/clip observation with bounded control-thread handoff. Full meter UI remains 0.8.0. | Gain-step/ramp vectors, reset/stop cases, pre/post-limiter overload observations, bounded work and no callback allocation/deallocation/locks. |
| D: small bus graph | 0.5.0 | Prepared strips and a small validated graph; separate master gain from source controllers. | Cycle/layout rejection, deterministic routing, mute/solo propagation, no partial publication, safe retirement and one application of master gain. |
| E: processors | 0.6.0 | Parametric EQ, compressor, then reverb/chorus; add delay or graphic EQ only under separate contracts. | Independent response/impulse vectors, stereo linking, reported versus measured latency, bypass/tails and stated block-partition tolerances. |
| F: outputs and presentation | 0.7.0 outputs; 0.8.0 UI | Expose accepted routes, MIDI activity and PCM meters after their core contracts. | Output-capability failures and bounded UI observation without callback/UI coupling. |

Complete 0.4.0 font-mapping slices after the isolation pilot, including missing
resources and transactional configuration changes. A master-only meter does not
need stems, but per-group effects require B and D. Do not duplicate the released
v0.2.0 limiter. Smoothing must preserve mute/stop safety and cannot
weaken the current limiter ceiling.

Use generated SMF and synthetic PCM signals for public tests. Record exact
source revisions, sample rates, block sizes and tolerances. Add scoped Windows
listening/device checks for audio-affecting work. Do not publish private songs,
lyrics, source paths, hashes or corpus details. Runtime evidence remains pinned
to its tested revision; this planning change creates no new audio acceptance.

## Reuse and provenance

Follow [MIGRATION-BOUNDARY.md](MIGRATION-BOUNDARY.md) and
[DEPENDENCY-POLICY.md](DEPENDENCY-POLICY.md). HandyKaraoke is a GPLv3 source
reference; preserve applicable notices for any permitted source port.
BASS-family wrappers/SDKs and Qt UI code/assets remain excluded.

SoundCraft covered code offers MIT OR Apache-2.0 at the pinned snapshot.
A source-derived C++ translation still needs a port record with full upstream
SHA/path, selected license, notices, modifications and tests. Dependencies and
assets need their own review. Do not import restricted ArtCraft marks.
No code, asset, Rust package or dependency is imported by this document.
