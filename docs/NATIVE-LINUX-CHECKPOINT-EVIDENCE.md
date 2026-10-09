# Linux checkpoint provider evidence

**Status:** Pinned WSL2 ext4 receipt run recorded; full-matrix review and acceptance Pending.
**Contracts:** [OHK-STORE-030](CATALOG-PERSISTENCE-CONTRACT.md) and
[OHK-NATIVE-030](NATIVE-CHECKPOINT-PROVIDER-PLAN.md).
**Scope:** Experimental LinuxCheckpointProvider; not released v0.2.0 support.

## Current regression observations

The development regression environment uses Linux kernel 6.18.44, GCC 13.3.0
and an overlay temporary filesystem. Production eligibility rejects that mount.
The test-only filesystem bypass runs real ownership, file I/O, rename and fsync
calls on overlay. This does not prove ext4 storage acceptance or power-loss
survival. There is no accessible native ext4 mount in this execution environment.

The suite exercises the coordinator/provider chain with deterministic faults,
allocation failure sweeps, seven-byte native I/O chunks with one injected EINTR
per configured read/write path, and no application allocation after successful rename.
Separate fresh-exec process interruption runs stop an owned child after candidate
verification, prior verification, rename before parent sync, and acknowledgment;
each cut point is repeated twice. The old primary remains at prepublication cuts;
new primary is visible at the later cuts. Reopen validates only primary and never
promotes abandoned stages. These observations are regression evidence only.

Injected errors prove routing, not that an OS generates those native failures.
The Windows synchronization gate remains open. No source asset, dependency,
CLI option, live catalog capture or durable-first mutation is introduced.

## Maintainer execution on 2026-10-09

Somchai Pongkasem supplied an environment log and test log after running the
provider on the WSL2 Linux filesystem. The original logs are retained without
editing in [the run bundle](evidence/linux-checkpoint/2026-10-09/README.md).
These are maintainer-supplied observations, not an execution by the documentation
editor. Uploaded files were named environment(1).log and tests.log; the stored
environment filename is environment.log and its bytes are unchanged.

| Field | Recorded observation |
| --- | --- |
| Provider/test revision | ea2a4148c43219e0b2e5d47bd5e46a80e12952c2, merge of PR #60 |
| OS/kernel | WSL2 Linux 6.18.40.1-microsoft-standard-WSL2, x86_64 |
| Compiler | Ubuntu GCC 13.3.0-6ubuntu2~24.04.1 |
| Mount | /dev/sdd, ext4, rw,relatime,discard,errors=remount-ro,data=ordered |
| Provider directory | Harness-owned directory under /home/somchaip/openhdk-evidence-20261009-135237 |
| Production eligibility | ext4-eligibility=1 |
| Filesystem test bypass | test-filesystem-bypass=0; fresh exec children inherit native mode |
| Build invocation supplied in chat | Direct g++, C++20, -Wall -Wextra -Werror -pedantic, -O1 -g -pthread; seam define on five designated targets |
| Sanitizers | Not enabled in this direct-build run |
| Regression log | All 23 distinct suite headers present, no failure diagnostic in submitted log |
| Exit-code receipts | Not emitted by that script; explicit compiler/test/overall receipts still required |

The source revision matches the requested merge commit. The environment log
contains no status entries following that revision; the requested git status
command therefore indicates a clean tree at environment capture if the supplied
script was used unchanged. The log does not include a separately labeled status
or diff-check receipt. Do not treat this as independent source authentication.

The first attempt used CMake/Ninja and stopped during configuration because
Ninja was unavailable. That attempt ran no tests. The submitted test log is from
the subsequent direct-compiler procedure, not CTest. Do not describe it as a
CTest result, sanitizer run, or compiler installation failure.

| Child interruption point | Repetitions | Reopened catalog revisions in log |
| --- | --- | --- |
| 0: CandidateVerified, before prior creation | 2 | 3, 3 |
| 1: PriorVerified, before publication | 2 | 3, 3 |
| 2: Rename returned success, before parent sync | 2 | 4, 5 |
| 3: Confirmed acknowledgment | 2 | 6, 7 |

These observations exercise real Linux rename/fsync operations on the recorded
WSL2 ext4 virtual disk. They do not establish bare-metal Linux behavior, native
Windows/NTFS behavior, cross-OS/DrvFS support or power-loss durability. The lease
suite's generic pending message does not negate the provider's explicit native
mode; its own test helper remains a separate ownership test boundary.

## Pinned receipt execution on 2026-10-09

The maintainer supplied [the receipt bundle](evidence/linux-checkpoint/2026-10-09-receipts/README.md)
after using the reviewed collector at PR #62 merge
`9310ab3384c478d8e05bede3a2b20c3098dcf2ee`. This is a separate run from the earlier
PR #60 evidence. Original archive entries and its 51-log manifest are preserved;
all hashes match. The received archive contains no binaries or source assets.

| Field | Recorded and checked observation |
| --- | --- |
| Source | Exact merge revision before/after; labeled clean=1, DIFF_CHECK exit=0; overall collector exit=0. |
| Environment | WSL2 Linux 6.18.40.1-microsoft-standard-WSL2, GCC 13.3.0; /dev/sdd ext4, rw,relatime,discard,errors=remount-ro,data=ordered. |
| Native mode | Provider ext4-eligibility=1 and test-filesystem-bypass=0; evidence target filesystem-bypass=0. |
| Build/test | Direct g++ strict C++20 flags; 24 compile exits=0, 24 run exits=0; SANITIZERS 0, not CTest. |
| Selected calls | 42 non-overflow traces; 713 actual returns and 7 injected EIO boundary records, checked separately. |
| Wire/token | All 256 full-wire SHA-256 values match; 189 complete checkpoints have valid footers and independently unpacked fields; 189 projections and 3 acknowledgments agree. |
| Artifacts | 79 captures before cleanup, complete and error-free: 12 checkpoints and 67 empty entries. Identity/capability/consumed state and captured bytes retained in log. |
| Sync | Successful create/update traces record file/parent sync before rename and parent sync after rename. |
| Process interruption | Four phases, two repetitions each, complete child packets and SIGKILL wait receipts; cuts preserve expected old/new revision. |

| Evidence cut | Before → reopened revision (two repeats) |
| --- | --- |
| CandidateVerified | 9 → 9; 9 → 9 |
| PriorVerified | 9 → 9; 9 → 9 |
| Rename before parent sync | 9 → 10; 10 → 11 |
| Confirmed acknowledgment | 11 → 12; 12 → 13 |

Injected write/read/sync/publication/reconcile failures remain injections, not
proof of real OS faults. Cleanup failure records Saved with a warning; the
create-collision receipt records actual EEXIST/NotCommitted. Empty probe wires
have expected decode errors; they are not accepted checkpoints. The filesystem
artifacts are gone after harness cleanup, but their captured bytes remain in the
log. The derived verification.json documents editor checks, not a runtime claim.

This closes the missing exit-code/selected-receipt observations for this run. It
does not itself close section 6 of the native plan. Some ownership/path,
allocation/short-I/O, external-replacement and invalid/missing-primary cases have
regression assertions and suite exit receipts rather than detailed per-case
native/token/artifact receipts. Review those distinctions before accepting the
full matrix. Explicit reviewer acceptance is still Pending. Windows synchronization,
durable-first wiring and broader release gates remain unchanged.

## Native ext4 execution

Select an existing writable local ext4 directory for harness-owned child test
directories. Do not select WSL cross-OS/DrvFS, overlay, tmpfs, network, removable
or cloud-synchronized storage. The target creates and removes only its own
mkdtemp directory after all child processes/owners exit.

Build the CMake linux-checkpoint-provider target, or compile directly with C++20,
strict warnings, pthread and OPENHDK_ENABLE_TEST_SEAMS=1. The define enables
fault/cut-point seams; the environment setting below disables filesystem bypass.

```sh
git rev-parse HEAD
git status --short
git diff --check
uname -a
g++ --version
findmnt -T /path/to/native-ext4 -o TARGET,SOURCE,FSTYPE,OPTIONS
OPENHDK_NATIVE_CHECKPOINT_DIR=/path/to/native-ext4 \
  /path/to/openhdk_linux_checkpoint_provider_tests
```

The environment setting is inherited by fresh exec children. Native mode MUST
fail if production ext4 eligibility fails; it does not skip or bypass that gate.
Keep the exact command, complete stdout/stderr, exit code, filesystem/mount data
and clean full commit SHA together. Run both Linux ownership/provider suites and
the full regression set. Repeat interruption cases and retain native error values,
primary sequence/digest/projection and artifact observations required by section 6
of the plan. The current suite prints cut point/repetition/revision; supplementary
native matrix observations and logs are still required before acceptance.

## Receipt collector (fresh run required)

The separate `linux-checkpoint-evidence` target adds bounded receipts; the older
regression target and submitted logs remain unchanged. On Linux, ordinary CI
uses the private filesystem bypass. With OPENHDK_NATIVE_CHECKPOINT_DIR set,
production eligibility must succeed, including in fresh exec children. Non-Linux
execution prints Skipped and performs no provider I/O.

After this test slice is reviewed, use its exact full commit and a clean checkout:

```sh
bash tests/run-linux-checkpoint-evidence.sh FULL_COMMIT_SHA /path/to/native-ext4
```

The parent must already exist, be outside the checkout and report ext4. The
collector requires g++, not Ninja, compiles all hardware-free suites with strict
warnings, and records individual compiler/test exit codes and the overall code.
Compile/run deadlines are 180/120 seconds with a five-second kill grace. Output
is a new `openhdk-receipts-*` directory containing environment.log, per-suite
compile/run logs, receipts.log, source-after.log and SHA256SUMS. Failure bundles
are retained too. Sanitizers are off by default; explicitly set
OPENHDK_EVIDENCE_SANITIZERS=1 to request ASan/UBSan and record that choice. A failed
collector invocation is not a successful native run.

Receipt meanings and bounds:

| Receipt | Observation |
| --- | --- |
| NATIVE | Selected openat/pread/pwrite/fsync/rename/unlink returns, errno only on failure, and an injected flag; injected provider EIO is not an OS return. |
| ARTIFACT | Held candidate/prior descriptor identity, capability, consumed state and up to 256 actual bytes, captured before cleanup closes/unlinks it. |
| WIRE / PROJECTION | Full tiny synthetic wire in hex, full-wire SHA-256 and decoded sequence/revision/high-water/counts/footer digest; malformed/empty probes report decode errors. |
| SAVE / ACK / OPEN / PUBLISH | Coordinator result, acknowledged token or explicit failure; numeric enum values refer to CatalogCheckpointStore.hpp. |
| CHILD / CUT_RESULT | Owned child PID, termination/wait receipt, and asserted old/new revision for four cuts repeated twice. |
| FILE / NAMESPACE | Actual primary and at most 64 stage entries inspected before fixture deletion, including absent/malformed entries; names do not authorize recovery. |
| EXIT / OVERALL | Actual compiler/test/collector exit codes; silence is not substituted for success. |

Each provider buffers at most 256 fixed records. Overflow or incomplete artifact
capture fails the evidence target; it does not silently truncate. Record insertion
performs no allocation or formatting. Test-only cleanup capture adds fstat/pread
observations; production builds contain neither receipt storage nor that extra
I/O. A separate allocation prohibition covers the instrumented update after
rename. The interruption hook transports a fixed packet to the parent through a
test pipe; formatting occurs outside publication. Parent receipt/wait deadlines
are finite and only the harness-owned PID is terminated.

This is selected-call instrumentation, not a complete syscall trace: lease walk,
all identity checks and every auxiliary syscall are not individually traced.
The collector covers successful create/update, selected injected save/reconcile
faults, actual EEXIST create rejection and process interruption. Remaining matrix
rows still require their own evidence; a complete log does not itself accept the
provider. Process termination does not simulate power loss. A fresh pinned ext4
run and explicit review remain Pending. The 2026-10-09 logs above are immutable
and must not be backfilled with observations from this target.

## Acceptance record (receipt run populated; gate open)

| Required evidence | State |
| --- | --- |
| Exact provider/test revision | Recorded at 9310ab3 with clean/diff labels, matching after-run revision and successful collector. |
| Native OS/kernel/compiler and local ext4 mount/options | Recorded for the WSL2 environment; no other environment claimed. |
| Regression execution | 24 explicit compiler and 24 test exits=0, overall=0; no sanitizers or CTest in this run. |
| Process-interruption observations | Four cuts × two repeats; child packets/waits, wire and expected revisions recorded. |
| Full fault/interruption matrix | Selected detailed cases recorded; per-row completeness and remaining evidence require review. |
| Native returns, sequence/digest/projection and artifact capture | Selected returns and bounded captures recorded and independently checked; not a complete syscall trace. |
| Capability probe and pre/post-publication sync | Selected successful/failed probe and create/update sync returns recorded. |
| Explicit reviewer acceptance for that revision/platform | Pending. |

The receipt collector and both submitted run bundles remain unmodified. Do not
backfill the earlier logs or substitute new-run observations into their record.
Any future instrumentation change requires another pinned run and review.

A passing hosted job or ext4 label alone does not close this record. No universal
power-loss claim is intended. Keep the storage roadmap checkbox open until the
accepted contracts' remaining platform, live wiring and validation gates close.
