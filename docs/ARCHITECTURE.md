# OpenHDK architecture

## Goal

Separate karaoke-domain behaviour from the audio implementation so a backend
can evolve without changing song indexing, lyric timing, settings, or UI
contracts.

## Layers

~~~text
Qt application and settings
        ↓
Karaoke domain: library, playlist, KAR/NCN parsers, lyric timeline
        ↓
AudioBackend interface
        ↓
FluidSynth backend → miniaudio output
        ↓
Windows audio device
~~~

The bootstrap has no Qt dependency yet. It validates the CMake and architecture
base before a deliberate UI migration.

## Backend rules

- audio/AudioBackend.hpp is the only application-facing audio contract.
- No backend-specific header may appear outside audio/.
- MIDI scheduling uses the playback session monotonic clock, never wall clock.
- Song parsing and lyric timing remain deterministic without an audio device.
- A backend failure returns a recoverable error; it must not terminate the UI.

## Initial backend

FluidSynthBackend will synthesize MIDI through an SF2 SoundFont. Generated PCM
will feed a miniaudio device callback. MIDI I/O stays a separate adapter,
initially RtMidi or libremidi after evaluation.

VST/VST3 hosting is outside the first backend scope.
