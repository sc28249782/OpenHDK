# Maintainer WSL2 ext4 durable mutation run

Status: Supplied selected mutation receipts verified; explicit scoped native
mutation acceptance Pending. Historical provider `6a6337d` and factory/fence
`4f69246` acceptance records remain separate and unchanged.

## Tested revision and archive

- Exact tested revision: `54f404ae8422499de950f5c96527c9023274a9b6`
  (PR #82 merge, with all five experimental durable mutations and receipts).
- Supplied archive: `mutation-receipts.tar.gz`.
- Original archive SHA-256:
  `98491eeaacdeebb20ab36ad5a3bc153c9f8068ef8db7b82d1ddf110f6930d779`.
- Archive contains 63 files: 62 logs and SHA256SUMS. The archive binary is not
  duplicated in git. README, verification.json and redaction.json are derived files.
- Environment and source-after logs pin the same revision; source-after contains
  only the revision line. The collector reports clean source and DIFF_CHECK=0.
  This is maintainer-supplied evidence, not independent source/run authentication.

The supplied environment is GCC 13.3.0 on WSL2 kernel
`6.18.40.1-microsoft-standard-WSL2`, `/dev/sdd` ext4 with `data=ordered`.
The provider reports `ext4-eligibility=1 test-filesystem-bypass=0`; binding,
native service and provider evidence targets also report filesystem bypass zero.
This is direct-g++ execution with SANITIZERS=0, not CTest or an ASan/UBSan run.

## Verification and observations

All 62 original manifest entries were verified before redaction. Published log
hashes were recomputed and verified after redaction. The mutation verifier,
negative verifier checks and existing admission verifier were rerun using the
pinned scripts; outputs match the submitted verification logs byte-for-byte.
They also pass on published logs. Independent hashlib checks cover 384 supplied
provider full-wire records. They do not independently hash omitted mutation
checkpoint bytes or prove source/run provenance.

| Observation | Recorded result |
| --- | --- |
| Compile / run / overall exits | 28 / 28 / overall, all zero |
| Verifier exits | Admission, mutation and negative checks: all zero |
| Mutation matrix | 34 cases: five operations × five scenarios, plus nine specials |
| Primary receipts | 69, including before/after and remove/rediscovery intermediate state |
| Mutation admission / IO traces | 32 / 25, overflow zero |
| Mutation artifact observations | 45: seven bounded captures available, 38 oversize EFBIG/unavailable |
| Allocation sweeps | Register 54, reattach 84, scan 14, relocate 73, remove 64 failures before success |
| FD scope | Register success 10→11; failed register paths retain 10; other warmed sweeps 11→11 |
| Existing admission verifier | 13 traces, 364 identity observations, three fence cases |
| Negative verifier checks | One positive and ten rejected tampered variants |

The matrix associates Saved, injected-write-failure, late-fence-drift,
sync-uncertainty and late-cancel observations for register, reattach, scan,
relocate and remove. Specials cover targeted repair, same-path NoChange/drift,
unrelated drift, store containment, saved-hint attachment, descriptor peak,
equal-key relocation and remove/rediscovery. The verifier checks typed outcomes,
revision/counter/generation rules, original guard sharing, memory at post-rename,
staged-result withholding, pending-context cleanup and explicit fresh recovery.
See [receipt semantics](../../../DURABLE-ROOT-LIFECYCLE-PLAN.md#14-bounded-mutation-receipts-and-verifier).

Scan allocation injection starts at store BeforeStaging, after discovery/capture;
it does not sweep standard-library traversal allocations. Other four sweeps
start at entry. Successful registration owns one additional guard descriptor;
failed allocations retain the observed rollback FD baseline.

## Redaction and limits

The local username is replaced with `maintainer` only in environment.log (three
replacements), linux_checkpoint_provider_tests.run.log (one), and
linux_checkpoint_evidence_tests.run.log (one). redaction.json records original
hashes, replacement counts and original/published manifest hashes. Numeric,
wire, identity, outcome and exit fields are unchanged. Original unredacted logs
are not published. All historical bundles remain unchanged.

Mutation receipts omit absolute paths/hints, pointers, epoch tickets and raw
hint-bearing checkpoint/artifact bytes. Detached checkpoint IDs are diagnostic
values, not live identity authority. The fixed 256-byte artifact observation
cap remains in force: 38 records explicitly report EFBIG and unavailable bytes.
Do not describe those as complete captures or independently verified artifact
content. Harness artifacts were removed; selected observations remain in logs.
Selected-call instrumentation is not a complete syscall trace. Injected faults
prove routing, not that the OS generates those errors. Replacement tests occur
before identity checks; they do not prove atomic pathname CAS or hostile-writer
isolation. Mount-namespace aliases remain Skipped, not Passed.

Scope is supplied process-interruption/transaction observations on WSL2 virtual
disk ext4 only. No bare-metal, Windows/NTFS, DrvFS, cross-OS, hostile-writer or
power-loss acceptance is granted. Explicit scoped mutation acceptance remains
Pending. Application/CLI wiring, NCN evidence and manual validation remain open;
Windows checkpoint acknowledgment stays BLOCKED. No storage release checkbox
is completed by this record.
