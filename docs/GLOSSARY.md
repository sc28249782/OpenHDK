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
| **manual Windows audio validation** | A reported check of Windows device output and playback controls with an identified MIDI/SoundFont pair and source revision. It is separate from hardware-free tests and does not establish measured latency or limiter ceilings. |
| **velocity curve** | A dispatch-time mapping of positive note-on velocity. It does not change CC7, CC11, source data, or note-off messages. |
| **Mixer Profile** | A proposed versioned whole-configuration model for future mixer features. It is not the implemented in-process mixer preset and has no supported file format yet. |
| **MIDI destination** | A proposed receiver of MIDI messages, distinct from a PCM bus or an audio endpoint. External MIDI output remains deferred. |
| **mixer preset** | An in-process named snapshot of 16-channel mute/solo flags. Recall preserves current gains and source controllers. |
| **linked stereo peak limiter** | The stateless PCM boundary that uses one gain per stereo frame, limits peaks to 0.98, and replaces non-finite input samples with zero. |

## Proposed library terms

These terms belong to the v0.3.0 contracts. Pure catalog and KAR helpers are
partly implemented in the development tree, but they do not extend released
v0.2.0 behavior.

| Term | Definition |
| --- | --- |
| **SongId** | An opaque identity owned by one library, independent of song title, source stem, or content hash. |
| **source key** | A registered RootId and normalized relative primary-MIDI locator. |
| **catalog snapshot** | An immutable reader view of one committed catalog revision. |
| **lyric cue** | A timed text fragment or display action retaining source order and media time. It does not imply a syllable or grapheme. |
| **NCN24** | The proposed narrow MIDI/LYR/CUR profile, with a format-evidence gate before implementation. It is not all NCN dialects. |
| **playback generation** | A nonzero identity for one admitted compiled-playback attempt. It prevents an observer from mixing positions and lyrics across attempts. |
| **committed media position** | Frame-derived media time for a block whose MIDI dispatch and PCM rendering both succeeded. It is not a device presentation timestamp. |

## Out-of-scope terms

| Term | Meaning in current documentation |
| --- | --- |
| **Qt migration** | A future UI migration. It is not part of the current SMF foundation. |
| **KAR/NCN playback** | Future karaoke-file parsing and playback integration. It is not part of the current SMF foundation. |
| **HNK/HNK3 compatibility** | Future compatibility or authoring work. It is not part of the current SMF foundation. |
| **VST2/VST3 integration** | A future, separate integration workstream. |
| **physical MIDI hardware I/O** | Future input/output device integration. It is not part of the current SMF foundation. |
