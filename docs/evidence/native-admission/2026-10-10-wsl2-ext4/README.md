# Maintainer WSL2 ext4 native admission run

Status: Supplied receipts verified; explicit scoped native factory acceptance
pending. This record does not extend or replace the historical `6a6337d` provider
acceptance.

## Tested revision and supplied archive

- Exact tested revision: `4f6924670d4272736984342c8294967650c60edb`
  (PR #74 merge; includes experimental factory/fence and receipt instrumentation).
- Supplied archive: `native-admission-receipts.tar.gz`.
- Original archive SHA-256:
  `88fe7d3680ce9af0ddc3325aa5354fb873d54e4422d21da045ec6b132727b51c`.
- Archive has 61 files: 60 logs and SHA256SUMS. The archive binary is not duplicated
  in git. The derived README, redaction.json and verification.json are separate.
- `environment.log` and `source-after.log` pin the exact revision; source-after
  contains only that revision line. Clean source is reported by the collector.
  This is supplied evidence, not independent source/run authentication.

The collector ran directly with GCC 13.3.0 on WSL2 kernel
`6.18.40.1-microsoft-standard-WSL2`, `/dev/sdd` ext4, `data=ordered`.
`SANITIZERS 0` means this run is neither an ASan/UBSan run nor CTest. Provider
reports `ext4-eligibility=1 test-filesystem-bypass=0`; binding, service and provider
evidence targets report bypass zero. There is no overlay bypass in this run.

## Verified observations

All original manifest entries were checked before redaction. The published
manifest was recomputed and checked after the documented replacements. The
production receipt verifier was rerun on supplied admission logs and its output
matches `native-admission-verification.log` exactly. Independent Python
hashlib/struct checks verify supplied provider full-wire hashes, lengths, valid
schema-1 footer digests and cut-result consistency. Intentionally invalid magic,
unsupported schema, empty and partial inputs are rejection fixtures; they must
not be described as accepted checkpoints.

| Observation | Recorded result |
| --- | --- |
| Compile/run receipts | 28 compile and 28 run exits, all zero |
| Admission verifier / overall | Both exit zero |
| Admission traces | 13, each overflow zero |
| Identity observations | 364 (363 actual, one injected differing mount ID) |
| Fence cases | Three: NoChange, final changed drift, NoChange drift |
| Binding/service checks | 74 / 33 |
| Factory allocation cleanup | 49 failures before success; FD baseline/final both six |
| Provider wires | 384: 280 valid, 99 empty, two partial, one invalid magic, two unsupported schema |
| Provider traces | 106, each overflow zero |
| Provider native records | 1,250 actual, 11 injected |
| Interruption cuts | Four phases, two repeats; observed revisions match expected in all eight cases |

Fresh/cache export, foreign ownership, release/reacquisition, real `/proc` mount
rejection and injected mount failures are separately recorded. Ancestor
termination is recorded. Root replacement is an actual harness rename/create
before the final identity check. It does not prove hostile-writer isolation or
atomic pathname CAS.

Changed and NoChange drift retain exact prior primary bytes and immutable memory
publication; before/after SERVICE_PRIMARY hashes/size/sequence/revision agree.
Both cases enter RecoveryRequired. RootId mapping is reported as equality, not a
serialized authority value. Restored unattached roots produce no root identity
observations. Injected publication-sync failure is Uncertain and retains memory;
validated fresh reopen observes the newly written override. Names and scope of
these cases are in [receipt semantics](../../../NATIVE-ADMISSION-EVIDENCE.md).

## Redaction and limits

The local username was replaced with `maintainer` in exactly three logs:
environment.log (three replacements), linux_checkpoint_provider_tests.run.log
(one) and linux_checkpoint_evidence_tests.run.log (one). No numeric, wire,
identity, outcome or exit-code fields changed. `redaction.json` records original
log hashes, changed-log replacement counts and original/published manifest hashes.
Original unredacted bytes are not published here. Historical bundles are unchanged.

Hashes establish supplied-byte consistency, not provenance or authentication.
Selected-call observations are not a full syscall trace. The private
mount-namespace alias case remains Skipped, not Passed. Harness artifacts were
removed; captured observations remain in logs, not inspectable live artifacts.
Scope is WSL2 virtual-disk ext4 only. There is no bare-metal, Windows/NTFS, DrvFS,
cross-OS, hostile-writer or power-loss approval. Explicit scoped factory
acceptance remains pending review; durable root lifecycle, app/CLI orchestration,
NCN evidence and manual validation remain separate work.
