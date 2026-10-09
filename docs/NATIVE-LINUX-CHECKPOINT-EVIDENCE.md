# Linux checkpoint provider evidence

**Status:** WSL2 ext4 provider run recorded; supplemental evidence and acceptance review Pending.
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

## Acceptance record (partially populated; gate open)

| Required evidence | State |
| --- | --- |
| Exact provider/test revision | Recorded in environment.log; labeled clean-tree/diff and exit-code receipts Pending |
| Native OS/kernel/compiler and local ext4 mount/options | Recorded for the WSL2 environment above; no other environment claimed |
| Regression execution | 23 suites listed, no reported diagnostics; explicit compile/run receipts Pending |
| Process-interruption observations | Four cut points, two repetitions each, catalog revisions recorded |
| Full fault/interruption matrix and logs | Partially exercised by existing suite; detailed per-row receipts Pending |
| Native returns, sequence/digest/projection and retained artifacts | Existing assertions cover selected behavior, but detailed evidence output Pending |
| Capability probe and pre/post-publication sync observations | Covered by provider calls/assertions; explicit native-call receipts Pending |
| Explicit reviewer acceptance for that revision/platform | Pending |

The current test harness removes its owned temporary directories at the end.
Do not claim that retained-artifact bytes can still be inspected from this run.
A follow-up test/evidence slice must emit bounded per-case receipts before
harness cleanup: operation, injected versus real native return/error, primary
sequence/digest/projection and candidate/prior identity/state. It must include
explicit compiler/test exit codes and cover the remaining section 6 matrix rows.
Changes to test instrumentation require a fresh pinned run and review. Do not
rewrite these submitted logs to fill missing observations or mark acceptance.

A passing hosted job or ext4 label alone does not close this record. No universal
power-loss claim is intended. Keep the storage roadmap checkbox open until the
accepted contracts' remaining platform, live wiring and validation gates close.
