# Playback session contract

## Purpose

The current 0.1 proof of concept uses FluidSynth's file player only to prove
MIDI-to-PCM output. OpenHDK 0.2 must own the playback timeline so karaoke
lyrics, seek, transpose, tempo, and audio backends share one deterministic
contract.

## Clock

A playback session exposes a monotonic media position in microseconds. It
advances only while the session is Playing. The implementation must derive it
from a monotonic clock plus accumulated rendered frames; it must never use wall
clock time.

## States

| State | Meaning | Legal next states |
|---|---|---|
| Idle | No prepared MIDI session | Preparing |
| Preparing | MIDI/SF2 validation and event schedule construction | Ready, Failed |
| Ready | Timeline prepared, position is zero | Playing, Idle |
| Playing | Events are dispatched and PCM is rendered | Paused, Finished, Failed, Stopping |
| Paused | Media position is frozen; no new events dispatch | Playing, Stopping |
| Stopping | Stop all active notes and release backend resources | Idle |
| Finished | End-of-file reached and release tail drained | Ready, Idle |
| Failed | Recoverable failure with status | Idle |

## Scheduling rules

- Parse MIDI events into an immutable event list expressed in media time.
- Dispatch all events with event time less than or equal to the render block
  end; preserve file order for equal timestamps.
- The audio callback must never perform file I/O, allocation, parsing, or UI
  work.
- Seek requires a deterministic reconstruction of active MIDI state before the
  next audio block; this is deferred until the initial scheduler is proven.
- Tempo, transpose, and lyric timing are session policies, not FluidSynth API
  aliases.

## Boundary for the first scheduler PR

The first implementation will support Standard MIDI File format 0/1 note,
program, controller, pitch-bend, and tempo events; it will render through the
existing FluidSynthBackend. It will not add KAR/NCN parsing, Qt UI, MIDI
hardware, VST/VST3, HNK/HNK3, or any BASS-family compatibility layer.
