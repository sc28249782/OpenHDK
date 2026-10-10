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
the twenty-eight hardware-free core test suites, use:

```powershell
cmake -S . -B build-parser -DOPENHDK_BUILD_AUDIO_POC=OFF -DOPENHDK_BUILD_TESTS=ON -DOPENHDK_FETCH_TEST_FIXTURES=OFF
cmake --build build-parser --parallel
ctest --test-dir build-parser --output-on-failure
```

The lightweight Linux core CI job uses this hardware-free configuration with
strict compiler warnings and AddressSanitizer/UndefinedBehaviorSanitizer. It
runs twenty-eight suites: catalog checkpoint codec/restore/store protocol, SMF parser fixtures, MIDI
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
stale tokens, staged publication and uncertainty recovery. Live checkpoint
capture now acquires an immutable projection/context together. Experimental native durable
override operations are implemented; other mutations and fresh evidence remain pending. The
[accepted durable service contract](docs/DURABLE-LIBRARY-SERVICE-CONTRACT.md)
defines exclusive admission and override-first commit-before-publication work.
The override primitive has fake-provider tests and an experimental Linux factory
with private admission. Other persistent mutations and native evidence remain pending. None of these changes are part of released v0.2.0.
The [accepted native provider plan](docs/NATIVE-CHECKPOINT-PROVIDER-PLAN.md)
records Linux/ext4 and Windows/NTFS API candidates and required native evidence;
a Linux ownership lease and experimental checkpoint provider are implemented.
The provider stages, publishes, synchronizes and reconciles through the detached
coordinator. [Native ext4 evidence](docs/NATIVE-LINUX-CHECKPOINT-EVIDENCE.md)
records scoped acceptance of provider/test revision `6a6337d` on WSL2 `/dev/sdd`
ext4. It does not approve native durable service admission or broader environments.
Windows synchronization remains BLOCKED.

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


The development tree also has a bounded Linux checkpoint evidence collector.
See the [native evidence procedure](docs/NATIVE-LINUX-CHECKPOINT-EVIDENCE.md)
for pinned ext4 runs and explicit exit-code receipts. Linux provider acceptance is scoped to revision `6a6337d` and the recorded
WSL2 ext4 environment; this does not change the released v0.2.0 storage boundary.

The accepted [native binding plan](docs/NATIVE-SERVICE-BINDING-PLAN.md) now has
private lease-epoch and bounded root-ancestry primitives with tests. The
coordinator fence and experimental Linux factories are implemented. Fresh native
integration evidence and acceptance remain pending; no released storage feature
is added.

The [experimental factory boundary](docs/NATIVE-SERVICE-BINDING-PLAN.md#11-experimental-factory-and-final-admission-fence)
records explicit Create/Open, private final and NoChange admission checks, and
shared binding/guard accounting. Future durable root lifecycle and application
wiring remain separate slices; scoped factory acceptance is recorded below; later mutation evidence remains separate.

## Native admission receipt instrumentation

The binding and experimental native service targets now emit bounded per-case
[admission receipts](docs/NATIVE-ADMISSION-EVIDENCE.md) for lease epochs, identity/mount checks,
ancestor termination and the final changed/NoChange fence. The existing collector
verifies both logs with Python 3 and records its verification exit code. Current
counts remain 28 core / 31 audio-enabled; binding checks are 74 and native service
checks are 33. A fresh pinned ext4 run is recorded in the evidence section below; explicit
scoped native factory acceptance is recorded below. Historical evidence and the `6a6337d` provider acceptance are
unchanged. Windows checkpoint acknowledgment remains blocked.

## Recorded WSL2 ext4 admission run

The maintainer supplied a [fresh pinned admission run](docs/evidence/native-admission/2026-10-10-wsl2-ext4/README.md) at
`4f6924670d4272736984342c8294967650c60edb` on WSL2 `/dev/sdd` ext4.
All 28 compile/run exits, admission verifier and overall exit are zero. The
binding/service logs use bypass zero and contain 13 traces, 364 identity
observations and three fence cases. The published logs redact the local username
with original/published hashes and replacement counts. This is supplied execution
evidence, not independent source/run authentication. Explicit scoped factory
acceptance is recorded in the final decision below; mount-namespace alias evidence remains Skipped.
Historical `6a6337d` acceptance, Windows blockage and roadmap checkboxes are unchanged.

## Scoped native factory acceptance

The maintainer [explicitly accepted the factory/fence](docs/NATIVE-ADMISSION-EVIDENCE.md#explicit-scoped-factory-acceptance--2026-10-10)
on 2026-10-10 for tested revision
`4f6924670d4272736984342c8294967650c60edb`, on WSL2 `/dev/sdd` ext4
`data=ordered`. This closes the OHK-BIND-030 factory/fence evidence gate only
within the recorded scope. Documentation merges do not relabel the tested
revision. Earlier Pending statements and supplied logs retain their historical
meaning; this final decision is authoritative. The `6a6337d` provider decision
remains separate and unchanged. Mount-namespace alias evidence remains Skipped;
Windows checkpoint acknowledgment, durable root mutations, application/CLI wiring,
NCN evidence and manual validation remain open. No storage release checkbox changes.

## Private durable root registration slice

[OHK-DURABLE-ROOT-030](docs/DURABLE-ROOT-LIFECYCLE-PLAN.md#10-current-slice-31-boundary)
is accepted. Complete-owner staging and fake-provider registration now preserve
lineage, mappings and allocator history until confirmed storage commit. Experimental service `registerRoot` and `reattachRoot` now use prospective
guards and an operation-aware fence. Durable scan/song lifecycle and fresh
mutation receipts/run/acceptance remain pending. Counts stay 28 core / 31 audio-enabled; prior native acceptances do not
extend to this slice.

## Prospective root guard primitive

[OHK-DURABLE-ROOT-030 slice 3.2](docs/DURABLE-ROOT-LIFECYCLE-PLAN.md#11-current-prospective-guard-primitive)
now has private immutable shared guards and prospective admission. Unchanged
roots retain their original handles/identities; one designated changed root may
be retired while the old context stays retained for rollback. Peak old-plus-new
descriptors share the existing limit. The service now associates guards with owned RootIds/generations and fences
experimental root registration/targeted reattachment. See the
[current integration](docs/DURABLE-ROOT-LIFECYCLE-PLAN.md#12-experimental-root-service-integration). Fresh affected native evidence/acceptance is still
required; previous tested revisions remain pinned.
