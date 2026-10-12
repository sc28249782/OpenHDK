# NCN representative member and playback observations

Status: collection tools implemented; representative execution **Pending**.
Related proposal: [OHK-NCN-EVIDENCE-030](NCN24-FORMAT-EVIDENCE-SUPPLEMENT.md).
NCN gate: **OPEN**. This procedure does not authorize a normalizer or accept a
mapping/sentinel policy. No private collection or legacy playback was run to
validate these tools; tool tests use original synthetic members and mock records.

## 1. Select a lawful sample and reader

Use a local collection you have permission to inspect. Keep the three members
and original playback artifacts outside Git. Select the exact MID/LYR/CUR triple;
do not glob/search an entire collection. Choose an opaque ID `sample-NNN`, with
three decimal digits. Do not put a song title or username in the ID.

Record the legacy application name, version and build reference. Record the
platform and how a position was measured. A pinned source snippet is not a
playback observation. The collector neither starts nor controls the player.
Use a trusted local directory. The collector rejects final symlinks and reads
regular files only; ancestor-path isolation and hostile-writer protection are
not claimed. Stop other programs from editing the selected members during the run.

Prioritize samples with non-60 BPM, tempo changes, Thai marks, spaces, CRLF,
blank/terminal lines and high-byte FF. A missing feature is Skipped with a reason,
not Passed. One representative sample need not contain every feature. Do not
truncate or edit a song to make it fit a bound or expected count.

## 2. Collect privacy-limited member inventory

Run from the repository root in WSL/Linux. Supply the three exact member paths
and up to 64 zero-based CUR indices. This example uses caller-set shell variables;
their values remain private and must identify real files, not synthetic placeholders.

```sh
python3 tests/collect-ncn-member-inventory.py --sample-id sample-001 --permission-basis lawful-private --mid "$ncn_mid" --lyr "$ncn_lyr" --cur "$ncn_cur" --indices 0,1,2 > "$ncn_inventory"
```

Choose a new inventory output file outside the checkout. Shell redirection can
overwrite a file; select the destination before running. Omit `--indices` for an
initial count scan. Re-run with actual in-range indices for observations. The
collector does not write to members, fix lyrics, drop cursor values, decode MIDI
track events or infer timing. Exit 0 means the inventory was emitted, not that
these members are a valid or compatible NCN song. Exit 1 is a read/limit failure;
exit 64 is a sanitized usage failure. Errors contain no native exception text.

The output contains member lengths/hashes, SMF container/header observations,
explicit TIS-620/newline count hypotheses, unsigned little-endian CUR statistics
and selected index/value/byte-offset rows. It contains no lyric/header text,
raw wires, absolute paths, username or inode/authority tokens. Unknown or invalid
LYR structure is reported as unavailable counts, without charset retry. Raw odd
CUR lengths and decreasing positions remain in the observations. Schema 3 adds
explicit framing provenance; it never removes complete words or edits members.

`ContainerObserved` checks framing only. It is **not** `SmfParser`/timeline
validation: tempo map, track events and last tick remain Pending. Count equality
is explicitly not compatibility. High-byte FF is counted under retain and
historical-skip hypotheses, but `high_ff_policy` remains Unresolved.

The input permission basis is a caller statement, not independent verification.
The record includes the collector's file hash and Python version. Also capture
the exact Git revision, clean/dirty status and exit code outside the inventory.
A script hash establishes supplied-byte consistency, not source/run provenance.

| Collector resource | Bound |
| --- | --- |
| MID / LYR / CUR | 4 MiB / 64 KiB / 64 KiB |
| Aggregate raw member bytes | 4 MiB, one allowance across the triple |
| Shared logical coexistence staging | 9 MiB |
| Output | 64 KiB |
| Selected CUR positions | 64 |
| Stored anomaly/high-FF index samples | First 16, with explicit truncation flags |

Staging reserves retained inputs, a maximum read-copy overlap, retained/serialized
output and conversion buffers. No decoded lyric string/vector is retained.
Payload bounds do not measure Python/container/allocator overhead or total
process memory. Per-file size/mtime checks do not create an atomic three-file
snapshot, and local read calls do not promise a universal I/O deadline.

## 3. Observe legacy behavior before deciding a hypothesis

1. Keep the inventory and member hashes. Open the matching triple in the selected
   legacy reader. Record normal playback and seeking separately; line repair or
   seek state can change cursor behavior.
2. Capture positions at a non-60 BPM quarter boundary. Record the actual cursor
   index and any MIDI tick/media value the reader exposes. Do not substitute a
   candidate formula for an observed position. If the player exposes no exact
   tick/time, leave the corresponding measured field null.
3. Observe Thai mark transitions, line entry versus line break, empty/terminal
   lines and selected high-byte FF entries. Distinguish visible changes from
   indices read through instrumentation. Do not infer scalar correspondence
   merely because total counts match.
4. Retain private artifacts with SHA-256 references. A visual capture alone cannot
   supply an exact MIDI tick or media microsecond measurement. Record its method
   as `visual`; both exact measurement fields must be null. Reader-reported and
   instrumented measurements use distinct method names.
5. Obtain the full primary tempo map and last tick with reviewed primary SMF
   analysis, independently of the cursor hypothesis. This collector does not
   provide that analysis. Mark it Pending until it is available. Then compare
   musical-unit and elapsed-time alternatives at discriminating positions.

Do not publish songs, unredacted screenshots or player logs containing paths,
usernames or private lyrics. Store artifacts privately; the observation record
uses only their hashes. Review supplied text and any publication/redaction rights
before committing evidence. Do not relabel a missing observation as Passed.

## 4. Bind and check a supplied observation record

Copy [observation-template.json](../tests/fixtures/ncn24/observation-template.json)
to a new private file. Initially every topic is Pending and the observation list
is empty. This is a template, not an execution receipt. Populate the sample ID,
reader/version/platform and the exact SHA-256 of the saved inventory file.

Add at most 128 actual observations. Each row has these exact fields:

| Field | Meaning |
| --- | --- |
| `id`, `topic` | Unique `obs-NNN`; topic is one of the six coverage keys. |
| `cursor_index` | An index present in the inventory's selected positions. |
| `measured_midi_tick`, `measured_media_us` | Independently observed unsigned integer or null; both null for visual-only observations. |
| `action` | `Text`, `LineTransition`, `EntryMarker`, `Skipped` or `Unavailable`; no lyric text. |
| `method` | `visual`, `reader-report` or `instrumented`; must match this run's method. |
| `artifact_sha256` | SHA-256 of a privately retained observation artifact. |

Use a separate record for each observation method. Set `run.status=Completed`
only after recording actual observations and supplying reader/version/platform.
Coverage statuses are `Observed`, `Pending` or `Skipped`, not Passed. Observed
means an artifact-backed claim was supplied, not a match to NCN24. Use reason
`recorded`, `not-collected`, or a specified skip reason from the template/checker.
Do not insert mock rows from tests into a real evidence record.

Check the saved files:

```sh
python3 tests/verify-ncn-observation-record.py "$ncn_inventory" "$ncn_record"
```

The verifier caps each file at 64 KiB, supplied observations at 128, JSON depth
at 12, shared logical staging at 512 KiB and summary output at 4 KiB. It rejects
duplicate keys, unknown record fields, wrong inventory hash, unsampled indices,
false coverage, visual exact-timing claims and a changed acceptance label.
`observation-record-checked` means structure and supplied-byte binding agree.
It does not authenticate playback, re-read members, validate the complete
inventory or prove that the tempo map or manual measurements are correct.
Its summary keeps playback authentication NotIndependent and tempo/mapping/
sentinel review Pending. It never returns NCN acceptance.

## 5. Handoff for scoped review

Provide the exact reader/version, sample inventory, populated observation record,
command/exit receipts and separately obtained primary tempo analysis. State each
missing/skipped feature. Supply private members/artifacts separately only when
permission allows; keep them out of Git. Review source/count/timing conflicts
and amend OHK-LYR-030 before implementing a normalizer if evidence disagrees.
No gate closes merely because the collector or verifier returns exit 0.
CLI playback and Windows checkpoint/manual gates remain separate.

## Legacy edge hypotheses

The [legacy edge analysis](NCN-LEGACY-EDGE-ANALYSIS.md) examines pinned reader handling
of SUB text, incomplete CUR words and decreasing cursor positions.
Four original edge vectors and 32 tests apply first-SUB EOF and optional
terminal FF framing while retaining non-FF odd-tail/decreasing rejection.
No measured mapping, authenticated playback or normalizer acceptance is claimed.
NCN evidence remains OPEN; no contract, CTest-count or checkbox change.

## Inventory interpretation after EOF amendment

The earlier collector output was inventory schema 2, with first-SUB EOF provenance
and logical body counts. Member lengths/hashes still cover raw bytes after EOF.
Schema-1 submitted records retain the previous strict-C0 interpretation. The
verifier labels both versions and binds observations to exact saved inventory
bytes. Do not edit an old receipt or reuse its hash for a rerun. Save a new
receipt separately; use its own hash and CUR selections for new observations.
LYR EOF acceptance alone does not accept CUR anomalies or prove mapping/timing.
The separate CUR amendment below admits only terminal singleton FF framing.

## Accepted NCN24 optional CUR terminator tooling

The [OHK-NCN-CUR-030 amendment](NCN24-CUR-FRAMING-PROPOSAL.md) was accepted with PR #96.
The contract and schema-3 offline tools now select complete little-endian words
at physical EOF, or exclude exactly one terminal FF byte from odd-length CUR.
Non-FF odd tails still reject. Complete FF words, padding/trailer hypotheses,
ordering, mapping and timing rules remain unchanged. Raw member hashes, caps
and shared accounting include the marker. The manifest has 69 cases (51 prior
plus 18 CUR framing vectors); current checker input rejects schemas 1 and 2.
Inventory keeps raw trailing-byte statistics beside framing provenance.
The observation record remains schema 1; its verifier labels inventory schemas
1/2 as Unframed CUR and schema 3 as OptionalTerminalFF, with arithmetic checks.
Preserve submitted receipts and rerun samples into new files. No application
NCN normalizer exists; the evidence gate remains OPEN. Runtime/suite counts,
checkboxes, historical bundles and storage acceptances do not change. Windows
checkpoint acknowledgment remains BLOCKED.
