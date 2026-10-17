# Synthetic lyric progression and natural finish (2026-10-17)

## Scope and provenance

This records bridge-reported execution on WSL2 ext4 with the real
OpenHDKLibraryAudio executable. It is bounded control/clock/lyric-count evidence,
not human listening, runtime acceptance, or independent original-run authentication.
Step 5 remains OPEN, NCN remains OPEN, and Windows checkpoint acknowledgment
remains BLOCKED. No release checkbox advances.

| Item | Pin or scope |
| --- | --- |
| Tested source | PR #110 head `0f3a88024a1e43573e93190ae79c06b4dd12f2f8` |
| Merged source | `8327a721bc64fa1344e02c0e2642dc890274e81d`; no file diff from tested head |
| Executable SHA256 | `44abd6ba6c76cb5210dc015fdd0f2968e874e04b7153c1c8ba298449b29248b5` |
| Local runner4f SHA256 | `ecfbe9ce5ea0e080daf0ddd6c6a9c9ae4dc7740c24bf068a4fde499c3353e808` |
| Build | Strict audio ON: `-Wall -Wextra -Werror -pedantic` |
| SoundFont | Public FluidSynth `VintageDreamsWaves-v2.sf2`, upstream pin and expected hash from tested CMake; bridge reports byte match |
| Source-to-binary independently proven | `false` |

The runner, raw transcripts, generated fixture and receipts remain local. This
document publishes a sanitized summary, not a reproducible verifier package.
No private songs, lyrics, corpus, profile paths or private asset hashes are
published. Historical evidence and acceptance revisions remain unchanged.

## Fixture and management preflight

The generated SMF format-0 fixture uses PPQN 480 and tempo 500000. Its four
FF05 lyric labels are invented test markers. Cue times are 0, 350000, 700000
and 1050000 microseconds; the last event is at 1500000 microseconds.
Management preflight through the real parser-only executable, before audio
playback, reports one scan candidate, zero diagnostics, 65 source bytes,
eight events, four cues, LyricMeta profile 1, track 0 and encoding utf8.
It exits zero. Last-event time is not a general audio-duration contract.

## Verifier review and retained attempts

Runner4e initially passed a reported 25-case offline suite, but review found
that its stop target could use two samples with the same cue count and its
finish target could accept Finished as the first sample. Its synthetic child
also ignored the finish-mode argument, so the earlier valid-finish row did
not exercise that path. Do not use that row as finish-path evidence.

The immutable runner4f copy requires at least two distinct cue-prefix counts
from valid Playing snapshots. Stop mode rejects Finished before its target.
Finish mode requires prior Playing progression, then a real Finished record
with four observed cues and media time at least 1500000 microseconds.
Every Advanced sample checks the prefix count against cue times less than or
equal to clock_media_us in that same status record, matching source, failure
and playback generation, positive even nondecreasing revision and monotonic
media time. Held handling and child identity/reaping checks were strengthened.

The bridge reports 31/31 final offline outcomes matching expectations,
including expected failures for unchanged cue counts, Finished-first, absent
progression, stop-too-late, malformed Held diagnostics and backward revision.
These are verifier checks, not 31 production playback passes.

The first live wrapper attempt aborted before spawning audio: a substring
process scan matched its own shell command. It and its receipt remain intact.
The wrapper was corrected to exact-token identity checks, and a new exclusive
directory was used. No failed runtime attempt was repaired or silently rerun.
The corrected stop and finish cases each ran once with a 25-second runner
budget and a 60-second outer watchdog.

## Reported real-executable observations

All samples below report Snapshot / CompiledTimeline / failure None and
clock_generation 1, matching the acknowledged play generation. The count is
the observed prefix size, not a rendering or human-reading claim.

| Case | clock_phase | clock_media_us | clock_revision | observed_cues |
| --- | --- | ---: | ---: | ---: |
| Stop | Playing | 355873 | 44 | 2 |
| Stop | Playing | 711746 | 82 | 3 |
| Finish | Playing | 355873 | 44 | 2 |
| Finish | Playing | 711746 | 82 | 3 |
| Finish | Playing | 1067619 | 120 | 4 |
| Finish | Playing | 1423492 | 158 | 4 |
| Finish | Finished | 1517142 | 168 | 4 |

The first poll sees two cues together; this run does not prove a separate
one-cue observation. Prefix checks use committed media time, not elapsed
wall time. Finished is read directly, never inferred from active or waiting.

Both cases then receive explicit stopped acknowledgment, inactive/Unbound
status with null generation, and quit acknowledgment. App and runner exit
zero; stdout and stderr reach actual EOF; the child is reaped and reported
not alive. The wrapper reports no remaining owned runner/audio processes and
no watchdog expiry. The last pre-stop sample in stop mode is Playing; no
Finished sample is seen in that case. Natural finish retains controller
ownership until the subsequent explicit stop.

Both stderr streams contain only the public test SoundFont warning:
`fluidsynth: warning: Sample 'SineWave': ROM sample ignored`.
It is retained in the local evidence rather than removed from the result.

## Remaining work

The retained mutation-rejection/clean-quit probe has been audited below. Perform
the planned [manual listening and clock observation](LIBRARY-PLAYBACK-MANUAL-CHECK.md)
before any scoped step 5 acceptance. Count-prefix evidence does not establish
display text rendering, output quality or user acceptance. Windows playback
validation remains separate from the blocked Windows checkpoint protocol.

## Reported mutation rejection with clean quit (2026-10-17)

Status: **read-only raw-evidence comparison completed on 2026-10-17**.
The bridge audit compared all 16 raw stdout records with the transcript and
retained runner/wrapper receipts. The records match, including the table below,
pins, expected exits, actual EOF and process identity/cleanup observations.
This is a consistency audit of retained evidence, not independent original-run
authentication or scoped runtime acceptance.

The same pinned PR #110 executable and 65-byte synthetic fixture were used.
The local runner4h SHA256 reported by the bridge is
`8edd14d41734cd685e0f6d3b43e8ade89dc8d6cd87be76d12c6d5e4f54f614fc`.
Runner4g had allowed an inconclusive post-rejection phase with an overall
pass. The immutable runner4h copy instead requires Advanced/Playing before
and after the rejected command. The bridge reports seven preliminary
offline outcomes and three final targeted outcomes matching expectations,
including rejection of post-command Finished or Held. These counts describe
different verifier checks, not ten live runs.

The representative mutating command is `scan 1`; the other five mutating
verbs are outside this probe. One live attempt, without retry, is reported:

| Observation | Reported value |
| --- | --- |
| Acknowledged play generation | 1 |
| Before scan | Playing, media 355873 us, revision 44, observed cues 2 |
| Scan response | error PlaybackActive, commit 3 (Rejected) |
| After scan | Playing, media 767936 us, revision 88, observed cues 3 |
| Catalog page before/after rejection | Byte-equal, catalog revision 2 |
| Explicit stop | stopped, then inactive/Unbound with null generation |
| Scan after stop | Saved, catalog revision 3, one candidate, zero diagnostics |
| Quit and exit | quit acknowledged; expected app exit 1, actual 1; runner exit 0 |
| Streams and process cleanup | Actual stdout/stderr EOF, child reaped, not alive, no leftovers; watchdog did not expire |

Playing samples reportedly match generation 1, Snapshot/CompiledTimeline and
failure None. Their cue counts match the prefix derived from the media time
in the same record. The application exit 1 is intentional: the session
retains the ordinary error from PlaybackActive even after later successful
stop, scan and quit. It must not be relabeled exit zero. This exit policy is
also visible in [LibraryAudioSession](../app/LibraryAudioSession.hpp);
the mutation guard is in [LibraryCliSession](../app/LibraryCliSession.hpp).

The report retains the public SoundFont ROM warning and all runner versions
and attempts. Original evidence and tools remain local. The audit checked table values,
page equality, mode routing, pins, exits, actual EOF and process
identity/cleanup, plus the preliminary/final verifier-control reports.
No fresh execution or human listening is claimed by this documentation change.
