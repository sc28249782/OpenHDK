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

The retained mutation-rejection/clean-quit probe has been audited below.
A later user-attended synthetic listening observation is recorded at the end
of this document. The [interactive manual procedure](LIBRARY-PLAYBACK-MANUAL-CHECK.md)
and broader manual playback scope remain distinct from that short run.
Review the remaining scoped requirements before any step 5 acceptance. Count-prefix evidence does not establish
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

## User-attended synthetic listening observation (2026-10-17)

The maintainer reported hearing a brief sound, then confirmed that it came
through the intended headphones without audible distortion or interruption.
These are the maintainer's observations from one short synthetic run, not
an automated assessment of hearing or measured audio quality.

The bridge reports one execution, without retry, after local command approval.
The wrapper interval was 2026-10-17 14:50:31.419Z to 14:50:35.205Z
(21:50:31.419 to 21:50:35.205 Asia/Bangkok). This interval includes process
setup and cleanup; it is not an exact measured acoustic playback interval.
The fixture's last-event time remains 1500000 microseconds.

The tested source/executable/runner4f and public SoundFont pins are the same
as the earlier stop/finish probe above. The bridge's sanitized result reports
the following samples, with generation 1, Snapshot/CompiledTimeline and
failure None:

| clock_phase | clock_media_us | clock_revision | observed_cues |
| --- | ---: | ---: | ---: |
| Playing | 355873 | 44 | 2 |
| Playing | 730476 | 84 | 3 |
| Playing | 1067619 | 120 | 4 |
| Playing | 1423492 | 158 | 4 |
| Finished | 1517142 | 168 | 4 |

The reported sequence then receives stopped acknowledgment, inactive/Unbound
status with null generation and quit acknowledgment. App and runner exit zero;
both streams reach actual EOF, the child is reaped and not alive, and the
wrapper reports no leftover owned processes or watchdog expiry. Stderr retains
only the public SoundFont ROM warning. These are bridge-reported control-path
observations, distinct from the maintainer's listening report.

This documentation uses the returned sanitized execution result and the
maintainer's chat confirmations. It does not claim a new raw-evidence audit
of this listening run. Earlier raw-evidence audits retain their own scopes.
The local approval command did not present a choice menu according to the
maintainer; the same pending run subsequently completed. No second playback
was started in response to that report.

This is a user-attended automated finish probe, not execution of every command
in the interactive manual procedure. It establishes a narrow reported audible
result on the intended headphones. It does not establish longer playback,
musical fidelity, rendered lyric text/highlighting, native Windows playback
or overall step 5 runtime acceptance. Step 5 and NCN remain OPEN, Windows
checkpoint acknowledgment remains BLOCKED, and
source_to_binary_independently_proven remains false.

## Real-executable lyric text prefix probe (2026-10-18)

This is bridge-reported execution and retained-record consistency evidence for
the opt-in `lyrics` command added in PR #114. It covers generated ASCII cue
text, clock alignment, natural finish and explicit stop/quit cleanup on WSL2
ext4. It is not a new human-listening report, GUI/highlighting validation,
independent original-run authentication or overall runtime acceptance.

| Item | Pin or scope |
| --- | --- |
| Tested source | `06b7ee5aec6fce4ef9e36a97aed73c8a913d65c8` |
| Source tree | `b9a6ab0c7816f69bd29d5c3f5f7a4f0a83867c8c` |
| Audio executable SHA256 | `bddc4e9b5bd923f393664e69efa1b3db5c910dcdec00567ed1415e869cbec8b2` |
| Management executable SHA256 | `a5f0eaac7b5bc10ab3e71f2358b421e1eefbeafc806292daa4b4f528ea7d1726` |
| Runner5b SHA256 | `8156f52e7cb17a98434a06b59f4f169129be9a6e627e54a6cef3269dd1247d14` |
| Preserved runner5a SHA256 | `1880e39c22ed3ab5c770ca92d9e2967a215ea392e055dce630d254c3fc88a74e` |
| Generated fixture SHA256 | `2605ec101a762e9b50cf97e243a5b54d687a27972ba3f6a4e2beae05d0af220d` |
| Public SoundFont SHA256 | `52132b2b83994f0067d3a66b93b4f1b67d53ff8c0d3be90d39f23f92b4585cdd` |

The fresh strict build used AUDIO ON and `-Wall -Wextra -Werror -pedantic`;
only the audio and management executable targets were built, with tests OFF.
This build is not a fresh 38-suite CTest run. The public SoundFont is
FluidSynth's `VintageDreamsWaves-v2.sf2`, pinned upstream at
`3ede3f7c8ffc5d16e5d28f416cbfd71f3ad2b814`. Executable and runner pins were
reported stable before/after execution. Source-to-binary independently proven
remains `false`.

### Verifier review and setup failure

Runner5b is an immutable successor to runner5a and the retained runner4f
baseline above. Review found that runner5a could count Paused prefixes toward
Playing progression. Runner5b only counts actual Playing samples; it also
requires integer idle counts, an explicit null idle generation and complete
Held diagnostics. Preparing/Held may retain a prefix while raw media time is
lower; it must not be treated as a newly committed Playing position.

The bridge reports 15 final offline control outcomes matching expectations
(two expected passes and thirteen expected failures). Controls reject wrong,
future or premature text; missing, true or numeric truncation flags; generation
mismatch; missing finish; stale post-stop text; Paused-only progression;
missing Held diagnostics; float idle counts; and missing idle generation.
The valid path and a retained-prefix Preparing/Held path pass. Earlier failed
control/setup attempts and all runner versions remain local. These are
verifier checks, not fifteen production playback runs.

The first management preflight failed before audio spawn because its store
parent directory had been removed during setup. Its retained error was service
code 5, store code 7, native error "2". No audio attempt occurred in that
failed setup. The corrected preflight used a new exclusive evidence directory,
an existing store parent and an absent primary file; old failure logs were
preserved. It exited zero with one candidate, zero diagnostics, 65 source
bytes, eight events, four cues, profile 1, track 0, utf8 and last event
1500000 microseconds. The synthetic format-0 fixture uses PPQN 480,
tempo 500000 and invented FF05 labels a/b/c/d at
0/350000/700000/1050000 microseconds.

### Single live result and record comparison

One live execution followed the corrected preflight, without a live retry.
The reported wrapper interval is 2026-10-18 04:31:40.013Z to 04:31:43.772Z;
it includes setup and cleanup, not a measured acoustic interval. The runner
budget was 25 seconds with a 60-second outer watchdog, which did not expire.

Each selected `lyrics` record reports Snapshot / CompiledTimeline / failure
None, generation 1 matching the play acknowledgment and
`text_truncated:false`. Counts and text are checked against cue times less
than or equal to `clock_media_us` in that same record.

| clock_phase | clock_media_us | clock_revision | observed_cues | text |
| --- | ---: | ---: | ---: | --- |
| Playing | 355873 | 44 | 2 | ab |
| Playing | 711746 | 82 | 3 | abc |
| Playing | 1067619 | 120 | 4 | abcd |
| Playing | 1404761 | 156 | 4 | abcd |
| Finished | 1517142 | 168 | 4 | abcd |

The first poll sees two cues together; no separate one-cue observation is
claimed. Distinct Playing counts are 2, 3 and 4. Finished comes directly from
the clock record and retains the full current prefix; it is not inferred
from elapsed time or activity.

Explicit stop is acknowledged, followed by an inactive/Unbound `lyrics`
record with null generation, integer zero observed cues, empty text and
`text_truncated:false`, then quit acknowledgment. App and runner exit zero.
Both streams reach actual EOF, the owned child is reaped and no longer alive,
and exact-token process checks report no owned leftovers before/after.
The public SoundFont ROM warning remains in stderr.

The bridge reports comparison of all fifteen raw stdout response records
with the transcript and runner/wrapper receipts. This is retained-record
consistency evidence, not independent execution authentication. Original
synthetic evidence, failure logs and local verifier tools remain local;
this document is a sanitized summary rather than a reproducible verifier
package. No private media or private paths/hashes are published.

The earlier listening report retains its earlier executable scope. This new
run establishes bounded ASCII plain-text prefix observations only; it does
not demonstrate Thai display, line/paragraph layout, clipping in live playback,
native Windows playback or the complete interactive manual procedure.
Step 5 and NCN remain OPEN, Windows checkpoint acknowledgment remains BLOCKED,
and no release or runtime-acceptance checkbox advances.
