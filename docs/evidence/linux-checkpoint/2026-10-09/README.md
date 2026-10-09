# Maintainer WSL2 ext4 run bundle

**Received:** 2026-10-09 (Asia/Bangkok).
**Revision reported:** ea2a4148c43219e0b2e5d47bd5e46a80e12952c2.
**Acceptance:** Pending; see [the evidence record](../../../NATIVE-LINUX-CHECKPOINT-EVIDENCE.md).

These files are the unedited logs supplied by Somchai Pongkasem. The uploaded
name environment(1).log is stored as environment.log without changing its bytes.
Hashes detect later changes to this copy; they do not authenticate a test run.

| File | SHA-256 |
| --- | --- |
| [environment.log](environment.log) | f3f6a7a2a2005d80ab24d5810f5d49c86c9812e7e9ad7952e69ef3b1b463a007 |
| [tests.log](tests.log) | c77b0f24d508571ea251f1eede8e4c6b468e9611943a1b47715b78a182e83afc |

The log lists 23 suites and eight child-interruption observations. It reports
production ext4 eligibility and filesystem bypass disabled for the provider.
There are no explicit compiler/test exit-code receipts, native syscall trace,
full token/projection or retained-artifact dump. Do not infer those from silence.
The run used direct g++, not CTest or sanitizers. This bundle records observations
for WSL2's /dev/sdd ext4 filesystem only. It does not approve Linux storage,
Windows/NTFS, cross-OS/DrvFS or power-loss guarantees.
