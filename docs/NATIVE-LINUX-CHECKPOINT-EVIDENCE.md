# Linux checkpoint provider evidence

**Status:** Pending native ext4 run and acceptance review.
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

## Acceptance record (not populated)

| Required evidence | State |
| --- | --- |
| Exact clean provider/test revision | Pending native execution |
| Native OS/kernel/compiler and local ext4 mount/options | Pending |
| Full fault/interruption matrix and logs | Pending |
| Native returns, sequence/digest/projection and retained artifacts | Pending |
| Capability probe and pre/post-publication sync observations | Pending |
| Explicit reviewer acceptance for that revision/platform | Pending |

A passing hosted job or ext4 label alone does not close this record. No universal
power-loss claim is intended. Keep the storage roadmap checkbox open until the
accepted contracts' remaining platform, live wiring and validation gates close.
