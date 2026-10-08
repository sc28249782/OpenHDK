# OpenHDK

**Open Handy Karaoke** is an open-source modernization project for a Windows karaoke player.

OpenHDK is a separate development line from [HandyKaraoke](https://github.com/sc28249782/HandyKaraoke). Its purpose is to replace the legacy BASS-based audio stack with a modular, open-source architecture while preserving practical MIDI, SoundFont, KAR and NCN playback workflows.

## Status

`0.2.0` — released mixer and dynamics source baseline. It adds
velocity curves, named mute/solo presets, and a linked stereo peak limiter.
Optional, pinned FluidSynth and
miniaudio dependencies support the audio POC. Deterministic SMF parsing, event
decoding, timeline compilation, PlaybackSession timing, event dispatch, MIDI
diagnostics, and runtime channel mixing are in place. The application does not
yet provide complete user-facing playback.

The [v0.2.0 source release](https://github.com/sc28249782/OpenHDK/releases/tag/v0.2.0)
contains source archives only. No prebuilt binary, MIDI, or SoundFont is bundled.
Start with the [documentation index](docs/INDEX.md) for contracts, operations,
release evidence, and future work.

Windows tests and device listening are recorded in
[docs/V020-WINDOWS-ACCEPTANCE.md](docs/V020-WINDOWS-ACCEPTANCE.md).

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

Playback accepts `--velocity-curve linear|soft|hard`
to select a note-on velocity curve. The default is `linear`. Selection leaves
source MIDI and diagnostics unchanged and remains fixed during playback.

For a Windows device-output playback session, add `--interactive-mixer` and
enter `gain <1-16> <0-100>`, `mute <1-16>`, `unmute <1-16>`, `solo <1-16>`,
`unsolo <1-16>`, `reset`, `help`, or `quit` in the console. It controls the
runtime mixer at render-block boundaries; it does not alter the MIDI file or
SoundFont.

The console also accepts `preset-save <name>`,
`preset-recall <name>`, `preset-delete <name>`, and `preset-list`. Presets
store mute/solo flags only and preserve current gains when recalled. Names
are case-sensitive ASCII identifiers of 1–32 characters, starting with a
letter and followed by letters, digits, hyphen, or underscore. Presets remain
in memory for the backend object's lifetime; `reset` does not delete them.

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

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/ROADMAP.md](docs/ROADMAP.md), [docs/MIXER-REFERENCE.md](docs/MIXER-REFERENCE.md), [docs/DEPENDENCY-POLICY.md](docs/DEPENDENCY-POLICY.md), [docs/WRITING-STYLE.md](docs/WRITING-STYLE.md), and [docs/GLOSSARY.md](docs/GLOSSARY.md).

## Build the proof of concept

```powershell
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The default POC configuration uses pinned FluidSynth and miniaudio dependencies
(and downloads a pinned test SoundFont when audio POC tests are enabled). For
the twenty-one hardware-free core test suites, use:

```powershell
cmake -S . -B build-parser -DOPENHDK_BUILD_AUDIO_POC=OFF -DOPENHDK_BUILD_TESTS=ON -DOPENHDK_FETCH_TEST_FIXTURES=OFF
cmake --build build-parser --parallel
ctest --test-dir build-parser --output-on-failure
```

The lightweight Linux core CI job uses this hardware-free configuration with
strict compiler warnings and AddressSanitizer/UndefinedBehaviorSanitizer. It
runs twenty-one suites: catalog checkpoint codec/restore/store protocol, SMF parser fixtures, MIDI
diagnostics CLI, runtime mixer console, velocity curves, mixer presets, stereo peak limiting, song catalog
identity/transactions, filesystem discovery, lyric text decoding, KAR cue
extraction, media-clock consumption, atomic media-clock publication,
generation-bound lyric observation, single-song preparation, root reattachment,
pure catalog metadata/policy models, and catalog metadata scan/preparation
integration, and user override/display transactions. It does not
install FluidSynth or vcpkg, fetch miniaudio or SoundFonts, access audio
hardware, build the complete audio application, or claim Linux support for
the Windows interactive console. The Windows CI job
continues to validate the complete audio and device-output build.

The POC command uses vcpkg's checked-in `vcpkg-configuration.json`, including
its pinned registry baseline, to resolve FluidSynth reproducibly.

## Planned 0.3.0 contracts

The [karaoke library contract](docs/KARAOKE-LIBRARY-CONTRACT.md) and
[lyric timeline contract](docs/LYRIC-TIMELINE-CONTRACT.md) define the
accepted planning boundary (PR #34): song identity, discovery, catalog ownership,
KAR lyric
selection/timing, and a gated NCN24 profile. They are pre-implementation
contracts. The development tree adds an in-memory catalog, bounded local
SMF/KAR discovery, content revision checks, and a pure UTF-8/TIS-620 lyric
text decoder, pure KAR selection/immutable cue extraction, and a pure media-clock
consumer. Catalog metadata and immutable root policies are connected to
scan/preparation under [OHK-META-030](docs/CATALOG-METADATA-POLICY-CONTRACT.md).
Ready includes selected lyric validation; preparation inherits the root policy
or uses a complete per-call override. Atomic user title/artist replacement and
owning display resolution are implemented. Prepared display uses fresh effective
source metadata and the acquired snapshot's overrides. Durable storage and
library playback orchestration remain unimplemented.
Released v0.2.0 does not provide
library or KAR/NCN services.

The accepted
[running-backend media-clock handoff](docs/MEDIA-CLOCK-HANDOFF-CONTRACT.md)
defines the next observation boundary. Its pure atomic publication cell and
generation counter are connected to compiled FluidSynth playback; lyric
consumer binding has a serialized control-path observer. Single-song SMF/KAR
preparation verifies the recorded source revision and owns
both timelines; application/CLI integration remains pending.

[The accepted persistence contract](docs/CATALOG-PERSISTENCE-CONTRACT.md) defines a bounded
whole-catalog checkpoint, fresh-owner restore and explicit root reattachment.
The development tree has a pure bounded schema-1 codec and detached projection
with independent wire/hash fixtures, plus validated fresh-owner restore. Restored
roots require explicit attachment and a complete scan; hints grant no source access.
A detached checkpoint store coordinator has fake-provider protocol tests for
stale tokens, staged publication and uncertainty recovery. Native storage, live
checkpoint capture and durable library operations remain pending. None of these
changes are part of released v0.2.0.

## Specification

The normative contract for this implemented playback baseline is
[docs/SPECIFICATION.md](docs/SPECIFICATION.md).

The v0.2 velocity, preset, and limiter behavior is explained in
[docs/MIXER-DYNAMICS-CONTRACT.md](docs/MIXER-DYNAMICS-CONTRACT.md).

See [CHANGELOG.md](CHANGELOG.md) for the release scope and
[docs/RELEASE-CHECKLIST.md](docs/RELEASE-CHECKLIST.md) for the signed-tag and
source-release procedure.

## License

OpenHDK is licensed under GPL-3.0-or-later. See [LICENSE](LICENSE).
