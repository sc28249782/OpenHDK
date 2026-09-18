# OpenHDK roadmap

OpenHDK begins at 0.1.0-dev. It is independent from HandyKaraoke 3.0.0-alpha.1,
which remains the compatibility/recovery release.

## 0.1.0 — bootstrap and audio proof of concept

- [x] Build CMake skeleton and Windows CI.
- [x] Define backend boundary and dependency policy.
- [x] Add a FluidSynth + miniaudio proof of concept for a known MIDI/SF2 fixture.
- [x] Add non-invasive audio-device enumeration and endpoint diagnostics.
- [ ] Manually validate Windows device output, startup reliability, and output latency.
- Do not port the HandyKaraoke UI or user song folders yet.

Exit gate: reproducible Windows build; MIDI fixture produces PCM/audio; no
BASS artefact enters the repository.

Implementation note: feat/fluidsynth-miniaudio-poc adds the pinned dependency
path, FluidSynthBackend, miniaudio device callback, and a headless CI test.
feat/audio-device-diagnostics adds a non-invasive playback-device listing
command. The remaining 0.1 gate is a manual Windows audio-device check.

## 0.2.0 — deterministic SMF playback foundation

- [x] Select an output endpoint by the index shown by --list-devices.
- [x] Reject unavailable endpoint indices and --device with --no-device.
- [x] Set initial FluidSynth output gain with --volume or mute it with --mute.
- [x] Parse SMF formats 0 and 1 deterministically from bounded byte input,
  including MThd/MTrk validation and malformed-input coverage.
- [x] Decode supported SMF events with bounded malformed-input handling.
- [x] Compile an ordered, tempo-aware SMF timeline with checked arithmetic.
- [x] Add a deterministic PlaybackSession render boundary.
- [x] Dispatch rendered SMF channel events to an
  abstract MIDI command sink, preserving timeline order without timing or I/O.

Next unfinished integration work: bind the dispatcher to FluidSynthBackend and
the miniaudio callback, define SF2 fallback behavior, and perform manual
Windows audio validation. This does not yet provide complete user-facing playback.

Exit gate: fixtures behave correctly and no crash occurs when a SoundFont or
audio device is unavailable.

## 0.3.0 — karaoke library

- Port song database and file discovery deliberately, with source attribution.
- Add KAR lyric parsing and timeline tests for FF 01 and FF 05, then KAR and
  NCN playback integration.
- Add staged runtime layout and installer research.

## Scope boundaries

The initial SMF parser, timeline, PlaybackSession, and dispatcher remain
limited to SMF formats 0 and 1. Do not add BASS-family artifacts, Qt migration,
KAR/NCN parsing or playback, HNK/HNK3 compatibility, VST2/VST3 integration,
or physical MIDI hardware I/O to this work. Audio callbacks must not perform
file I/O, allocation, parsing, or UI work.

## Later work

- HNK3 authoring/container design.
- Physical MIDI hardware regression rig.
- Optional VST3-host feasibility study in an isolated process.
- Cross-platform work after Windows behaviour is stable.
