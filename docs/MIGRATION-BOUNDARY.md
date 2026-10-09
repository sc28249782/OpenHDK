# Migration boundary from HandyKaraoke

OpenHDK is not a rename of HandyKaraoke. It is a separate repository and a
clean modernization effort.

## What may be carried forward

- Compatibility goals: MIDI, KAR, NCN, lyrics, SoundFonts and karaoke controls.
- Public file-format knowledge and independently written parsers.
- GPL-covered HandyKaraoke source only with preserved copyright/license notices.

## What must not be carried forward

- BASS-family SDK files and API-coupled wrapper code.
- BASS distribution/staging scripts.
- Proprietary binary, key, installer payload, or non-redistributable asset.
- Assumptions that VST2/VST3 hosting is safe or enabled by default.

## Port record requirement

Every source port needs a short record in its commit or docs/ports/ containing
the upstream repository/commit/path, original notice, modifications, and tests.

## Feature-reference rule

OpenHDK MAY study another project to identify user-facing capabilities and to
compare product behavior. A feature reference is not source-port permission.
`docs/MIXER-REFERENCE.md` records the pinned Buai Music Mixer (formerly HandyMixer) comparison and the
adopt, redesign, defer, or reject decision for each capability.

Code, build files, UI assets, dependency wrappers, and API patterns that are
specific to the BASS family or Qt MUST NOT be copied from that reference. Any
future source port remains subject to the port record requirement and this
migration boundary.

## SoundCraft source-study boundary

[SOUNDCRAFT-REFERENCE.md](SOUNDCRAFT-REFERENCE.md) pins the separate mixer/DSP
study. No SoundCraft source, asset or dependency is imported by that reference.
Covered code offers MIT OR Apache-2.0; inspect the actual file, selected license,
NOTICE and separately licensed material for each proposed port. C++ translation
of source-derived logic still needs the port record above. Independently written
algorithm work should cite primary formulas and tests; language translation is
not a clean-room claim. ArtCraft brand marks have restricted separate terms and
must not be imported or used to imply endorsement.
