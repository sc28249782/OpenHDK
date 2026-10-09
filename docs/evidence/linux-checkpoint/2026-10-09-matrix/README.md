# Pinned WSL2 ext4 matrix receipt run — 2026-10-09

**Status:** Received and checked; explicit native reviewer acceptance Pending.
**Revision:** `6a6337df03fb69326cc5096eb2ef34b6e32bac11` (PR #64 merge).
**Supplier:** Somchai Pongkasem. The documentation editor checked submitted
bytes; the editor did not execute this native run.

## Submitted bundle and public copies

The uploaded `matrix-receipts.tar.gz` has SHA-256
`82844a77a07d83b5cb8ea150dd5895908ef207b42d8377c08e81fa24d4cf90d3`.
Its 52 regular entries were checked privately: 51 logs and SHA256SUMS, including
empty logs. All 51 original manifest hashes match. The archive is not duplicated
in git. At the maintainer’s request, three published logs replace the literal
local username with `maintainer`; all other log bytes are unchanged.
[redaction.json](redaction.json) records replacement counts and original/published
hashes. Published SHA256SUMS was recomputed and covers the public logs only;
this README, verification.json and redaction.json are separate derived records.
Earlier evidence bundles remain unchanged.

The collector reports the exact revision before and after execution, clean=1,
DIFF_CHECK exit=0, 24 compiler exits=0, 24 test exits=0 and OVERALL exit=0.
It used direct g++ C++20 with strict warnings, -O1 -g -pthread, not CTest;
SANITIZERS 0. Compile/run deadlines were 180/120 seconds.

Environment: WSL2 Linux 6.18.40.1-microsoft-standard-WSL2, x86_64; Ubuntu GCC
13.3.0-6ubuntu2~24.04.1; /dev/sdd ext4 mounted with
rw,relatime,discard,errors=remount-ro,data=ordered. The source was
/home/maintainer/openhdk-matrix-src-zcFjQd and the harness parent was
/home/maintainer/openhdk-matrix-ext4-sUBj7j. Provider output records
ext4-eligibility=1 test-filesystem-bypass=0; the evidence target records
filesystem-bypass=0.

## Independent checks of supplied receipts

Python hashlib verified manifest/full-wire/footer hashes. Python struct unpack
checked all complete schema-1 synthetic header/root/song records against the
fixture layout; projection and acknowledged token fields agree. Counts and
observations are retained in [verification.json](verification.json).

| Observation | Checked result |
| --- | --- |
| Wire receipts | 384: 280 valid checkpoints, 99 empty entries, 2 seven-byte partial captures, 1 invalid-magic primary and 2 unsupported-schema primaries. All full-wire hashes match; intentionally rejected inputs are not accepted checkpoints. |
| Projections / acknowledgments | 280 / 4, consistent with decoded sequence/revision and footer digest. |
| Selected-call traces | 106, every declared count matches and overflow=0; 1,250 actual returns and 11 injected records. |
| Artifact captures | 137, captured byte counts equal sizes, capture_errno=0. |
| Preservation | 61 byte-equality/hash receipts and 6 pathname-identity receipts agree. |
| Interruption | Four phases × two repetitions; expected revisions: 9,9 / 9,9 / 10,11 / 12,13. |
| Allocation sweep | 31 failed budgets preserve primary; budget 31 succeeds. |

## Matrix observations

| Group | Recorded cases |
| --- | --- |
| Ownership/path | Same-provider/second-store and same-process contenders Busy; fresh-exec contender Busy with actual EAGAIN. Owner release and owned-child death permit reacquisition; stable lock retained. Six primary/lock symlink/hard-link/FIFO traps retain identity and bytes; ancestor symlink and directory replacement reject. |
| Short-I/O/allocation | Forced seven-byte requests complete 189-byte read/write with one injected EINTR each. Injected EIO follows an actual seven-byte write prefix, captured before cleanup. Allocation failures and subsequent success have per-budget result/preservation receipts. |
| External replacement | Replacement before final publication identity check returns SourceChanged/NotCommitted and preserves both files. ExpectedAbsent race returns actual EEXIST without overwrite. Replaced stage cleanup retains original and unrelated replacement. |
| Invalid/missing primary | Invalid magic and schemas 0/2 reject without projection/token or stage promotion. Missing primary after injected CommitUncertain retains faulted state; explicit restoration and validated reopen clear it. |

`PUBLISH_CASE outcome=1` is `StorePublication::NotCommitted`.
`SAVE outcome=1` is `StoreOutcome::Uncertain`. These are different enums;
interpret them using CatalogCheckpointStore.hpp, not by the number alone.

## Limits and remaining decision

These are maintainer-supplied observations, not independent source/run
authentication. Hashes establish consistency of supplied bytes, not provenance.
Instrumentation records selected calls, not every lease walk/stat/flock syscall.
Seven-byte requests, EINTR/EIO and allocation faults are test-controlled. Process
termination is not power loss. Replacement precedes the final identity check;
it does not prove atomic pathname compare-and-swap or hostile-writer isolation.
Harness filesystem artifacts were removed; captured bytes remain in logs only.

The run adds representative observations for all four extended groups. Per-row
completeness and explicit reviewer acceptance remain Pending. No bare-metal
Linux, Windows/NTFS, cross-OS/DrvFS or universal power-loss support is established.
Windows synchronization, durable-first wiring, NCN/application and release gates
remain open. No runtime, dependency, version or roadmap checkbox changes here.
