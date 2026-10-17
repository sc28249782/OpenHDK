# Library management and preparation CLI evidence

Status: explicit scoped management/preparation CLI acceptance recorded on
2026-10-17 for tested revision `80b673ee` on the WSL2 ext4 environment below.
The acceptance does not extend to playback or other runtime revisions.

This procedure implements [OHK-CLI-030 step 4](LIBRARY-CLI-ORCHESTRATION-PLAN.md#8-implementation-order-and-review-gates).
It uses the production `OpenHDKLibrary` executable. The executable is compiled
without test seams and without audio dependencies. The evidence harness does not
install a provider callback or change application, service or publication code.

## Collection

Use a clean detached checkout of the full 40-character commit to be tested.
Use an existing ext4 parent outside that checkout. The parent must allow harness
subdirectories and production native admission. Do not use DrvFS or overlay.
From that checkout:

```bash
bash tests/run-linux-checkpoint-evidence.sh FULL_40_CHARACTER_SHA EXISTING_EXT4_PARENT
```

The collector checks HEAD, clean tree, whitespace and findmnt before collection.
It builds and runs all 31 hardware-free C++ suites, then compiles the production
management executable. It records separate compile, subprocess-matrix, verifier,
negative-verifier and overall exits. CTest counts remain 32 core / 35 audio-enabled;
the additional Python checks are collector/CI checks, not new CTest targets.
Optional `OPENHDK_EVIDENCE_SANITIZERS=1` enables the existing sanitizer build mode;
the default is 0. Report the actual setting, not an inferred sanitizer result.

Each compile has a 180-second timeout; each run/verifier has 120 seconds with a
five-second kill grace. The CLI harness bounds each reply/child exit to 15 seconds,
reads at most a 64 KiB JSON line and 101 rows per batch, and checks checkpoint and
executable sizes before reading their bounded bytes. Its verifier accepts at most
2 MiB / 128 receipt rows. The selected fixture has one 31-byte synthetic KAR file.
These are harness bounds, not a total process-memory or filesystem-atomicity claim.

The output directory includes:

- `OpenHDKLibrary.compile.log`: production compilation diagnostics.
- `library-cli-native.run.log`: JSON Lines case/checkpoint/output/exit observations.
- `library-cli-verification.log`: bounded consistency result.
- `library-cli-verifier-tests.log`: one positive and 15 rejected tampered variants.
- `library-cli-tools-tests.log`: eight offline transport/reader/verifier checks.
- Existing suite logs, environment/source receipts and a `SHA256SUMS` manifest.

The harness creates only its own temporary store/song directories under the
selected parent. After successful observations and child exits it removes those
fixture directories. Checkpoint bytes are read before cleanup; they are not kept
in receipts. On failure it stops only its owned children and retains its fixture
directory for local investigation. The collector retains its bundle on failure.
Do not interpret a partial bundle or a Skipped/ineligible run as Passed.

## Selected 37-case matrix

| Group | Cases / observations |
| --- | --- |
| Baseline | Create Saved; a second Create rejects without replacing the existing primary; reopen reports Opened |
| Registration and scan | Register, complete scan, bounded song listing and preparation; root/song high-water advances |
| Overrides | Exact Thai title/artist, NoChange without publication, prepared user display metadata |
| Logical lifecycle | Invalid selector rejects; equal-key relocation invalidates; preparation NotReady; rescan; removal; rediscovery uses SongId 2 and drops old overrides |
| Restored owner | Root is unattached; preparation InvalidRoot; explicit saved-hint attachment advances generation 1 to 2; same-path reattachment is NoChange; preparation stays NotReady until scan |
| Ownership | A separate fresh executable contends while the current session holds its lease and exits Busy (75) |
| Authority drift | Harness renames the active root; override fails before publication, primary stays unchanged, session exits RecoveryRequired (76) |
| Explicit recovery | A fresh session restores unattached state; changed-path attachment advances generation to 3; scan restores preparation admission |
| Receipt loss | Close the output reader after prior replies, send exactly one override, observe exit 74; independent primary observation advances once; reopen/list finds the committed title/artist, attachment advances generation to 4, and prepared metadata agrees |

Six owned process exits are recorded: initial ordinary rejection (1), rejected
Create (1), Busy contender (75), drifted session (76), lost-output session (74),
and final reopened session (0). No automatic mutation retry is sent. Output-loss
classification is inferred from the observed exit plus committed checkpoint and
reopened display, not an acknowledgment that was delivered to the lost reader.
These observations do not provide exactly-once result delivery.

Every case includes before/after primary size, full SHA-256, footer digest,
sequence, catalog revision, high-water counters and row counts. The harness checks
header/schema/payload length and SHA-256 footer independently with Python
struct/hashlib. It does not decode the complete primary payload independently.
The verifier requires checkpoint continuity, Saved +1/+1 transitions, byte/hash
retention for queries/rejections/NoChange, exact replies, metadata and attachment
generations. Scan has a serialized revision change even if song identity stays.

CLI JSON output is retained as bounded response objects, with relative locators
and synthetic metadata. Receipts contain no command text, absolute root/hint paths,
raw checkpoint wire, pointer, epoch or store ticket. Executable and fixture hashes
are observations; they do not authenticate compilation from a claimed source.
The environment/compile logs can still contain local account/path information.
Before publication, redact the requested account name in those logs, preserve
original/published hashes and replacement counts, and regenerate the published
manifest. Do not backfill observations into old bundles or change numeric receipts.

## Recorded private WSL2 ext4 run — 2026-10-16

A maintainer-supplied run tested exact revision
`80b673ee5e96a423cbd5974f3b57c34a6fbf78ce` on WSL2 `/dev/sdd`
ext4 (`data=ordered`). The checkout was pinned and clean, the native
eligibility bypass was off, and the sanitizer setting was 0. All 31 selected
hardware-free suite compiles and runs, the production CLI compile and run,
the receipt verifier, the supplied-log negative verifier and the offline
tools exited zero. The private receipt records 37 selected cases, six owned
child exits (1, 1, 75, 76, 74, 0), one positive plus 15 rejected tampered
logs, and eight offline tool checks. The original private manifest verified
before and after redaction.

A separate derived copy masks account, host, local-path and media-related
values. It omits the raw CLI run log and compiled binaries. Its regenerated
manifest checks the derived files, and its privacy scan found no specified
sensitive tokens. The derived summary agrees with the original on case order
and child exits, but the stock receipt verifier cannot recheck the modified
copy. The original log and successful verifier results remain private. This
record is a report of supplied execution, not independent authentication of
source, build and run. No media hash, corpus or private path is published here.

When this run was first recorded, scoped native acceptance was **Pending**.
The explicit decision below is now authoritative for this tested scope.
The output-loss classification is inferred from exit and subsequent state;
there is no exactly-once delivery or power-loss claim. Prior provider,
factory/fence and mutation acceptances are unchanged. The Windows checkpoint
acknowledgment remains BLOCKED; playback, NCN and manual validation remain
separate open gates.

## Verification and limits

```bash
python3 tests/verify-library-cli-receipts.py BUNDLE/library-cli-native.run.log
python3 tests/check-library-cli-verifier.py BUNDLE/library-cli-native.run.log
python3 tests/library_cli_evidence_tools_tests.py
```

The last command is offline: independent checkpoint-header fixtures, transport
batch/line/EOF bounds, ineligible-parent rejection and malformed verifier input.
Linux CI runs it plus shell syntax checks. It does not execute the native matrix.
The supplied-log negative checks cover missing/duplicate cases, query writes,
rollback after receipt loss, repeated mutation, wrong exit, automatic attachment,
generation reuse, metadata loss, content/storage confusion, private fields,
false acceptance, bypass, duplicate JSON keys and oversized logs.

Local full-matrix development checks through private seams on overlay are
regression coverage only. The production harness has no bypass switch and refuses
an ineligible parent. A filesystem label, successful verifier or hosted CI result
alone does not close native acceptance. Fresh pinned maintainer execution, bundle
review and an explicit revision/environment/limits-bound decision are required
for each proposed acceptance. The decision below applies only to its recorded run.

This matrix selects process/CLI/checkpoint observations, not every syscall or
flush. No injected sync uncertainty, power-loss testing, hostile-writer isolation,
atomic pathname CAS, mount-namespace alias acceptance, independent source/run
provenance, bare-metal, NTFS or cross-OS support is claimed. Earlier provider,
factory/fence and mutation evidence remains separate and unchanged at `6a6337d`,
`4f69246` and `54f404a`. Windows checkpoint acknowledgment remains BLOCKED.
Playback orchestration, NCN evidence, manual validation and release checkboxes
remain open. CLI success on a synthetic KAR file is not NCN evidence.

## Explicit scoped CLI acceptance — 2026-10-17

After review of the private evidence and its redacted summary, the maintainer
explicitly accepted OHK-CLI-030 step 4 only for tested revision
`80b673ee5e96a423cbd5974f3b57c34a6fbf78ce`, from a clean pinned checkout on
WSL2 `/dev/sdd` ext4 `data=ordered`, with sanitizer setting 0 and filesystem
eligibility bypass off. Acceptance covers the selected 37-case production
management/preparation CLI matrix, six owned child exits and the recorded
checkpoint continuity. This closes step 4 within that scope and permits the
separate playback-controller work in step 5.

This decision relies on the reviewed private run. The redacted derived copy
cannot be rerun through the stock receipt verifier. A single run and its
self-consistent verifier do not provide independent source-to-binary/run
authentication. Output-loss classification remains an inference; sanitizer
coverage, power-loss behavior and exactly-once delivery are not established.
No atomic pathname CAS, hostile-writer isolation, namespace-alias, bare-metal,
NTFS, DrvFS or cross-OS acceptance follows from this decision.

Documentation commits do not relabel the tested revision. Earlier Pending
statements retain their historical meaning. Prior provider, factory/fence and
mutation acceptances and bundles remain unchanged. Playback/controller runtime,
NCN evidence, manual validation and release checkboxes remain open; Windows
checkpoint acknowledgment remains BLOCKED. No private bundle or media hash is
published by this decision.
