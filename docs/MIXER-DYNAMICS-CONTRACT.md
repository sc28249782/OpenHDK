# Mixer and dynamics contract reference

`SPECIFICATION.md` is the normative authority. This document explains the
v0.2 contract in implementation and test terms. It does not add a feature that
the current `0.1.0` baseline already provides.

## Scope

The first mixer and dynamics increment has three independent features:

1. velocity curves at note-on dispatch;
2. in-process named mute/solo presets; and
3. a linked stereo hard-peak master limiter.

They are deliberately smaller than the later group, bus, effects, and routing
layers. A future implementation must preserve source SMF bytes, decoded
events, compiled timing, CC7/CC11 automation, and the lock-free callback
boundary.

## Velocity curves

Velocity is a MIDI note-on property, not CC7, CC11, or output gain. The input
and output domains are integers from 1 through 127. A velocity-zero note-on is
the existing MIDI representation of note-off and bypasses every curve.

| Curve | Exact positive-velocity mapping | Intent |
| --- | --- | --- |
| `linear` | `v` | Preserve the source value. |
| `soft` | `max(1, round(v*v/127))` | Reduce lower and middle note-on values. |
| `hard` | `min(127, 127-round((127-v)*(127-v)/127))` | Raise lower and middle note-on values. |

`round(n/d)` uses integer `(n + d/2) / d` for non-negative `n` and positive
`d`; integer division discards the remainder. This rule avoids a floating-point
or locale-dependent curve definition. The curve applies once, immediately
before a note-on reaches `MidiCommandSink`. It never changes the source
timeline or any diagnostic output.

Minimum test vectors include `v=1`, `64`, and `127` for all curves, a
velocity-zero note-on, and a non-note-on event adjacent to a transformed note.

## Named mute/solo presets

Each preset is a 16-channel snapshot of only `muted[16]` and `soloed[16]`.
It is not a song-file setting and is not serialized in v0.2. Runtime gain is
deliberately excluded: gain has a separate user-adjustment lifecycle, while a
preset answers only which channels should be mute or solo.

The public operation set is save, recall, delete, and list. Operations that
take a name require the name grammar in the specification. Recall replaces
the complete mute/solo
snapshot and increments the published mixer revision once. The callback may
observe that revision only at its next block boundary. Existing CC120 behavior
still applies when the recalled state makes a channel effectively silent;
recalling an audible state does not recreate a killed voice.

Minimum test coverage includes valid save/recall, replacement by name, invalid
name, missing recall/delete, all-channel recall, one revision per recall, and
preservation of runtime gains and source CC7 state.

## Linked stereo hard-peak limiter

The v0.2 limiter operates on each already-mixed stereo PCM frame. It first
replaces a non-finite input sample with `0.0`. It calculates one shared gain
from the larger absolute left/right value, so it preserves the balance of a
stereo frame. It is instantaneous and stateless by design; no release-tail or
lookahead policy is implied.

| Input left/right | Expected output left/right |
| --- | --- |
| `0.25`, `-0.50` | unchanged |
| `1.00`, `0.50` | `0.98`, `0.49` |
| `-2.00`, `1.00` | `-0.98`, `0.49` |
| `NaN`, `0.50` | `0.00`, `0.50` |
| `+Inf`, `-Inf` | `0.00`, `0.00` |

Tests may compare finite floating-point output with a documented tolerance,
but must assert that both samples are finite and no magnitude exceeds `0.98`
plus that tolerance. The limiter must use no heap allocation, mutex, I/O,
logging, or callback-time interaction with the control path.

## Implementation order

The pure helper `audio/MidiVelocityCurve.hpp` implements the three mappings.
It returns no result for a velocity above 127 or an unknown curve value.
Velocity zero returns zero for each valid curve. It is not connected to
playback yet. Curve selection and dispatch integration remain planned work;
the application does not yet expose velocity curves as a supported feature.

`audio/MidiMixerPresets.hpp` provides the in-process control-thread registry.
Save replaces an existing name, erase deletes a name, and names lists names in
ASCII lexicographic order. Recall uses `MidiRuntimeMixer::setChannelFlags` to
publish both 16-bit masks in one lock-free 32-bit atomic store and increments
the revision once. Invalid or missing names leave the mixer unchanged. Gains
remain independent; the registry has no access to source MIDI controllers.
The registry can allocate and must not run in an audio callback. A single
control thread owns the registry; the render thread reads only the mixer.
Mixer reset does not delete saved presets. Backend and console integration
remain planned work, so the application does not yet expose named presets.

Implement and review one feature per change set:

1. pure velocity-curve helper and hardware-free test vectors;
2. preset model plus atomic mixer publication and tests;
3. pure limiter helper, render-path integration, and deterministic PCM tests.

Manual Windows audio validation occurs after the three changes are integrated.
It supplements, but does not replace, hardware-free tests and both CI jobs.
