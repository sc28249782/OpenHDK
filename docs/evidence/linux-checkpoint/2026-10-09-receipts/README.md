# Maintainer pinned WSL2 ext4 receipt bundle

**Received:** 2026-10-09, Asia/Bangkok.
**Reported revision:** 9310ab3384c478d8e05bede3a2b20c3098dcf2ee (PR #62 merge).
**Acceptance:** Pending; see [the evidence record](../../../NATIVE-LINUX-CHECKPOINT-EVIDENCE.md).

Somchai Pongkasem supplied receipts.tar.gz after running the reviewed collector.
The 52 regular archive entries are stored here byte-for-byte; archive path
prefixes `./` were removed. No uploaded log was edited or backfilled. The raw
archive SHA-256 is 539e8d000c4c82126ddfe6acecf339fbc739faad783dfd0980da9e4c5603c28d.
The archive itself is not duplicated in Git. Original extracted bytes and the
submitted [SHA256SUMS](SHA256SUMS) are retained, including empty compile/run logs.
The manifest covers 51 logs, not this README or derived verification.json.

Verify submitted bytes from this directory:

```sh
sha256sum -c SHA256SUMS
```

| Evidence | Observation |
| --- | --- |
| [environment.log](environment.log) | Full revision, clean/diff labels, WSL2 kernel 6.18.40.1, GCC 13.3.0, /dev/sdd ext4 data=ordered, strict direct-g++ flags, SANITIZERS 0. |
| [receipts.log](receipts.log) | 24 compile and 24 run exits are 0; SUMMARY suites=24; OVERALL exit=0. |
| [source-after.log](source-after.log) | Same full revision; no status entries after it. Collector success checks clean source. |
| [linux_checkpoint_provider_tests.run.log](linux_checkpoint_provider_tests.run.log) | ext4-eligibility=1, test-filesystem-bypass=0; four interruption phases repeated twice. |
| [linux_checkpoint_evidence_tests.run.log](linux_checkpoint_evidence_tests.run.log) | filesystem-bypass=0; 42 traces, 720 native/injected records, 79 pre-cleanup artifact captures, 256 wire receipts, 8 child/cut receipts. |
| [verification.json](verification.json) | Derived editor checks with Python hashlib/struct, not an uploaded runtime receipt or acceptance decision. |

Independent checks matched all 256 full-wire hashes. Of these, 189 are complete
189-byte synthetic checkpoints with matching footer digests and independently
unpacked header/root/song fields; 67 are empty probe/partial-stage observations
and intentionally have decode errors. All 189 PROJECTION and 3 ACK receipts
agree with wire values. All 42 trace counts match, with overflow=0; 713 records
are real selected-call returns and 7 are injected EIO boundaries. Actual negative
recorded returns are renameat2 EEXIST (17). This does not claim all syscalls were
traced or that the OS generated an injected error.

All 79 ARTIFACT records capture their actual size without a capture error:
12 are 189-byte checkpoints and 67 are empty entries. Held descriptor identity,
links, capability and consumed state were recorded before cleanup. The receipt
bytes remain inspectable here; harness-owned filesystem artifacts were removed
at test end and must not be described as still present on the maintainer's disk.

All eight child receipts have receipt_complete=1, kill_rc=0, wait_complete=1 and
wait_status=9. Candidate/prior cuts preserve revision 9; post-rename cuts reopen
10 then 11, and acknowledged cuts reopen 12 then 13. Successful create/update
traces contain artifact/parent sync before rename and parent sync afterwards.
That sequence and process-interruption visibility do not demonstrate power-loss
survival or completion of every native evidence matrix row.

This is a maintainer-supplied WSL2 virtual ext4 disk run, direct g++ without
sanitizers or CTest. Hashes preserve supplied bytes, not independent source/run
authentication. No native Windows/NTFS, bare-metal Linux, cross-OS/DrvFS,
removable/network/cloud storage, or universal durability support is accepted.
The older [run bundle](../2026-10-09/README.md) remains unchanged.
