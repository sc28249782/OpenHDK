# Native checkpoint provider plan

**Plan ID:** OHK-NATIVE-030
**Status:** Accepted by review and merge of PR #58; Linux lease and experimental provider implemented; native acceptance and Windows synchronization pending.
**Target:** OHK-STORE-030 slice 4; not part of released v0.2.0.
**Source review date:** 2026-10-08.

This plan supplements the accepted [persistence contract](CATALOG-PERSISTENCE-CONTRACT.md).
[SPECIFICATION.md](SPECIFICATION.md) remains the authority for implemented behavior.
The codec, restore factory, detached coordinator, Linux ownership primitive and
experimental Linux provider are implemented. Native platform acceptance is pending. This document records API candidates,
required tests and unresolved decisions. It adds no dependency or storage support.

## 1. Scope and implementation order

Implement Linux/local ext4 and Windows/local NTFS providers separately. Each
provider must satisfy the existing `CheckpointStoreProvider` interface. Keep
provider code, tests and evidence outside the audio callback. The fake provider
in PR #57 proves coordinator decisions only.

The accepted implementation order is:

1. Specify finite path/handle, name-attempt and buffer bounds, then implement
   path/handle ownership and lease acquisition on Linux.
   Test contention and rejected paths without publishing a checkpoint.
2. Add Linux staging, publication, synchronization and reconciliation. Run the
   native ext4 fault and subprocess-interruption matrix before support claims.
3. Implement Windows handle/lease ownership. Resolve the publication and
   synchronization gates in section 5 before enabling acknowledged saves.
4. Add Windows publication tests and an NTFS evidence record. Review each
   failure classification against the exact APIs and Windows version.
5. Continue slice 5 separately: live capture, reciprocal store/song-root
   containment, same-library snapshot admission and durable-first mutations.

The ownership primitive implements the ownership portion of step 1.
A completed Linux provider does not approve Windows storage. A platform compile
or passing hosted CI test does not close a filesystem evidence gate.
The storage roadmap checkbox remains open. The development tree now has
23 core / 26 audio-enabled suites; the provider suite adds publication regression
tests. These do not close native ext4 acceptance.

## 2. Shared ownership and resource rules

The caller selects an existing local parent and one primary filename. Reject
aliases, symbolic links/reparse entries, nonregular targets and hard-linked
primary/lock/artifact files. Reject known unsupported storage types before creating files. Confirm required
API/filesystem capabilities before checkpoint staging; capability probes use only
exclusively owned test entries in a harness-selected directory.
Do not create parent directories, truncate a primary, import a legacy database,
or select a nearby backup. Native path encoding and component limits must be
validated before any write; no lossy conversion or silent truncation is allowed.

Retain the parent identity and native handles during the whole lease. Revalidate
the selected path against that identity before staging and publication. A held
handle protects identity lifetime; it does not make pathname lookups atomic.
Directory replacement returns SourceChanged. Mounted filesystem detection must
use the retained directory, not an unrelated drive or the current working directory.

The coordinator's atomic_flag prevents reentry in one store object. The native
provider must ALSO enforce exclusive ownership across provider/store instances
in the same process and across processes. A second acquire on the SAME provider
must return Busy; it must not reuse an existing lease for a second store.
Use a nonblocking process registry keyed by native directory identity plus the
primary-name equivalence rule, and a separate stable native lock. Account for
Windows case aliases when forming this key. Fail or return Busy rather than
spin. Release only resources owned by this lease after a failed acquisition.

Open/create the stable lock without truncation. Validate its regular type,
identity and link count before locking. Retain its handle until close. Never
unlink a runtime lock file or infer ownership from its filename. Recheck its
name-to-identity binding before publication. A process registry is not a
cross-process lock; the native lock is not snapshot lineage authentication.

Artifacts need lease-bound capabilities: a fixed internal slot, exclusive name,
file identity and handle state. At most one candidate and one prior copy belong
to a save. Use bounded exclusive-create attempts; a collision never permits
opening/truncating another file. Allocate names, conversion buffers and result
state before publication. Account for provider-retained payload within the same
operation allowance; do not duplicate full wire buffers without a ledger charge.

All post-publication methods must be noexcept and allocate no application
memory. Use prepared native paths, fixed diagnostics and preallocated verification
scratch. Partial writes and interrupted I/O need explicit handling. Preserve
native error values with the exact operation. Cancellation is admitted only
before publication, as already enforced by the coordinator.

Cleanup validates the owned capability and current identity. Consuming a
candidate through rename does not grant permission to delete the primary.
An identity mismatch preserves the entry and reports a cleanup warning. On
uncertainty, preserve candidate/prior artifacts wherever they remain. Do not
scan suffixes or clean another operation's files on open.

These rules coordinate participating writers in a trusted local directory.
Advisory locking and identity rechecks do not prevent all changes by a writer
that ignores the protocol. In particular, check-then-unlink or check-then-rename
is not a hostile-directory sandbox or an atomic compare-and-swap of a pathname.
Do not claim those guarantees in implementation comments or evidence reports.

## 3. Linux API candidates and publication sequence

The first Linux target is local ext4 with a recorded native test environment.
Other POSIX systems, network mounts, overlay/tmpfs, removable media and WSL
cross-OS/DrvFS storage are outside this acceptance target. Do not infer ext4
acceptance from a generic ext-family filesystem magic number alone.

| Provider step | Proposed API/use | Review or test obligation |
| --- | --- | --- |
| Parent traversal | Component-wise openat with O_DIRECTORY, O_NOFOLLOW and O_CLOEXEC | Reject symlinks in every component. A final O_NOFOLLOW alone does not check ancestors. Retain/recheck dev/inode identity. |
| File admission | openat without O_TRUNC; fstat regular type and st_nlink == 1 | Reject FIFO/device/directory/symlink/hard-link entries before reading or writing; avoid blocking on special files. |
| Stable lock | flock(LOCK_EX \| LOCK_NB) on a separately opened stable lock | Test same-process independent opens, same-provider reuse and child-process contention. Do not inherit the lease into an exec child. |
| Artifact creation | openat with O_CREAT \| O_EXCL \| O_NOFOLLOW \| O_CLOEXEC; private permissions | Bound naming attempts; retain ownership on partial creation errors. |
| Read/write | Bounded native read/write loops | Handle short reads/writes and EINTR. Detect growth beyond the requested bound before buffer expansion. |
| Artifact synchronization | fsync artifact and retained parent; coordinator then verifies by reopen | Recovery copies need synchronized directory entries before primary replacement. File sync alone is insufficient. |
| ExpectedAbsent create | renameat2 with RENAME_NOREPLACE in the retained parent | Never replace a target created after the final check. Unsupported syscall/flag/filesystem fails closed; no link/copy/delete fallback. |
| Existing primary update | renameat in the same retained parent | Recheck token and admitted target identity first; classify the exact return and retained state. No cross-volume path. |
| Publication synchronization | fsync retained parent after successful rename | A failure after rename is CommitUncertain, not rollback. |
| Reconciliation | Validate primary under lease, fsync that file and retained parent | Return error if synchronization is unconfirmed. Do not select a candidate or prior artifact. |

Linux documentation identifies flock locks with open file descriptions.
Independent opens in one process can conflict, while duplicated/inherited
descriptors share a lock [L2]. Tests must create truly independent contenders;
a fork child that inherits the owner's descriptor is not sufficient evidence.

The proposed create uses the no-replace flag rather than a check followed by
an overwriting rename. The flag depends on kernel/filesystem support [L3].
Probe support using harness-owned test entries before admitting checkpoint writes.
Do not discover lack of support by overwriting or deleting the selected primary.

The proposed artifact synchronization includes parent-directory fsync [L1].
This records the candidate/prior names before replacing the primary. A successful
rename and a successful directory sync are separate observations. Reconciliation
synchronizes only the validated primary and required namespace state; it does
not prove that a previous unacknowledged operation survived a past power loss.

The Linux implementation review must map every publication return. A successful
rename followed by a failed parent sync is Uncertain. A proved no-replace
collision leaves the competing target untouched and returns a definite error.
Unknown I/O failures are Uncertain unless the supported local API and native
observations prove this operation made no namespace change. Do not apply local
ext4 reasoning to remote mounts. Prepare any verification buffers before the
publication call; there is no allocating diagnostic reread afterward.

## 4. Windows API candidates and failure classification

The first Windows target is a native process on a recorded local NTFS volume.
The Linux spelling /mnt/e is not evidence of native Windows behavior. WSL
cross-OS storage, UNC/network shares and cloud-sync directories remain excluded.
An extended local path is not automatically UNC; validate its actual namespace.
Reject alternate data stream names and ambiguous device/path aliases before I/O.

| Provider step | Candidate API/use | Review or test obligation |
| --- | --- | --- |
| Parent admission | CreateFileW with FILE_FLAG_BACKUP_SEMANTICS and FILE_FLAG_OPEN_REPARSE_POINT | Inspect every ancestor, reject reparse entries, retain identities and sharing that prevents directory replacement. Final-only flags do not establish an ancestor policy. |
| Identity/type | GetFileInformationByHandle / GetFileInformationByHandleEx | Check volume/file identity, attributes, regular-file status and link count. Test short-name and case aliases. |
| Stable lock | CreateFileW without truncation; LockFileEx with exclusive and fail-immediately flags | Lock a fixed range, retain the handle, and test same-process and subprocess contention. No asynchronous waiting fallback. |
| Artifact creation | CreateFileW with CREATE_NEW and explicit sharing/access | Bound collisions; never CREATE_ALWAYS on primary/lock/prior names. Resolve sharing against later publication before choosing flags. |
| Staging | Bounded ReadFile/WriteFile; FlushFileBuffers; close/reopen and verify identity/bytes | Partial I/O, disk-full, access/share failures, artifact namespace synchronization and ledger accounting need tests. |
| ExpectedAbsent create | Candidate: MoveFileExW without REPLACE_EXISTING or COPY_ALLOWED | Same-volume only. The API and write-through flag do not by themselves close the namespace synchronization gate. |
| Existing primary update | Candidate: ReplaceFileW with flags 0 and no automatic backup selection | Keep the independently staged prior copy. Review metadata/ACL behavior and handle sharing. Never ignore ACL/merge errors. |
| Post-publication/reopen sync | Unresolved sequence; section 5 | Do not return Saved merely because replacement or file flush succeeded. |

ReplaceFileW documents REPLACEFILE_WRITE_THROUGH as unsupported. Its failure
codes 1176 and 1177 can leave the primary absent or under another name; code
1175 retains the original names [W1]. These are inputs to classification, not
permission to reconstruct a primary from a backup. The default proposed rule
for a failed replacement call is Uncertain. A narrower NotCommitted result
requires a reviewed proof for that return code and the tested preconditions.

Failures proven before the publication API is invoked are NotCommitted.
After invocation, an unclassified result is Uncertain. Successful publication
still requires the provider's reviewed synchronization. A subsequent flush,
identity check or verification failure is Uncertain. Keep diagnostics and
artifacts; do not retry replacement or switch to another publication API.

MoveFileExW provides a write-through option. Its documentation describes flush
behavior for copy/delete moves [W2]. This plan forbids that fallback and does
not infer a directory synchronization guarantee for same-volume rename.
API candidates remain subject to the Windows gate below.

## 5. Windows synchronization gate (open)

The retrieved FlushFileBuffers documentation describes flushing a writable file
handle. It also describes privileged volume flushing [W3]. These facts do not
establish an unprivileged NTFS equivalent of the Linux parent-directory fsync
sequence. This is an inference limit, not a claim that Windows cannot provide
a correct protocol.

Before enabling acknowledged Windows saves, review and record:

1. The exact create/update APIs, flags and handle-sharing arrangement.
2. How synchronized candidate/prior names remain usable for explicit recovery.
3. What confirms publication synchronization and what confirms it on reopen.
4. Which documented guarantees support that sequence and which conclusions
   depend only on the tested OS/filesystem behavior.
5. How every partial replacement, sharing, ACL and flush failure maps to
   NotCommitted or Uncertain without allocating after publication.

Reopening a visible primary and flushing its content alone is not an accepted
answer to the namespace question. A privileged volume flush, successful sample
run or WRITE_THROUGH flag must not silently become a new requirement or proof.
If the reviewed API sequence cannot satisfy OHK-STORE-030, keep the Windows
provider unsupported for acknowledged saves and propose a contract change.
Do not weaken syncPublication/reconcilePublication to return unconditional success.

Linux implementation may proceed after this plan is accepted. Windows ownership
and negative-path tests may proceed separately; successful native Windows save
support remains blocked until this gate and its tests close. No fallback to an
unreviewed filesystem/provider is permitted.

## 6. Native fault and interruption evidence matrix

Run deterministic injected API failures and real subprocess interruption as
separate tests. Injection proves error routing; it does not prove that the OS
produces that failure or that a controller survives power loss.

| Case or cut point | Required observation |
| --- | --- |
| Same provider, second store; independent provider same process; independent process | Busy without waiting, no shared lease, no release of the first owner's lock. |
| Symlink/reparse ancestor or final entry; hard link; special file; directory replacement | Rejection before modifying admitted files. Owned handles close; unrelated files remain. |
| Create collision; allocation/partial write/artifact flush/reopen failure | No save acknowledgment; old primary byte-identical; cleanup restricted to owned stages. |
| Candidate sync complete, before prior creation; prior copy complete, before publish | On child termination, primary is absent (create) or old valid bytes (update); reopen ignores artifacts. |
| Primary externally replaced after recheck; ExpectedAbsent race | No promise of hostile-writer isolation. Test no-replace create and documented update trust limits; classify detected mismatch. |
| Immediately after namespace publication, before required sync | No acknowledged success from terminated child. Reopen yields valid old/new primary or explicit missing/corruption/sync error; never promotes artifacts. |
| Publication API reports partial or unclassified failure | CommitUncertain, no token; available artifacts retained; save refused until explicit validated reopen. |
| Required post-publication sync fails | Same uncertainty rules even when the new primary is visible. |
| Immediately after confirmed acknowledgment | Reopen validates the acknowledged sequence/digest/projection in the tested process-interruption environment. This is not a power-loss experiment. |
| Cleanup fails after acknowledgment | Saved plus cleanup warning; primary stays valid; later open ignores abandoned entries. |
| Process dies while holding lock | Independent contender can acquire after OS release; no stale-lock deletion. Use a bounded wait in the test harness only. |
| Invalid/new-schema primary; missing primary after uncertainty | Structured error or NotFound; no automatic repair, recreation or artifact selection. |

Use synthetic codec fixtures only. Retain parent/child phase markers and native
return codes. Block a child at a documented test seam, then terminate it from
the harness. Use finite timeouts; never kill an unrelated process. A test seam
must not introduce callback work or ship in a production target.

Test directories belong to the harness. Inspect preserved files before cleanup.
The harness may remove its own whole temporary directory after all owners exit;
the runtime provider must not use that privilege to remove a live lock or
unowned artifact. Repeat cut-point tests and record repetition count and seeds.
A missing required environment is Pending/Skipped evidence, not Passed.

## 7. Evidence record and release gate

Each platform needs a separate record tied to an exact provider/test commit.
Populate a record only from an executed native provider test revision. The
current ownership tests do not populate a publication or durability record.

| Record field | Required content |
| --- | --- |
| Revision | Full commit SHA, clean tree, provider/test target names and build configuration. |
| Environment | Native OS/kernel/build, compiler, local filesystem, mount/volume and relevant caching/options. Record how filesystem identity was established. |
| API sequence | Flags, access/share rights, lock range, synchronization calls, capability identities and classification table. |
| Executed cases | Matrix row, fault/cut point, repetitions/seed, native code, observed primary/sequence/digest and artifact state. |
| Interruption method | Child PID owned by harness, synchronization seam and bounded termination method; distinguish injected errors from real interruption. |
| Results and limits | Pass/fail/skip with logs; no universal crash or power-loss claim. List unresolved namespace/metadata behavior. |
| Acceptance | Reviewer decision for this exact provider/platform. A filesystem label or CI job name is insufficient. |

A hosted runner may supply native evidence only if the record verifies its
actual filesystem/environment and runs the required native cases. Existing
Linux core and Windows bootstrap jobs continue to guard regressions; their
current tests do not close either provider gate. The ownership suite passes native Linux lock/path tests using a test-only
filesystem bypass when needed; ext4 publication acceptance remains pending. Windows listening/lyric validation, NCN evidence, application
orchestration and source-release gates remain separate.

## 8. Primary API references

The following documentation was retrieved on 2026-10-08. Links explain proposed
API choices; no implementation was copied. A checked API page is not a tested
provider. This plan adds no third-party library.

- [L1: Linux fsync](https://man7.org/linux/man-pages/man2/fsync.2.html).
- [L2: Linux flock](https://man7.org/linux/man-pages/man2/flock.2.html).
- [L3: Linux rename/renameat/renameat2](https://man7.org/linux/man-pages/man2/renameat2.2.html).
- [L4: Linux open/openat and O_NOFOLLOW](https://man7.org/linux/man-pages/man2/open.2.html).
- [W1: Microsoft ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).
- [W2: Microsoft MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw).
- [W3: Microsoft FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers).
- [W4: Microsoft CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).
- [W5: Microsoft LockFileEx](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex).
- [W6: Microsoft GetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle).
- [W7: Microsoft GetFileInformationByHandleEx](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex).

## 9. Current Linux ownership implementation boundary

`library/LinuxCheckpointLease.hpp` implements a noncopyable control-path lease,
not a CheckpointStoreProvider. It has no checkpoint reader, artifact writer,
publication, synchronization, recovery or live-capture interface. Windows and
other non-Linux builds reject valid requests with UnsupportedStorage, and
invalid configuration is rejected without filesystem access.

The default bounds are 4096 UTF-8 parent bytes, 32 parent components and 128
UTF-8 primary-name bytes. Positive smaller bounds are allowed; increases require
review. Inputs are absolute, normalized component paths with no dot/dot-dot,
empty components, control bytes, backslashes or colon aliases. A primary is one
component; the .ohk-lock suffix is reserved. The process registry admits at most
64 simultaneous leases. Its key is directory dev/inode plus exact Linux primary
bytes; no Unicode normalization or ASCII case folding changes Linux identity.
The lease retains two native descriptors and uses at most four concurrently
during traversal/checks. Retained path/name/lock-name bytes and both owned key-name copies are exposed
by ownedBytes for later combined
provider accounting. Logical sizes are not a process-memory ceiling.

Component-wise no-follow directory opens retain the final parent. Existing
primary admission checks regular type, single link and identity across the
metadata/open observations. Special-file opens use nonblocking flags. These are
identity/type observations, not an atomic pathname sandbox or content revision.
The stable lock uses exclusive creation or no-truncate existing open, regular
single-link checks before and after existing-file open, native flock and name/identity recheck. release/destruction
close only owned descriptors and release the process reservation; they never
unlink the lock. Same-object reacquire and competing owners return Busy.

Production acquire obtains STATX_MNT_ID from the retained directory and checks
that exact ID's filesystem label in /proc/self/mountinfo is ext4. Missing statx
support/returned mask, unavailable or malformed mount data, oversized input or a
non-ext4 label fails closed before lock creation. The parser bounds a line to
8191 bytes and its consumed text to 1 MiB. This is eligibility for a lock primitive, not
native provider or durability acceptance; it does not close the Windows gate.
Sources: [Linux statx](https://man7.org/linux/man-pages/man2/statx.2.html) and
[proc mountinfo](https://man7.org/linux/man-pages/man5/proc_pid_mountinfo.5.html),
retrieved on 2026-10-08.

The test target alone enables OPENHDK_ENABLE_TEST_SEAMS. Its private-access
helper bypasses only filesystem eligibility so real Linux ownership tests can
run on overlay/tmpfs in CI. It does not replace openat/flock/type/identity checks.
The current local test filesystem is overlay; production rejects it. Tests use
exec children with fresh process registries and CLOEXEC lease descriptors, so
cross-process contention does not depend on the parent's copied registry or
inherited flock descriptor. A bounded handshake/termination test verifies OS
lock release after child exit. Harness cleanup occurs after owners are released.

The suite covers aliases/special files, preserved primary/lock bytes, contention,
directory/lock replacement, bounds, capacity and reservation cleanup. Native
ext4 publication and interruption evidence remain Pending, not Passed. The experimental provider now implements the Linux staging/publication sequence;
[separate native evidence](NATIVE-LINUX-CHECKPOINT-EVIDENCE.md) remains pending.

## 10. Experimental Linux provider and regression evidence

`library/LinuxCheckpointProvider.hpp` now implements the provider interface using
the existing lease. The persistence contract records its finite artifact/I/O
bounds, combined retention ledger and failure classification. No Windows
publication sequence, live capture adapter or application wiring is added.

The provider regression target enables test seams only in that target. Filesystem
bypass permits syscall tests on overlay; it does not replace openat, flock,
renameat2/renameat or fsync. Separate faults simulate staging, unknown publication,
post-publication sync, cleanup and reconciliation errors. Short-I/O and injected
EINTR exercise native read/write loops. Application allocation is prohibited
from successful rename through acknowledgment in a deterministic test.

Fresh exec children are stopped after CandidateVerified, PriorVerified, rename
before parent sync, and confirmed acknowledgment. Each point runs twice with
finite five-second harness waits; only the owned child PID is terminated. Old/new
primary revision observations are printed. These are process-interruption tests,
not power-loss tests. Native ext4 acceptance is still Pending. The separate
evidence document specifies how to run without the filesystem bypass and how
to bind logs to an exact commit/environment for review.
