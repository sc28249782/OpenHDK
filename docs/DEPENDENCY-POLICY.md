# Dependency and licensing policy

## Baseline

OpenHDK is GPL-3.0-or-later and must remain buildable from source with
documented, auditable dependencies.

## Excluded legacy stack

OpenHDK deliberately excludes BASS, BASS FX, BASSMIDI, BASSmix, BASS_VST,
and their DLLs, import libraries, headers, SDK archives, redistribution scripts,
license text, and API wrappers.

## Candidates under evaluation

| Component | Candidate | Intended role | Decision |
|---|---|---|---|
| MIDI/SF2 synth | FluidSynth | MIDI events to SoundFont PCM | Evaluate in 0.1 |
| Audio output/mixing | miniaudio | Windows device output and PCM routing | Evaluate in 0.1 |
| MIDI I/O | RtMidi or libremidi | Device input/output | Compare in 0.1 |
| Tempo/pitch | SoundTouch or Rubber Band | Later audio processing | Defer |
| Plugin host | JUCE or dedicated VST3 host | Future optional feature | Defer |

Before adding a dependency, record exact version, source URL, SPDX identifier,
linking model, notice requirements, security/update plan, and test coverage.

## Binary release rule

No binary release may be published until every bundled dependency has a reviewed
redistribution basis and required notices.
