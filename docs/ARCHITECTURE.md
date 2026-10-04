# OpenHDK architecture

## Implemented deterministic SMF path

~~~text
bounded canonical SMF bytes
        ↓
SmfParser
        ↓
SmfTrackEventDecoder
        ↓
SmfTimelineCompiler
        ↓
    SmfTimeline
      ↙     ↘
MidiChannelDiagnostics    PlaybackSession
(read-only aggregate)     (frame-derived timing and due-event spans)
                                ↓
                    SmfMidiEventDispatcher
                                ↓
                    abstract MidiCommandSink
~~~

The parser supports SMF formats 0 and 1. The decoder retains supported channel
events plus tempo, Meta, SysEx, and End-of-Track records; the dispatcher sends
only its supported channel subset to the sink. Timeline ordering is by tick,
file track index, and source index. `PlaybackSession` returns due events but
does not render PCM. `FluidSynthBackend::playCompiledTimeline()` binds those
due-event spans to a private FluidSynth sink and miniaudio render blocks.

The current policy is strict canonical SMF input: bounded MThd/MTrk structure,
required End-of-Track, and no trailing track or file data. A compatibility mode
for legacy padding would require an explicit future design, including exact
acceptance and error-offset rules; it is not implied by the current parser.

`MidiChannelDiagnostics` is an output-independent aggregate over the compiled
timeline. It uses fixed-size per-channel and per-program storage, counts only
note-on messages with nonzero velocity, and records the set of observed
programs. For CC7, CC39, CC11, and CC43, it retains first, pre-first-note,
final, minimum, maximum, and value-change observations in deterministic
timeline order. It reports a 14-bit final value only after both the controller
MSB and LSB have appeared on the same channel, and counts CC121 reset events.
It also reports `effective-cc7`, the source CC7 state OpenHDK will send before
the launch-time channel gain: each CC121 makes that state 100 at the reset's
timeline position. This is an OpenHDK playback-contract view, distinct from
the raw CC7 events in the SMF.
The CLI owns text formatting and file I/O. Its standalone `--midi-diagnostics`
path exits before constructing the audio backend, so inspection has no
SoundFont or audio device dependency.

## Current audio proof of concept

`AudioBackend` separates the 0.1 FluidSynth + miniaudio proof of concept from
the SMF path above. Its explicit compiled-timeline entry prepares a
`PlaybackSession` outside the callback, renders PCM segments between due-event
frame offsets in the callback, and dispatches events at those offsets. The CLI
reads, parses, and compiles SMF before activating the backend, then uses this
compiled-timeline entry and supports a launch-time per-channel gain table. The
FluidSynth MIDI sink keeps
each channel's source CC7 value, applies its configured gain to that value, and
then sends the resulting CC7 value to FluidSynth. Therefore a trim preserves
the volume automation in an SMF instead of replacing it. Reset All Controllers
(CC121) restores the GM default CC7 value before that same gain is applied.
This table is prepared before playback; changing channel gains while a device
callback is running uses `MidiRuntimeMixer`. The control path publishes
lock-free atomic gain, mute, and solo state; the callback observes that state
only at a render-block boundary and issues the corresponding CC7 changes from
its private FluidSynth sink. This preserves source CC7 automation and never
calls FluidSynth from the control thread. Its API uses zero-based channel
indices 0–15; the CLI's existing `--channel-volume` remains one-based 1–16.
`resetRuntimeMixer()` clears all runtime gains, mutes, and solos.
On Windows, `--interactive-mixer` is a main-thread console polling surface for
those same operations. It does not create a second callback or a worker thread;
the callback remains the only path that calls FluidSynth for runtime changes.
The legacy file-player path is not permitted to run alongside compiled-timeline
playback on the same synth.

The POC has no implicit or bundled SoundFont fallback. A missing or unloadable
configured SoundFont returns the existing structured recoverable error.

## Deferred layers

Qt migration, karaoke library work, KAR/NCN parsing and lyric timelines,
physical MIDI hardware and RtMidi/libremidi, HNK/HNK3 compatibility, and
VST2/VST3 are deferred. They are not part of the implemented SMF path or the
audio proof of concept. No BASS-family component belongs in any layer.

Audio callbacks must not perform file I/O, allocation, parsing, or UI work.
