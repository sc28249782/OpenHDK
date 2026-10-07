# Playback session contract

## Current boundary

`PlaybackSession` owns timing and due-event selection for an already compiled
immutable `SmfTimeline`. `prepare()` copies that timeline's ordered events into
the session; it does not parse SMF bytes, create a backend, or render PCM.

The retained FluidSynth file-player API remains available for isolated legacy
tests, but the CLI proof of concept uses this compiled timeline path.

## Frame-derived clock

Media time is measured in microseconds and advances only while Playing. Every
`render(frames)` derives its advance from rendered frame count and the prepared
sample rate. The fractional numerator remainder is carried between calls, so a
given total frame count produces the same media time regardless of block
splitting. The clock never reads wall-clock time.

A Playing render block covers `[blockStart, blockEnd]`. It returns each
remaining timeline event whose timestamp is at or before the inclusive block
end, exactly once, in the compiler's existing order. `render()` returns a
due-event span only; it does not synthesize or render PCM. A Paused render
returns no events and does not advance the clock.

The returned event span remains valid until the next `prepare()` or `stop()`
call on that session.

## State transitions

Preparing and Stopping are synchronous internal states. The public legal
transitions are:

| Current state | Operation | Result |
|---|---|---|
| Idle | `prepare(timeline, sampleRate)` | Ready, or Failed for sample rate zero |
| Ready | `play()` | Playing |
| Playing | `pause()` | Paused |
| Paused | `play()` | Playing |
| Playing or Paused, after all timeline events are due | `completeReleaseTail()` | Finished |
| Ready, Playing, Paused, Finished, or Failed | `stop()` | Idle |

All other requested transitions return an IllegalTransition error. Reaching the
end of the timeline does not automatically enter Finished: an explicit
`completeReleaseTail()` acknowledges that any future synthesis release tail is
complete. Calling it before the event cursor reaches the end returns
CompletionBeforeEndOfTimeline.

## Dispatcher boundary

The caller passes the due-event span to `SmfMidiEventDispatcher` and a
hardware-independent `MidiCommandSink`. The dispatcher preserves span order,
has no clock, playback state, sorting, allocation, or I/O, and currently maps
note-on/off, controller, program-change, and pitch-bend only. It normalizes a
zero-velocity note-on to note-off and deliberately ignores tempo, End-of-Track,
generic Meta, SysEx, Polyphonic Key Pressure, and Channel Pressure records.

Preset recall replaces the complete runtime mute/solo flag pair as one mixer
revision. It does not change gains, source controllers, or session position.
The render callback observes the next revision and applies the existing CC120
mute/solo transitions; recalling an audible preset does not recreate stopped
voices. A retained legacy player blocks recall.

The curve-selecting dispatcher overload transforms only positive note-on
velocity immediately before the sink call. The default overload remains
linear. An unknown curve fails before any command is sent. Curve selection
is fixed by backend initialization; non-linear curves cannot use the legacy
file-player path. Source event data and timing remain immutable.

`FluidSynthBackend::playCompiledTimeline()` binds this boundary to FluidSynth
and miniaudio. It stops an initialized device before preparing or replacing the
session, then starts the device only after the prepared session is published.
The callback-visible active flag is lock-free atomic; session mutation remains
confined to the stopped-device control path or the callback itself. Each
callback block selects due events, dispatches them to a private
FluidSynth-backed sink at their clamped frame offsets, and renders the PCM
segments between those offsets. Before selecting events, it may apply a
published `MidiRuntimeMixer` snapshot. Runtime gain, mute, and solo changes
therefore take effect at the next render-block boundary, retain the SMF's CC7
automation, and do not call FluidSynth from the control thread. The callback
performs no parsing, allocation, file I/O, locks, or UI work.

The adapter does not invent a release-tail duration. After it renders the block
that reaches the end of the timeline, it explicitly calls
`completeReleaseTail()` immediately, silences active FluidSynth notes and
sounds, and marks compiled playback complete. A future adapter that needs a
synthesis tail must make that completion decision explicit rather than adding
an implicit duration. The legacy FluidSynth file player and compiled-timeline
entry points cannot control the synth together.

A proposed [running-backend media-clock handoff](MEDIA-CLOCK-HANDOFF-CONTRACT.md)
would publish only positions whose complete MIDI dispatch and PCM render block
succeeded. It would not expose callback-owned `PlaybackSession` state or treat
the frame-derived render clock as a device presentation clock. This handoff is
not implemented and requires review before it changes the adapter.
