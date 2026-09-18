# OpenHDK roadmap

OpenHDK begins at 0.1.0-dev. It is independent from HandyKaraoke 3.0.0-alpha.1,
which remains the compatibility/recovery release.

## 0.1.0 — bootstrap and audio proof of concept

- Build CMake skeleton and Windows CI.
- Define backend boundary and dependency policy.
- Add a FluidSynth + miniaudio proof of concept for a known MIDI/SF2 fixture.
- Measure device enumeration, startup reliability and output latency.
- Do not port the HandyKaraoke UI or user song folders yet.

Exit gate: reproducible Windows build; MIDI fixture produces PCM/audio; no
BASS artefact enters the repository.

Implementation note: feat/fluidsynth-miniaudio-poc adds the pinned dependency
path, FluidSynthBackend, miniaudio device callback, and a headless CI test.
The remaining 0.1 gate is hosted CI plus a manual Windows audio-device check.

## 0.2.0 — playback foundation

- MIDI file scheduler and SF2 selection.
- Audio-device selection, volume, mixer routing and safe fallback.
- Tempo and transpose semantics defined and tested.
- KAR lyric timeline tests for FF 01 and FF 05.

Exit gate: fixtures behave correctly and no crash occurs when a SoundFont or
audio device is unavailable.

## 0.3.0 — karaoke library

- Port song database and file discovery deliberately, with source attribution.
- Add KAR and NCN playback integration.
- Add staged runtime layout and installer research.

## Later work

- HNK3 authoring/container design.
- Physical MIDI hardware regression rig.
- Optional VST3-host feasibility study in an isolated process.
- Cross-platform work after Windows behaviour is stable.
