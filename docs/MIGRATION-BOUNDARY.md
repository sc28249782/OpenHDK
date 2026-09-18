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
