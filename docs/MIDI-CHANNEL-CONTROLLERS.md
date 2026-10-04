# MIDI channel controllers reference

This document records the MIDI 1.0 controller semantics that OpenHDK must
preserve when it analyses or plays a Standard MIDI File (SMF).  It is a
reference for implementation and tests, not a claim that every SMF or
synthesizer implements every optional MIDI feature.

## Authoritative references

- [The MIDI Association: MIDI 1.0 Control Change Messages](https://midi.org/midi-1-0-control-change-messages)
- [The MIDI Association: About MIDI—Part 4: MIDI Files](https://midi.org/about-midi-part-4midi-files)

## Relevant controllers

| Controller | Meaning | Value range | OpenHDK implication |
|---|---|---:|---|
| CC7 | Channel Volume (MSB; formerly Main Volume) | 0–127 | Primary channel-volume automation. |
| CC39 | LSB for CC7 | 0–127 | Optional fine resolution paired with CC7. |
| CC11 | Expression (MSB) | 0–127 | Separate level control that can change perceived loudness even when CC7 is unchanged. |
| CC43 | LSB for CC11 | 0–127 | Optional fine resolution paired with CC11. |
| CC121 | Reset All Controllers | 0–127 | Reset handling must remain explicit and deterministic. |

CC7 and CC11 are channel messages: they affect only the MIDI channel encoded
by the event.  They are not note velocity, master volume, or a guarantee of a
particular acoustic loudness.

## Initial values are an authoring convention, not file metadata

The MIDI Association's SMF authoring guidance lists an initial CC7 level of
100 and initial CC11 level of 127.  An SMF may send those values before its
first note, send different values, send none at all, or automate them during
playback.  A synthesizer can also have its own reset/default state.

Consequently, a final CC7 observation means only the last CC7 event in
deterministic timeline order.  It must not be presented as a channel's initial
preset, average loudness, or effective final loudness.  For example, a final
CC7 value of zero can be a deliberate end-of-song fade after many audible
notes.

## Fourteen-bit controller pairs

MIDI 1.0 defines CC39 as the LSB associated with CC7, and CC43 as the LSB
associated with CC11.  When an SMF actually emits an LSB, a diagnostic may
report the combined value as:

```
combined14 = (msb << 7) | lsb
```

The implementation must retain the most recently observed MSB and LSB for the
same channel and respect the compiled event order, including events with the
same timestamp.  When no LSB was observed, diagnostics should keep the native
7-bit value rather than inventing a 14-bit value.

## OpenHDK playback contract

OpenHDK's launch-time `--channel-volume <1-16>:<0-100>` trim is a user gain,
not an edit to the SMF.  It multiplies the source CC7 value before sending it
to FluidSynth.  Therefore a CC7 automation curve, including a zero fade, is
preserved while the user can lower or mute a channel.  CC121 restores the
source CC7 default of 100 before applying that same user gain.

CC7=100 is the conventional initial level, not an OpenHDK assertion of
``unity gain`` or a fixed dB conversion.  Mapping controller values to audible
gain depends on the synthesizer, SoundFont, expression state, velocity,
program/bank, and effects.  Do not make a logarithmic formula part of the
OpenHDK compatibility contract without a separately verified FluidSynth
requirement.

When a runtime mute or solo transition makes a channel's effective output gain
zero, the render path sends CC120 (All Sound Off) to stop voices already
sounding on that channel. Unmuting or removing solo does not recreate those
voices; it affects subsequent note-on events.

## Diagnostics output

`--midi-diagnostics` reports note-on count and observed programs, then reports
the following per controller: first observation, most recent observation before
the channel's first audible note, final observation, 7-bit range, and count of
actual value changes. Samples are rendered as `<value>@<tick>t/<microseconds>us`.

The command also reports a final 14-bit value for CC7 and CC11 only after the
corresponding MSB and LSB have both occurred on that channel. It does not
invent a fine-resolution value from an MSB alone. It separately reports the
count of CC121 Reset All Controllers events; controller series remain raw SMF
observations and do not invent an implied CC7 or CC11 event after a reset.

For the OpenHDK playback path, `effective-cc7` is also reported. It starts only
when an SMF emits CC7 or CC121, follows raw CC7 events, and becomes 100 at each
CC121 reset because `FluidSynthBackend` restores that source CC7 value before
applying a launch-time channel trim. This is a backend-contract diagnostic, not
a claim about every MIDI synthesizer's internal reset semantics.

Use public, synthetic SMF fixtures for all automated tests.  MIDI and
SoundFont files obtained from private groups remain external manual-validation
assets and must not be committed or included in releases.
