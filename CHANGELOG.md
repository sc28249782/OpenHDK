# Changelog

This file records the user-visible scope of each OpenHDK source release.

## Unreleased

### Added

- Playback velocity selection with `--velocity-curve linear|soft|hard`.
  Positive note-ons are transformed at dispatch; source MIDI and controller
  automation remain unchanged. Linear is the default. Non-linear curves
  require compiled playback. Manual Windows release validation is pending.

- An always-enabled linked stereo hard-peak limiter at the shared FluidSynth
  render boundary for device and headless output, with a ceiling of 0.98 and
  non-finite sample sanitization. Manual Windows release validation is pending.

## 0.1.0 - 2026-10-05

Initial source-release baseline.

### Added

- A bounded parser for canonical Standard MIDI File (SMF) formats 0 and 1.
- Deterministic event decoding, tempo-aware timeline compilation,
  `PlaybackSession` timing, and ordered MIDI command dispatch.
- A FluidSynth and miniaudio audio proof of concept for SoundFont synthesis,
  headless PCM rendering, Windows device output, and device enumeration.
- Launch-time master and per-channel volume controls.
- Runtime channel gain, mute, solo, reset, and interactive console controls.
- Read-only MIDI channel diagnostics for programs, note-on counts, controller
  history, reset behavior, and effective channel volume.
- Windows audio-path CI and hardware-free Linux core CI with AddressSanitizer,
  UndefinedBehaviorSanitizer, and strict compiler warnings.
- A normative implemented-behavior specification and regression coverage for
  the playback foundation.

### Scope limits

- This release is source-only. It does not include a prebuilt binary or a
  bundled runtime SoundFont.
- It does not include private validation MIDI or SoundFont assets.
- It is not a complete user-facing karaoke application.
- KAR and NCN playback, lyric display, song library management, Qt UI,
  physical MIDI I/O, external effects or plugins, and packaged end-user
  workflows remain deferred.
