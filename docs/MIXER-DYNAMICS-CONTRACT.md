# Mixer and dynamics contract reference

`SPECIFICATION.md` is the normative authority. This document explains the
implemented v0.2 contract in implementation and test terms. These additions
are outside the released `0.1.0` baseline.

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

## Released implementation

The pure helper `audio/MidiVelocityCurve.hpp` implements the three mappings.
It returns no result for a velocity above 127 or an unknown curve value.
Velocity zero returns zero for each valid curve. The dispatcher
maps positive note-on velocity once, immediately before calling the sink. Its
two-argument API keeps linear behavior; its curve-selecting overload rejects
unknown curves before it emits any command. Backend configuration selects one
curve for the initialized session, with linear as the default. The playback
CLI accepts `--velocity-curve linear|soft|hard`; names are case-sensitive and
the last repeated option wins. Curve selection does not change during playback.
Non-linear curves require compiled-timeline playback; the legacy file-player
API rejects them. Source timeline data, timing, and diagnostic observations
remain unchanged. Manual Windows validation passed as recorded in
`V020-WINDOWS-ACCEPTANCE.md`.

`audio/MidiMixerPresets.hpp` provides the in-process control-thread registry.
Save replaces an existing name, erase deletes a name, and names lists names in
ASCII lexicographic order. Recall uses `MidiRuntimeMixer::setChannelFlags` to
publish both 16-bit masks in one lock-free 32-bit atomic store and increments
the revision once. Invalid or missing names leave the mixer unchanged. Gains
remain independent; the registry has no access to source MIDI controllers.
The registry can allocate and must not run in an audio callback. A single
control thread owns the registry; the render thread reads only the mixer.
Mixer reset does not delete saved presets. The backend owns
the registry and exposes save, recall, delete, and list operations. All require
initialization; recall also rejects a retained legacy player. The registry
survives backend shutdown/reinitialize and ends with its backend object.
The Windows console uses `preset-save <name>`, `preset-recall <name>`,
`preset-delete <name>`, and `preset-list`. List returns ASCII-sorted names.
Only recall publishes mixer state; the callback applies the existing CC120
transition rules at the next block boundary. No fixed count limit or disk
persistence is added. Manual Windows validation passed for the recorded
revision and external asset pair.

`audio/StereoPeakLimiter.hpp` implements the stateless linked stereo limiter.
The FluidSynth adapter applies it after each successful synth render segment,
after master/channel gain, before device or headless output. It is always
enabled, including legacy file-player output. No CLI toggle or limiter state
is added. Double intermediates preserve linked gain for extreme finite float
inputs; final PCM remains float. Hardware-free tests cover the contract vectors,
finite output, stereo balance, bypass, and independence from block partitions.
The headless smoke test checks the shared boundary for compiled and legacy
playback, including a maximum-velocity chord. Manual Windows audio validation
passed for the recorded revision and external asset pair. The numerical tests
remain the evidence for the limiter ceiling.

The v0.2.0 implementation was reviewed in separate slices: the pure velocity
helper, preset model and atomic publication, limiter and render integration,
then velocity and preset backend/console integration. Manual Windows device
validation passed after integration. It supplements hardware-free tests and
both CI jobs. Future changes require updated contracts, tests, and acceptance
evidence before release.
