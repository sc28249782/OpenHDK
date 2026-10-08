# Changelog

This file records the user-visible scope of each OpenHDK source release.

## Unreleased

- Development library core: opaque song IDs, immutable in-memory catalog
  snapshots, and bounded local SMF/KAR discovery with canonical SMF validation,
  SHA-256 content revisions, per-file diagnostics, and scan rollback.
  No library CLI, metadata database, KAR/NCN lyric playback, or library playback
  preparation is provided yet. Released v0.2.0 behavior is unchanged.

- Development lyric text core: explicit bounded UTF-8/TIS-620 decoding with
  payload byte-offset failures and no partial result or encoding fallback.
  Pure KAR extraction adds FF05/identified FF01 selection, immutable event-timed
  cues with source-ordered display operations, and retained metadata/title and
  raw payloads. A pure media-clock consumer emits whole cues once, retains an
  owning observed prefix, rejects backward positions, and clears traversal on
  reset/stop. The accepted backend-clock handoff now has a pure lock-free
  latest-value cell and monotonic playback-generation counter with bounded
  reads and explicit exhaustion. Compiled FluidSynth playback now acknowledges
  its generation and publishes committed render positions and terminal state;
  explicit backend stop publishes Stopped, and legacy playback reports that no
  compiled clock is available. Catalog preparation and audio/CLI lyric-consumer
  binding remain unimplemented.

## 0.2.0 - 2026-10-06

Mixer and dynamics source baseline, prepared for signed source-only release.

### Added

- Named in-process mute/solo presets through backend operations and the
  Windows interactive mixer console: save, recall, delete, and sorted list.
  Recall preserves runtime gains and source controllers. Presets remain in
  memory for the backend object's lifetime; reset does not delete them.

- Playback velocity selection with `--velocity-curve linear|soft|hard`.
  Positive note-ons are transformed at dispatch; source MIDI and controller
  automation remain unchanged. Linear is the default. Non-linear curves
  require compiled playback.

- An always-enabled linked stereo hard-peak limiter at the shared FluidSynth
  render boundary for device and headless output, with a ceiling of 0.98 and
  non-finite sample sanitization.

### Validation and scope

- Windows automated tests passed 9/9 and manual device listening passed on
  `86ff592`, with an external MIDI/SoundFont pair. See
  `docs/V020-WINDOWS-ACCEPTANCE.md` for the evidence and limits.
- Soft applies to drums as well as melodic channels and can substantially
  reduce drum loudness. Linear remains the default.
- Source-only: no binary, MIDI, or SoundFont assets are bundled. This is not
  a complete karaoke application; the deferred product scope remains unchanged.
- Signed tagging and GitHub Release publication require the release checklist.

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
