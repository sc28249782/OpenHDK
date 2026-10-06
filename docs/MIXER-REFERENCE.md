# Mixer feature reference

## Purpose and status

This document records a feature-level comparison with
[Kridthawat/HandyMixer](https://github.com/Kridthawat/HandyMixer). It helps
OpenHDK plan a mixer-focused audio engine without importing the legacy BASS or
Qt implementation.

**Status:** planning reference; it does not specify implemented behavior.
`SPECIFICATION.md` remains the normative authority for implemented OpenHDK
behavior. `ROADMAP.md` assigns future work but does not make it available.

**Reference snapshot:** HandyMixer `main` commit `8b730d2`, reviewed on
2026-10-05. The snapshot is useful for feature discovery, not for code reuse.

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

| Capability seen in HandyMixer | OpenHDK decision | Planned direction |
| --- | --- | --- |
| Mixer-focused operation | Adopt concept | Consider a separate mixer-only mode after MIDI/IPC input is specified. |
| Multiple SoundFonts and per-SoundFont gain | Redesign | Backend-independent SoundFont library and layer model in 0.4.0. |
| GM program, bank, and drum mapping | Redesign | Deterministic mapping model; FluidSynth translates it privately. |
| Instrument-family grouping | Redesign | Explicit, testable classifier; never rewrite source MIDI events. |
| Channel mute, solo, and gain | Extend | Current channel controls remain the baseline; group semantics belong to the mixer engine. |
| Bus groups | Redesign | Directed, validated bus graph in 0.5.0; do not reproduce a fixed stream or bus count. |
| Master limiter | Adopt with a new contract | Implemented in 0.2.0; preserve its contract when adding later DSP layers. |
| EQ, compressor, reverb, and chorus | Redesign | Add only after insert/send/return contracts in 0.6.0. |
| Bus-to-speaker routing | Redesign | Start with one multi-channel endpoint in 0.7.0. |
| Multiple physical output devices | Defer | Needs explicit clock-domain, drift, buffering, and resampling policy. |
| System, Dark, and Light themes | Adopt concept | UI design tokens in the later mixer UI phase; no Qt UI reuse. |
| VST/VSTi hosting | Reject from the mixer baseline | Optional VST3 feasibility work stays isolated and outside default playback. |
| Physical MIDI I/O | Defer | Separate hardware and input-contract work. |

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

HandyMixer is a GPLv3 project derived from Handy Karaoke. License compatibility
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
