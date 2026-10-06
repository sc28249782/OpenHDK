# Glossary

This glossary is the authoritative source for project terms. Use the exact terms
in this file in documentation, user-facing messages, code comments, and pull
request descriptions.

## Project and licensing

| Term | Definition |
| --- | --- |
| **OpenHDK** | The GPL-3.0-or-later open-source modernization project for a Windows karaoke player. It is a separate development line from HandyKaraoke. |
| **HandyKaraoke** | The separate legacy project. Ported code must preserve required copyright and attribution. |
| **excluded legacy stack** | BASS, BASS FX, BASSMIDI, BASSmix, BASS_VST, and their binaries, import libraries, headers, SDK archives, redistribution scripts, license text, and API wrappers. |
| **dependency policy** | The rules in `docs/DEPENDENCY-POLICY.md` for adding, pinning, reviewing, and redistributing dependencies. |
| **third-party notice** | A required notice that identifies the license and attribution terms for a bundled or used third-party component. |

## Current development scope

| Term | Definition |
| --- | --- |
| **0.1.0 baseline** | The initial source-release baseline. It includes the implemented audio POC and deterministic SMF playback foundation. It is not a completed user-facing application release. |
| **0.2.0 baseline** | The mixer and dynamics source baseline. It adds velocity curves, named mute/solo presets, and a linked stereo peak limiter. It is not a complete karaoke application. |
| **audio proof of concept (audio POC)** | The optional pinned FluidSynth and miniaudio integration used to prove parts of MIDI-to-PCM and device-output behavior. It does not by itself establish complete playback behavior. |
| **SMF playback foundation** | The deterministic parsing, event decoding, timeline, `PlaybackSession`, and dispatch groundwork for SMF formats 0 and 1. |
| **complete user-facing playback** | A complete application-level playback feature with the required behavior for users. OpenHDK does not provide this feature yet. |
| **deferred integration** | Work explicitly scheduled for a later phase. It is not part of the current implementation. |

## SMF and playback

| Term | Definition |
| --- | --- |
| **SMF** | Standard MIDI File. The current foundation supports formats 0 and 1. |
| **SMF parser** | The bounded parser that validates and reads SMF header and track data. |
| **track event decoder** | The component that decodes supported MIDI, Meta, and SysEx events from a parsed track. |
| **timeline** | The ordered, tempo-aware schedule compiled from decoded SMF events. |
| **timeline compiler** | The component that converts decoded events into the deterministic timeline with checked arithmetic. |
| **PlaybackSession** | The deterministic playback-session boundary. It uses a monotonic session clock and renders due-event spans. It does not render PCM. |
| **dispatcher** | The component that sends rendered SMF channel events to an abstract MIDI command sink in timeline order. It does not perform timing or I/O. |
| **MIDI command sink** | The abstract receiver for dispatched MIDI channel events. A concrete backend may implement it. |
| **monotonic session clock** | The playback clock used for MIDI scheduling. It does not use wall-clock time. |
| **PCM** | Pulse-code modulation audio frames. A backend can generate PCM for device output. |
| **SoundFont (SF2)** | A SoundFont 2 file used by FluidSynth to synthesize MIDI events into PCM. |
| **endpoint** | An audio output device selected by an index returned by `--list-devices`. |

## Audio implementation

| Term | Definition |
| --- | --- |
| **AudioBackend** | The small application-facing audio interface. It keeps karaoke-domain behavior independent from a specific audio implementation. |
| **FluidSynth backend** | An audio backend that uses FluidSynth to synthesize MIDI through an SF2 SoundFont. |
| **miniaudio callback** | The audio callback that sends generated PCM to a miniaudio output device. It must not allocate memory, perform file I/O, parse data, or update the UI. |
| **hardware-free test** | A test that does not require an audio device or physical MIDI hardware. |
| **manual Windows audio validation** | A manual check of Windows device output, startup reliability, and output latency. It is separate from hardware-free tests. |

## Out-of-scope terms

| Term | Meaning in current documentation |
| --- | --- |
| **Qt migration** | A future UI migration. It is not part of the current SMF foundation. |
| **KAR/NCN playback** | Future karaoke-file parsing and playback integration. It is not part of the current SMF foundation. |
| **HNK/HNK3 compatibility** | Future compatibility or authoring work. It is not part of the current SMF foundation. |
| **VST2/VST3 integration** | A future, separate integration workstream. |
| **physical MIDI hardware I/O** | Future input/output device integration. It is not part of the current SMF foundation. |
