# OpenHDK

**Open Handy Karaoke** is an open-source modernization project for a Windows karaoke player.

OpenHDK is a separate development line from [HandyKaraoke](https://github.com/sc28249782/HandyKaraoke). Its purpose is to replace the legacy BASS-based audio stack with a modular, open-source architecture while preserving practical MIDI, SoundFont, KAR and NCN playback workflows.

## Status

`0.1.0` — initial source-release baseline. Optional, pinned FluidSynth and
miniaudio dependencies support the audio POC. Deterministic SMF parsing, event
decoding, timeline compilation, PlaybackSession timing, event dispatch, MIDI
diagnostics, and runtime channel mixing are in place. The application does not
yet provide complete user-facing playback.

Inspect a supported SMF format 0 or 1 file without a SoundFont or audio device:

```powershell
.\build\OpenHDK.exe --midi-diagnostics C:\Music\demo.mid
```

The deterministic report always lists user-facing MIDI channels 1–16 with
nonzero-velocity note-on counts, distinct observed program numbers (shown as
1–128), and controller-state history for CC7/CC11. It reports optional
CC39/CC43 14-bit pairs, CC121 resets, and OpenHDK's effective CC7 state after
a reset. Missing controller data is printed as `unavailable`. Diagnostics is a
standalone, read-only mode and cannot be combined with playback or device
options.

For a Windows device-output playback session, add `--interactive-mixer` and
enter `gain <1-16> <0-100>`, `mute <1-16>`, `unmute <1-16>`, `solo <1-16>`,
`unsolo <1-16>`, `reset`, `help`, or `quit` in the console. It controls the
runtime mixer at render-block boundaries; it does not alter the MIDI file or
SoundFont.

## Principles

- No BASS, BASS FX, BASSMIDI, BASSmix, BASS_VST binaries, headers, libraries or build scripts are included.
- Keep the application core independent from the audio implementation through a small `AudioBackend` interface.
- Start with MIDI + SoundFont playback, device output, and KAR lyric synchronization.
- Treat VST/VST3 hosting as a separate future workstream.
- Preserve required GPL attribution for any HandyKaraoke source code that is later ported.

## Current proof of concept and deferred candidates

- FluidSynth — current SoundFont 2 synthesis proof of concept
- miniaudio — current device-output and mixing proof of concept
- Qt Widgets — deferred UI migration
- RtMidi or libremidi — deferred physical MIDI hardware I/O
- SoundTouch or Rubber Band — later tempo/pitch processing

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/ROADMAP.md](docs/ROADMAP.md), [docs/DEPENDENCY-POLICY.md](docs/DEPENDENCY-POLICY.md), [docs/WRITING-STYLE.md](docs/WRITING-STYLE.md), and [docs/GLOSSARY.md](docs/GLOSSARY.md).

## Build the proof of concept

```powershell
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The default POC configuration uses pinned FluidSynth and miniaudio dependencies
(and downloads a pinned test SoundFont when audio POC tests are enabled). For
the hardware-free SMF parser fixtures, use:

```powershell
cmake -S . -B build-parser -DOPENHDK_BUILD_AUDIO_POC=OFF -DOPENHDK_BUILD_TESTS=ON
cmake --build build-parser --parallel
ctest --test-dir build-parser --output-on-failure
```

The lightweight Linux core CI job uses this hardware-free configuration with
strict compiler warnings and AddressSanitizer/UndefinedBehaviorSanitizer. It
runs the SMF parser fixtures, MIDI diagnostics CLI tests, and runtime mixer
console command tests. It does not install FluidSynth or vcpkg, fetch miniaudio
or SoundFonts, access audio hardware, build the complete audio application, or
claim Linux support for the Windows interactive console. The Windows CI job
continues to validate the complete audio and device-output build.

The POC command uses vcpkg's checked-in `vcpkg-configuration.json`, including
its pinned registry baseline, to resolve FluidSynth reproducibly.

## Specification

The normative contract for this implemented playback baseline is
[docs/SPECIFICATION.md](docs/SPECIFICATION.md).

See [CHANGELOG.md](CHANGELOG.md) for the release scope and
[docs/RELEASE-CHECKLIST.md](docs/RELEASE-CHECKLIST.md) for the signed-tag and
source-release procedure.

## License

OpenHDK is licensed under GPL-3.0-or-later. See [LICENSE](LICENSE).
