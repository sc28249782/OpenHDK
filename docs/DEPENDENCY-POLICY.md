# Dependency and licensing policy

## Baseline

OpenHDK is GPL-3.0-or-later and must remain buildable from source with
documented, auditable dependencies.

## Excluded legacy stack

OpenHDK deliberately excludes BASS, BASS FX, BASSMIDI, BASSmix, BASS_VST,
and their DLLs, import libraries, headers, SDK archives, redistribution scripts,
license text, and API wrappers.

## Approved audio dependencies

| Component | Candidate | Intended role | Decision |
|---|---|---|---|
| MIDI/SF2 synth | FluidSynth | MIDI events to SoundFont PCM | Approved for the 0.1.0 and 0.2.0 source baselines |
| Audio output/mixing | miniaudio | Windows device output and PCM routing | Approved for the 0.1.0 and 0.2.0 source baselines |

## Deferred candidates

| Component | Candidate | Intended role | Decision |
|---|---|---|---|
| MIDI I/O | RtMidi or libremidi | Physical device input/output | Defer with MIDI hardware work |
| Tempo/pitch | SoundTouch or Rubber Band | Later audio processing | Defer |
| Plugin host | JUCE or dedicated VST3 host | Future optional feature | Defer |

Before adding a dependency, record exact version, source URL, SPDX identifier,
linking model, notice requirements, security/update plan, and test coverage.

## Binary release rule

No binary release may be published until every bundled dependency has a reviewed
redistribution basis and required notices.

## Exact approved baseline pins

| Component | Exact pin | SPDX/license | Acquisition and redistribution |
|---|---|---|---|
| FluidSynth | vcpkg 2.5.7, baseline fa8cecf91d7f31a1715a7a6524f208897ffb33ce | LGPL-2.1-or-later | CMake find_package from vcpkg source build. Preserve upstream LGPL notice and source/relinking obligations for every binary release. |
| miniaudio | 0.11.25, commit 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d | Unlicense OR MIT-0 | CMake FetchContent; compiled into OpenHDK. Preserve the selected upstream notice. |
| Vintage Dreams Waves test SF2 | FluidSynth v2.5.7 commit 3ede3f7c8ffc5d16e5d28f416cbfd71f3ad2b814, SHA-256 52132b2b83994f0067d3a66b93b4f1b67d53ff8c0d3be90d39f23f92b4585cdd | LicenseRef-VintageDreamsWaves-Freeware | Downloaded only while tests are enabled; never a runtime or release asset. Its custom notice is retained in docs/THIRD_PARTY_NOTICES.md. |

The vcpkg baseline pins FluidSynth and its transitive packages. Updates require
a reviewable change to the pin, notices, and tests in the same pull request.

## SoundFont fallback policy

The proof of concept has no implicit or bundled runtime SoundFont fallback. A
configured SoundFont that is missing or cannot be loaded continues to produce
the corresponding structured recoverable backend error.


## Proposed catalog storage boundary

[OHK-STORE-030](CATALOG-PERSISTENCE-CONTRACT.md) proposes a binary checkpoint
codec plus native operating-system file/lock APIs. It is unaccepted and
unimplemented. It introduces no package, linking change or redistributed asset.
SQLite remains deferred; this proposal does not approve SQLite, Qt SQL or a
legacy database format. A later engine or copied-code choice must record the
exact pin, source, license, notices and update/test plan before implementation.
Native providers still require reviewed API/failure behavior and platform tests.
