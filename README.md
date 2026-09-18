# OpenHDK

**Open Handy Karaoke** is an open-source modernization project for a Windows karaoke player.

OpenHDK is a separate development line from [HandyKaraoke](https://github.com/sc28249782/HandyKaraoke). Its purpose is to replace the legacy BASS-based audio stack with a modular, open-source architecture while preserving practical MIDI, SoundFont, KAR and NCN playback workflows.

## Status

`0.1.0-dev` — bootstrap repository. The current target is a buildable CMake skeleton; it does not yet play audio or load songs.

## Principles

- No BASS, BASS FX, BASSMIDI, BASSmix, BASS_VST binaries, headers, libraries or build scripts are included.
- Keep the application core independent from the audio implementation through a small `AudioBackend` interface.
- Start with MIDI + SoundFont playback, device output, and KAR lyric synchronization.
- Treat VST/VST3 hosting as a separate future workstream.
- Preserve required GPL attribution for any HandyKaraoke source code that is later ported.

## Intended first stack

- Qt Widgets — existing desktop UI direction
- RtMidi or libremidi — MIDI input/output
- FluidSynth — SoundFont 2 synthesis
- miniaudio — device output and mixing
- SoundTouch or Rubber Band — later tempo/pitch processing

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/ROADMAP.md](docs/ROADMAP.md), and [docs/DEPENDENCY-POLICY.md](docs/DEPENDENCY-POLICY.md).

## Build the bootstrap

```powershell
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The bootstrap intentionally has no network-fetched or binary audio dependency.

## License

OpenHDK is licensed under GPL-3.0-or-later. See [LICENSE](LICENSE).