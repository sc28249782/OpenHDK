# OpenHDK roadmap

OpenHDK versioning begins with the `0.1.0` source-release baseline. It is
independent from HandyKaraoke 3.0.0-alpha.1, which remains the
compatibility/recovery release.

## 0.1.0 — released source baseline

- [x] Build CMake skeleton and Windows CI.
- [x] Define backend boundary and dependency policy.
- [x] Add a FluidSynth + miniaudio proof of concept for a known MIDI/SF2 fixture.
- [x] Add non-invasive audio-device enumeration and endpoint diagnostics.
- [x] Manually validate Windows device output with an external MIDI/SoundFont
  pair; no private asset was added to the repository.
- Do not port the HandyKaraoke UI or user song folders yet.
- [x] Select an output endpoint by the index shown by --list-devices.
- [x] Reject unavailable endpoint indices and --device with --no-device.
- [x] Set initial FluidSynth output gain with --volume or mute it with --mute.
- [x] Apply launch-time per-MIDI-channel gain trims while preserving SMF CC7
  automation and the GM default channel volume.
- [x] Parse SMF formats 0 and 1 deterministically from bounded byte input,
  including MThd/MTrk validation and malformed-input coverage.
- [x] Decode supported SMF events with bounded malformed-input handling.
- [x] Compile an ordered, tempo-aware SMF timeline with checked arithmetic.
- [x] Add a deterministic PlaybackSession render boundary.
- [x] Dispatch rendered SMF channel events to an
  abstract MIDI command sink, preserving timeline order without timing or I/O.
- [x] Add read-only per-channel SMF diagnostics for note-on counts, observed
  programs, CC7/CC11 history, optional CC39/CC43 14-bit pairs, and deterministic
  event positions, including an OpenHDK effective-CC7 view after CC121, without
  requiring a SoundFont or audio device.
- [x] Add runtime channel gain, mute, solo, reset, and an interactive Windows
  mixer console without changing source SMF controller automation.

The `v0.1.0` source release includes this completed baseline. Its Windows
audio-device check has passed with external, non-distributable validation
assets. It does not provide a complete user-facing playback application.

## 0.2.0 — released mixer and dynamics baseline

- [x] Specify the velocity-curve, named-preset, and master-limiter contracts
  before implementation.
- [x] Add deterministic velocity curves while preserving source MIDI event data
  and the existing CC7/CC11 channel-volume contract.
- [x] Add named channel mute/solo presets without altering source SMF events.
- [x] Add a real-time-safe master limiter after the runtime mixer.
- [x] Extend hardware-free regression coverage for each mixer and dynamics
  contract, including limiter peak behavior.
- [x] Manually validate Windows output with an external MIDI/SoundFont pair;
  do not commit those private assets.

The maintainer reported successful Windows tests and device listening on
2026-10-06. See [V020-WINDOWS-ACCEPTANCE.md](V020-WINDOWS-ACCEPTANCE.md).
The local revision, clean tree, and asset pair are confirmed. Soft reduced
drum loudness as expected from a global velocity curve; linear remains the
default. The signed `v0.2.0` tag and source-only GitHub Release were published
on 2026-10-06 at commit `0175cac`. The release gates below passed; the
acceptance record includes the final commit checks.

Exit gate: hardware-free tests and both CI workflows pass; limiter behavior is
covered deterministically; Windows output is manually validated; no crash
occurs when a SoundFont or audio device is unavailable.

## 0.3.0 — karaoke library (planned)

- Specify library identity, discovery, database ownership, lyric event timing,
  and KAR/NCN format boundaries before implementation. Keep the released
  canonical SMF and mixer contracts unchanged unless a compatibility change
  is explicitly reviewed.
- Port song database and file discovery deliberately, with source attribution.
- Add KAR lyric parsing and timeline tests for FF 01 and FF 05, then KAR and
  NCN playback integration.
- Add staged runtime layout and installer research.

## 0.4.0 — multi-SoundFont synthesis

- Specify a backend-independent SoundFont library, identity, load/unload, and
  per-SoundFont gain contract before implementation.
- Add deterministic GM program, bank, and drum mappings to SoundFont layers.
- Add an instrument-family classifier only as a documented, testable mapping;
  it MUST NOT replace source MIDI events or controller state.
- Add headless coverage for mapping selection, missing SoundFonts, and fallback
  errors without requiring a physical audio device.

Exit gate: mappings and SoundFont lifecycle are deterministic and covered by
hardware-free tests; no BASS-family API or asset enters the repository.

## 0.5.0 — mixer and bus engine

- Specify backend-independent mixer strips, instrument groups, named groups,
  mute/solo/gain/pan, and a directed bus-routing graph before implementation.
- Add configurable buses incrementally, beginning with a small deterministic
  graph rather than a fixed legacy stream count.
- Preserve the real-time callback boundary while applying mixer changes.

Exit gate: graph validity, routing, mute/solo semantics, and state changes have
deterministic regression coverage; Windows output is manually validated.

## 0.6.0 — DSP and effects

- Specify insert, send, return, parameter, bypass, and preset contracts before
  adding effects.
- Use the 0.2.0 master limiter as the foundation, then add parametric EQ,
  compressor, and reverb/chorus in that order. Add per-bus inserts and
  send/return paths only after those contracts are stable.
- Keep an optional VST3-host feasibility study isolated from the playback
  process and outside the default product path.

## 0.7.0 — audio routing

- Add bus-to-output-channel routing for one multi-channel audio endpoint.
- Define channel-layout, endpoint-capability, and safe-fallback behavior.
- Defer multiple physical audio devices until a separate clock-domain,
  buffering, resampling, and drift policy is accepted.

## 0.8.0 — mixer-focused UI

- Select a UI framework only after the mixer and routing contracts are stable.
- Add mixer strips, meters, faders, group/bus routing, and System/Dark/Light
  themes through design tokens rather than copied UI code.
- Consider a mixer-only companion mode only after the required MIDI/IPC input
  contract has been designed separately.

## Scope boundaries

The future milestones above are plans, not implemented release guarantees.
KAR/NCN work requires the separate 0.3.0 contracts; Qt or another UI framework
requires a later reviewed selection.

The released v0.2.0 SMF parser, timeline, PlaybackSession, and dispatcher
remain limited to SMF formats 0 and 1. Qt migration, KAR/NCN parsing or
playback, HNK/HNK3 compatibility, VST2/VST3 integration, and physical MIDI
hardware I/O are outside that released baseline. BASS-family artifacts remain
excluded from all milestones. Audio callbacks must not perform
file I/O, allocation, parsing, or UI work.

## Later work

- HNK3 authoring/container design.
- Physical MIDI hardware regression rig.
- External MIDI input and mixer-sidecar mode.
- Cross-platform work after Windows behaviour is stable.
