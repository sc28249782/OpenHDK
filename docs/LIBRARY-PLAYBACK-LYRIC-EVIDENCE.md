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

Review mutation rejection while playback is active together with clean quit,
and perform the planned human listening and manual playback/lyric validation
before any scoped step 5 acceptance. Count-prefix evidence does not establish
display text rendering, output quality or user acceptance. Windows playback
validation remains separate from the blocked Windows checkpoint protocol.
