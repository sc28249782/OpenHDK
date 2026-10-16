# Legacy runtime evidence status

**Status:** Sanitized private evidence summary through 2026-10-16. This
document records observations from copied legacy test packages. It is not an
OpenHDK implementation or acceptance record.

## Purpose

The NCN24 evidence work uses isolated legacy probes to understand reader,
handoff, ownership, startup, and shutdown behavior. The probes use private
packages because the legacy application depends on Qt and BASS-family
components. OpenHDK core does not import those dependencies.

This document publishes only status and limits. It does not publish song,
lyric, corpus, user-profile, media-path, or media-hash data. Detailed receipts
remain with their private packages.

## Evidence summary

| Evidence class | Sanitized result | Boundary |
| --- | --- | --- |
| G4 isolation and tiny reader | Source/build and five negative-isolation cases passed. The first native tiny-runner receipt failed hex-expectation checks; a later bounded offline receipt review passed, and the provenance consistency review passed. | These reviews do not authenticate source-to-binary correspondence. The inventory covers only observed directories and does not prove that no write occurred outside them. |
| Startup wrapper and safe-mode probe | Synthetic wrapper checks passed. A private safe-mode startup and confirmed close passed without selecting or playing a song. | This is not a playback trace and does not validate an NCN bundle. |
| G5 identity and integration | Identity, lineage, widget, and font-controlled integration checks passed with synthetic inputs. | No representative song playback occurred. |
| Medley handoff | Slot and handoff contracts passed in the copied harness. | The harness does not authenticate a full legacy player playback run. |
| Real model ownership | Twelve ownership-transfer checks passed with a real legacy `MidiFile` and a stopped `MidiSequencer`. | The sequencer did not start in this evidence class. |
| Active-engine stop acknowledgement | Seven of nine checks passed. Normal bounded stop, repeated stop, model validity, and process isolation passed without timeout or forced termination. | Two checks exposed legacy engine state defects. The overall native verdict is FAIL. |

## Active-engine observations

The copied legacy engine exposed two behaviors with a synthetic in-memory SMF:

1. A stop request can be lost between `start()` and entry to `run()`.
   `stop()` can observe the old stopped flag and return before `run()`
   changes that flag.
2. Natural thread completion can leave the public playing flag set. The thread
   is no longer running, but `isSeqPlaying()` can still report true.

These observations support OpenHDK's explicit generation, terminal-state, and
stop-acknowledgement contracts. They do not prove that the OpenHDK
implementation has the same defects.

A reporting-only v3 copy kept the nine cases, assertions, and time budgets
unchanged. Its native result was seven PASS and two FAIL (process exit 1), with
no timeout, forced termination, or stderr. Under its immediate-stop synthetic
schedule, all 12 of 12 stop requests were lost; none took effect. After
natural completion, the thread was not running, but the public flags were
playing=true, stopped=false, paused=false, and finished=true. These counts
describe this bounded fixture, not a population-wide failure rate.

The v3 receipt was read back from the private package. The v2 failed receipt
and package remain unchanged. A separate proposal may test a legacy fix in a
new copy; no legacy behavior or OpenHDK contract changed in this step.

## Non-claims

This evidence does not:

- close the NCN24 evidence gate;
- satisfy the blocked Windows checkpoint acknowledgement;
- admit an application NCN parser or normalizer;
- provide an actual representative playback trace;
- validate NCN mapping, tempo, glyph rendering, or audio output;
- provide independent source-to-binary authentication;
- import Qt or any BASS-family dependency into OpenHDK core; or
- change a released feature, test count, roadmap checkbox, or historical
  acceptance decision.

The authoritative OpenHDK contracts remain
[the specification](SPECIFICATION.md),
[the playback-session contract](PLAYBACK-SESSION-CONTRACT.md), and
[the media-clock handoff contract](MEDIA-CLOCK-HANDOFF-CONTRACT.md).
