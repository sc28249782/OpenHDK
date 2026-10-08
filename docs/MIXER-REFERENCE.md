# Mixer feature reference

## Purpose and status

This document records a feature-level comparison with
[Kridthawat/Buai-Music-Mixer](https://github.com/Kridthawat/Buai-Music-Mixer). It helps
OpenHDK plan a mixer-focused audio engine without importing the legacy BASS or
Qt implementation.

**Status:** planning reference; it does not specify implemented behavior.
`SPECIFICATION.md` remains the normative authority for implemented OpenHDK
behavior. `ROADMAP.md` assigns future work but does not make it available.

**Initial snapshot:** HandyMixer `main` commit `8b730d2`, reviewed on
2026-10-05. The repository is now named Buai-Music-Mixer. The original URL
redirects to the renamed repository.

**Update snapshot:** `5a50c9ab2fcb804beea157f9c45d0d496958773a`,
dated 2026-10-07; reviewed on 2026-10-08. The
[comparison](https://github.com/Kridthawat/Buai-Music-Mixer/compare/8b730d2...5a50c9a)
contains 29 commits, and `version.h` changes from 1.0.0 to 1.2.0. This is a
pinned comparison, not a claim about the current upstream head. Source changes
were inspected; upstream audio, devices, and UI were not run locally.

## Product direction

OpenHDK is intended to develop from deterministic MIDI/SoundFont playback into
a backend-independent karaoke mixer and routing engine. FluidSynth is the
current synthesizer adapter; it MUST NOT define the public mixer model.

The planned audio path is:

```text
SMF or future external MIDI
  -> MIDI channel state
  -> optional instrument-family classifier
  -> SoundFont layers
  -> mixer strips and instrument groups
  -> bus routing
  -> insert and send/return effects
  -> master bus and limiter
  -> output routing
  -> miniaudio endpoint
```

This is a target architecture. Each layer requires its own contract and tests
before it becomes an OpenHDK feature.

## Capability decisions

| Capability seen in the reference | OpenHDK decision | Planned direction |
| --- | --- | --- |
| Mixer-focused operation | Adopt concept | Consider a separate mixer-only mode after MIDI/IPC input is specified. |
| Multiple SoundFonts and per-SoundFont gain | Redesign | Backend-independent SoundFont library and layer model in 0.4.0. |
| GM program, bank, and drum mapping | Redesign | Deterministic mapping model; FluidSynth translates it privately. |
| Instrument-family grouping | Redesign | Explicit, testable classifier; never rewrite source MIDI events. |
| Channel mute, solo, and gain | Extend | Current channel controls remain the baseline; group semantics belong to the mixer engine. |
| Bus groups | Redesign | Directed, validated bus graph in 0.5.0; do not reproduce a fixed stream or bus count. |
| Master volume | Extend | OpenHDK already has backend volume control. Specify a separate master-gain stage before the limiter when buses are added; expose it in the future UI. |
| Mixer configuration files | Redesign | Versioned Mixer Profile; distinct from the existing flags-only mixer preset. |
| Drum tuning protection | Redesign | Explicit opt-in playback policy; preserve source interpretation by default. |
| Per-instrument external MIDI output | Adopt concept; defer implementation | Separate MIDI destinations from audio buses; require hardware and route-transition contracts first. |
| Visible bus destination | Adopt UX concept | Show the destination on each strip without requiring a routing dialog. |
| Master limiter | Adopt with a new contract | Implemented in 0.2.0; preserve its contract when adding later DSP layers. |
| EQ, compressor, reverb, and chorus | Redesign | Add only after insert/send/return contracts in 0.6.0. |
| Bus-to-speaker routing | Redesign | Start with one multi-channel endpoint in 0.7.0. |
| Multiple physical output devices | Defer | Needs explicit clock-domain, drift, buffering, and resampling policy. |
| System, Dark, and Light themes | Adopt concept | UI design tokens in the later mixer UI phase; no Qt UI reuse. |
| VST/VSTi hosting | Reject from the mixer baseline | Optional VST3 feasibility work stays isolated and outside default playback. |
| Physical MIDI I/O | Defer | Separate hardware and input-contract work. |

## Lessons from the update snapshot

The following are planning decisions. They do not change the implemented
playback contract or authorize dependencies or source ports.

### Mixer Profile and master gain

Commit [aa6b6e3](https://github.com/Kridthawat/Buai-Music-Mixer/commit/aa6b6e34465b858158d1fc90d2d57c24007de782)
adds visible master-volume control and `.bmcfg` save/open operations in
`MainWindow.cpp`. Treat portable configuration as a separate Mixer Profile
model. Its future schema may cover SoundFont assignments, gains, mute/solo,
bus routes, effects, output layouts, and MIDI destinations as those features
become available. Do not adopt the upstream settings dump or file format.

Before implementation, specify schema versions, size limits, validation,
unknown-field policy, resource identity, migration, and transactional loading.
An invalid profile should leave the active configuration intact. Decide whether
resource changes require stop/reprepare; do not assume live graph replacement.
The v0.2 mixer preset remains an in-process mute/solo snapshot that preserves
gains. It does not become a persistent whole-system profile.

For future buses, distinguish master gain from limiting: bus mix -> master gain
-> master DSP/limiter -> output. Current volume is applied through FluidSynth
synth gain before the shared limiter. A future PCM master stage needs its own
contract to prevent gain being applied twice.

### Typed routing and hardware behavior

Commit [113fe35](https://github.com/Kridthawat/Buai-Music-Mixer/commit/113fe358aaca104ba7cff1530a692a9bd187d5af)
adds instrument-family external MIDI destinations in `Midi/MidiSynthesizer.cpp`.
Missing or busy ports at open time fall back to internal synthesis. This is an
observation, not an approved OpenHDK fallback policy or a hot-unplug guarantee.

Plan MIDI destinations (internal or future synth adapters, external MIDI)
separately from PCM buses and audio endpoints. External MIDI does not pass
through an OpenHDK audio bus or PCM limiter. Specify note ownership across
program changes, drum-note classification, controller fan-out, timing, and
route transitions before implementation. Device names alone are not a stable
identity contract. Decide fallback explicitly and report it to the user.

Future acceptance cases should cover missing, busy, disconnected, and
reconnected devices; selecting the same device; changing a route during
playback; all-notes-off cleanup; and delivery of note-off to the destination
that received note-on. Hardware I/O remains deferred. Do not add device calls,
allocation, or blocking work to the audio callback.

### Drum tuning policy

The snapshot adds drum-channel pitch-bend and selected RPN/NRPN tuning filters
in `Midi/MidiSynthesizer.cpp`. In that implementation, controller forwarding
to external ports occurs before the internal drum filter. Do not assume the
option protects every destination equally.

Consider explicit policies such as PreserveSource, IgnorePitchBend,
IgnoreTuningRpnNrpn, and IgnoreAllPitchControl. Names and semantics remain
proposals. PreserveSource should remain the default; filtering belongs at the
playback boundary, not in the parser, immutable timeline, or diagnostics.
Specify per-channel RPN/NRPN selection, data entry, increment/decrement, null
selection, reset behavior, and destination scope. Do not equate every channel
10 tuning message with a universal GM drum-pitch standard. Add synthetic
fixtures before introducing any compatibility option.

### Mixer UI and plugin lessons

Commit [31202e0](https://github.com/Kridthawat/Buai-Music-Mixer/commit/31202e02e566f4a88742126a9483c525cd39109b)
exposes `BusN` as a strip row in `Widgets/InstCh.cpp`. Adopt destination
visibility and consistent controls, with named buses and accessible labels.
Skins, LEDs, icons, title bars, and rounded dialogs are UX references only;
no Qt source or assets are selected for reuse.

The update makes plugin chunks authoritative over program/parameter fallback
in `BASSFX/VSTFX.cpp` and `Midi/MidiSynthesizer.cpp`. The
[5a50c9a editor change](https://github.com/Kridthawat/Buai-Music-Mixer/commit/5a50c9ab2fcb804beea157f9c45d0d496958773a)
uses a native frame. Retain these as lessons for an isolated VST3 study:
specify state precedence and editor open/hide/close/reopen ownership. They do
not authorize VST2, BASS_VST, Qt embedding, or x86 support in OpenHDK.

## Non-negotiable design rules

- Do not copy BASS-family, BASS FX, BASSMIDI, BASSmix, or BASS_VST code,
  wrappers, artifacts, or API-shaped abstractions.
- Do not copy Qt UI code or assets. A future UI must use its own framework and
  design tokens.
- Keep source MIDI events and SMF controller automation intact. Mixer state
  modifies rendering behavior; it does not edit the source file.
- Keep the public mixer and routing model independent from FluidSynth. A
  synthesizer adapter converts that model to backend-specific operations.
- Preserve the callback contract: no allocation, locks, file I/O, parsing,
  logging, or UI work in the real-time audio callback.
- Do not introduce multiple physical-device playback without an accepted
  synchronization design.

## Provenance and licensing

Buai Music Mixer (formerly HandyMixer) is a GPLv3 project derived from Handy Karaoke. License compatibility
does not override OpenHDK's migration boundary. This document records a
behavioral reference only; it grants no permission to copy implementation.

Any future OpenHDK source port must follow
`MIGRATION-BOUNDARY.md` and include the required upstream path, revision,
notice, modification, and test record. Dependencies introduced for the planned
mixer work must also satisfy `DEPENDENCY-POLICY.md`.

## Related documents

- `ROADMAP.md` — release ordering and exit gates.
- `SPECIFICATION.md` — current normative playback behavior and deferred scope.
- `MIXER-DYNAMICS-CONTRACT.md` — v0.2 velocity, preset, and limiter detail.
- `MIGRATION-BOUNDARY.md` — restrictions on code and artifacts from legacy or
  reference projects.
- `DEPENDENCY-POLICY.md` — dependency and redistribution requirements.
