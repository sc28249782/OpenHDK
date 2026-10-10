# Native durable service binding and admission plan

**Plan:** OHK-BIND-030
**Status:** Accepted; binding/ancestry and experimental factory/fence implemented;
scoped WSL2 ext4 factory/fence acceptance recorded for tested revision `4f69246`
**Scope:** Linux binding capability and store/root admission for
[OHK-DURABLE-030](DURABLE-LIBRARY-SERVICE-CONTRACT.md), slice 2.

## 1. Current boundary and proposed change

PR #70 implements service baseline admission and durable user overrides with a
test-only provider category. Experimental Linux Create/Open factories now use private binding admission.
The Linux provider retains a directory and lock lease. A private binding
primitive now retains that lease epoch; no public provider interface exports it.

The accepted plan specifies that capability, a bounded containment check, and
the evidence needed before native factory acceptance. The first implementation
adds private binding/ancestry primitives and an experimental Linux factory.
It adds no dependency or released storage support.
The existing detached checkpoint provider and capture APIs retain their scope.

The Linux acceptance remains tied to provider/test revision
`6a6337df03fb69326cc5096eb2ef34b6e32bac11` on the recorded WSL2 `/dev/sdd` ext4
configuration. That decision does not accept this new integration. Windows
acknowledged saves remain BLOCKED by the unresolved NTFS synchronization gate.

## 2. Owning capability and lease epoch

Propose an immutable `NativeStoreBinding` with private construction and export.
Only the native service factory can obtain it from its owned Linux provider
while that provider holds an acquired lease. A caller cannot construct it from a
path, numeric identity, file descriptor, checkpoint projection or token.
There is no serialization or public writable handle accessor.

The capability retains:

- One close-on-exec duplicate of the provider's retained parent directory handle.
- The validated parent mapping and exact primary name.
- The directory device/inode and observed mount ID.
- An owning, private epoch token for this provider's current lease acquisition.

The duplicate pins the directory lifetime. It grants neither write authority nor
ownership of the writer lock. Its destruction closes only its own descriptor;
it never unlinks a lock, staging entry or primary.

Allocate the epoch/capability lazily when the factory first requests a binding.
Cache at most one immutable capability per acquisition; copies share its handle
and strings. A failed export publishes no capability and closes temporary handles.
Releasing or reacquiring the provider invalidates the epoch. A retained capability
may outlive the provider as descriptive data, but cannot authorize admission.
A later acquisition of the same path and native identity gets a different token.
Held tokens prevent accidental pointer-address reuse from becoming authority.

Before use, require the same owned provider, its current epoch, a live lease,
lease name-to-identity checks, and the capability handle identity. Reject foreign,
released and stale bindings even when their descriptive fields are equal.
Mount IDs are observations under retained handles, not persistent identifiers.
Check the statx result mask; absence of a requested mount ID is unsupported.

## 3. Narrow Linux containment algorithm

The first native service factory supports active roots on the same observed mount
as the store parent. Different or unavailable mount IDs fail closed with
UnsupportedStorage. This is the first primitive admission restriction; it does
not lower checkpoint schema limits or change detached discovery behavior.
It deliberately leaves cross-mount and bind-mount admission for later review.

Perform the following on the serialized control path:

1. Validate configured admission limits before opening root paths. Acquire the
   binding from the provider's retained lease, not from constructor path text.
2. Enumerate active root mappings from the transferred owner. Unattached restored
   roots and saved hints establish no active boundary and trigger no hint lookup.
3. Walk each active root path component with no-follow directory opens. Retain
   its handle through admission and final verification. Require the recorded
   mapping, directory identity and mount ID to remain consistent.
4. Reject lexical component containment of the store by a root. Do not use a
   string-prefix comparison: `/songs` and `/songs2` are different components.
5. Walk parent handles from the retained store directory with bounded `openat`
   directory opens. Compare each retained ancestor's device/inode with every
   retained active root. Equality means the store is inside or equal to that root
   and rejects admission. A store parent above a song root is permitted.
6. Stop at an observed mount boundary or self-parent root. Reject a walk that
   cannot reach a valid termination within the configured bound. Recheck the
   provider epoch, directory mapping and root mappings before permitting the
   initial checkpoint publication.

A root that disappears, becomes a symlink, or is replaced during this operation
is a failure, not evidence that the store is outside it. Retained root handles
prevent directory identity recycling during checks. Handle ancestry supplements
canonical mappings; canonical strings alone do not prove absence of aliases.

This algorithm does not claim atomic pathname compare-and-swap, hostile-writer
isolation, or stability during arbitrary mount-namespace changes. Different
mount IDs reject alias cases rather than silently treating them as safe.

## 4. Bounds and one operation ledger

| Admission resource | Default and maximum | Rule |
|---|---|---|
| Active roots | 32 | Unattached roots do not count; no partial admission. |
| Root path | 4096 UTF-8 bytes | Preserve mapping; no truncation. |
| Root path components | 32 | No-follow walk, not recursive enumeration. |
| Store ancestor parent steps | 32 | Self-parent or mount-boundary termination required. |
| Cached binding | 1 per acquired lease | Shared copies retain one duplicate handle. |
| Native descriptors during admission | 45 | Conservative aggregate cap, including provider and transient walks. |
| Logical staged payload | Existing service allowance, at most 64 MiB | No second allowance. |

Limits may be reduced for boundary tests. Increasing a maximum requires reviewed
plan/contract change. The active-root limit is stricter than the detached codec's
root-count limit. Restoring more roots is permitted while unattached; activating
more than the service admission cap is not permitted through the first factory.

Budget binding strings, retained root mapping copies and any simultaneous
conversion/validation copies before allocation. Count shared capability storage
once per object identity. Reuse already charged owner/provider strings when the
implementation retains a view with an owning lifetime. Descriptor counts are a
separate resource bound, not a claim about total process memory or open files.
Preflight the conservative cap before duplicating or opening handles.

## 5. Factory and save ordering

Propose private native factory plumbing for explicit Create/Open modes. Its
production entry point owns the concrete Linux provider and SongDiscovery.
It does not accept arbitrary provider subclasses, bindings, projections,
expectations or root mapping assertions from the caller. The existing fake-provider
category remains test-only and cannot authorize native admission.

Opening the store first may create the stable lock or capability-probe artifacts
under the existing provider protocol. These effects precede containment checks.
Admission failure must leave primary bytes unchanged, clean only owned probe
artifacts, and never unlink the stable lock. Do not claim zero filesystem effects
before admission or overwrite an existing primary to test a capability.

Create performs containment before saving its initial baseline. Open validates
and reconciles storage, restores a fresh unattached owner, and checks baseline
equality through the existing capture comparison. A saved hint is not attachment.
Return no writable service after failed binding or containment admission.

Retain the binding and active root guards for the service lifetime. Before each
save, revalidate their lease epoch and name-to-identity mappings. Add a private,
bounded admission fence at the coordinator's final prepublication check so a
late detected replacement prevents publication. This is control-path validation,
not a caller callback or a reentrant service operation. It may fail before native
publication; it must not add fallible work between confirmed save and the
service's noexcept memory swap. Specify the exact fence ownership/interface in
the implementation review and test it with the fake provider and native provider.

Detected external mapping drift moves the service to RecoveryRequired while
retaining its prior publication. A confirmed save still uses PR #70's acknowledgment
checks and non-throwing memory publication. An uncertain native outcome retains
memory and follows explicit recovery; no automatic adoption or retry is added.

Future durable root registration and reattachment must reuse reciprocal admission
before staging a root around the store. Staged guards and mappings must publish
with that root transaction. Those mutations remain unavailable in this slice.

## 6. Proposed errors and lifetime rules

Binding/admission results contain no partial capability or writable service.
Propose fixed structured operation tags Export, Admit and Recheck, with error
codes InvalidConfiguration, Unbound, ForeignBinding, StaleBinding, RootOverlap,
SourceChanged, UnsupportedStorage, LimitExceeded and StorageFailure.
Preserve a nested provider error and optional RootId when applicable. Reporting
must not allocate pathname copies on failure. Final names and enum integration
require code review; this plan does not reserve public numeric values.

Allocation failure closes only owned temporary descriptors. Close invalidates
binding authority before releasing the lease. Retained snapshots, prepared songs
and descriptive capabilities keep their existing owning data lifetimes.
Service operations remain serialized, non-reentrant and outside audio callbacks.

## 7. Required implementation tests

- Private construction/export and no raw-handle authority; a fake capability
  cannot enable the production native factory.
- Foreign provider, close/reacquire, identical-path reacquire, capability lifetime,
  close-on-exec, bounded descriptor usage and allocation-failure leak sweeps.
- Equal root/store and containing root reject; siblings, component-prefix
  neighbors and store-parent-above-root permit; missing/replaced roots reject.
- Saved unattached hints trigger no filesystem lookup. Same-mount ancestry works;
  absent mount-ID support and different mounts reject without primary mutation.
- Ancestor symlinks, root/store replacement, old epoch and late prepublication
  changes preserve prior memory and primary when rejection precedes publication.
- Create/Open baseline paths, uncertainty/recovery, no native admission through
  fake seams, and no allocation/fallible validation after confirmed save.
- Exact and beyond limits for root count, paths, parent walk, handles and shared
  payload accounting; no partial root list or independently reset allowance.

Exercise mount aliases only in a harness-owned private namespace when available.
Unavailable namespace privileges produce Skipped evidence with a reason, not a
Passed result. Do not alter unrelated mounts or require privileged changes to
make a test appear complete. Separate real results from injected missing-field,
allocation and mapping faults.

## 8. Native evidence and acceptance

Extend bounded receipt instrumentation for binding export, lease epoch checks,
root mapping/identity, mount-ID checks, ancestor termination and the final
prepublication fence. Record actual versus injected results, operation, errno on
failure, primary preservation and cleanup before harness artifacts disappear.
Overflow is an explicit evidence failure. Selected-call receipts remain distinct
from a complete syscall trace.

After instrumentation and integration review, obtain a fresh pinned run on the
maintainer's eligible WSL2 ext4 environment. Capture clean source revision,
mount/kernel/compiler details, compile/run/overall exits, receipt verification,
allocation/descriptor bounds and the new admission matrix. Preserve historical
bundles. Apply the existing documented username-redaction procedure to new logs;
record original/published hashes and replacement counts separately.

Native factory acceptance requires that run and explicit reviewer acceptance
for its exact revision and environment. The scoped decision is recorded below. Do not relabel `6a6337d`,
borrow its receipts for changed service code, or infer universal power-loss,
bare-metal, NTFS, cross-OS or hostile-writer guarantees.

## 9. Implementation order and sources

1. Review this capability/admission plan.
2. Implement lease epoch, owning binding and bounded ancestry checks with tests.
3. Integrate the private prepublication fence and experimental native factory.
4. Record fresh native receipts and request explicit scoped acceptance.
5. Continue durable root lifecycle transactions and application/CLI orchestration.

NCN evidence, manual device/lyric validation and the Windows gate remain separate.
The initial docs-only proposal changed no tests or completed roadmap checkbox.
The binding primitive adds one suite: current counts are 28 core / 31
audio-enabled. The storage completion checkbox remains open.

Primary API references:

- [statx](https://man7.org/linux/man-pages/man2/statx.2.html): FD observations,
  result masks and mount IDs. Ordinary mount IDs can be reused after unmount.
- [F_DUPFD / F_DUPFD_CLOEXEC](https://man7.org/linux/man-pages/man2/F_DUPFD.2const.html):
  descriptor duplication and close-on-exec.
- [open](https://man7.org/linux/man-pages/man2/open.2.html): directory-relative
  opening, no-follow and retained descriptor semantics.
- [mount namespaces](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html):
  bind mounts and per-mount observations.

These references explain API semantics, not runtime evidence or acceptance.

## 10. Current binding/ancestry primitive

[NativeStoreBinding.hpp](../library/NativeStoreBinding.hpp) implements private
binding export and admission helpers. The public descriptive object has no
constructor, raw descriptor or write/lock authority. Export lazily duplicates
one close-on-exec directory handle and caches one immutable value per lease.
Release clears the cache and epoch before closing the lease. Existing detached
provider operations do not export a binding or allocate its token/duplicate.

The helper accepts active mapping views only through its private plumbing.
Experimental service factories use their owned concrete Linux provider. TestAccess behind
OPENHDK_ENABLE_TEST_SEAMS can supply harness paths to the primitive; it cannot
create a writable native service. Structured primitive errors identify a root
by its input index. The owning service factory maps that index to its RootId;
the ordinal is not authority or a serialized identity.

Admission stages at most 32 retained root handles. It rejects different observed
mount IDs and checks both component containment and retained handle ancestry.
Recheck reopens root mappings with per-component no-follow rules and repeats
ancestry checks. It does not perform store saves, final coordinator fences,
owner attachment or durable mutation publication.

The helper's alreadyOwned argument excludes the binding/root strings it is
about to charge. The lease's retainedBytes contribution includes its cached
binding strings. Future service ledger handoff must deduplicate that same
capability when charging provider and admission storage. This integration is
not implemented here. Descriptor preflight reserves eight provider descriptors,
one binding descriptor and four transient descriptors, plus the active roots.
This conservative upper bound is at most 45; unrelated process descriptors are
outside it. The fixed guard records and kernel allocation are not logical text
payload bytes.

Tests cover private construction, stale/foreign epochs, lifetime, component and
ancestor containment, real different-mount rejection, missing-field injection,
root/store replacement, exact/beyond bounds and allocation-failure FD sweeps.
A seam that disables the lexical fast reject exercises real handle ancestry;
it is not evidence of bind-mount acceptance. The private mount-namespace case
is explicitly Skipped because its harness is not implemented. Local filesystem
eligibility bypass is test-only; native collector runs disable it.

Binding/factory receipt instrumentation is described in the current section below;
fresh native evidence remains pending.
The existing scoped acceptance at 6a6337d is unchanged. That primitive slice added no native factory or coordinator fence; the
following integration section records the later implementation.

## 11. Experimental factory and final admission fence

[DurableLibraryService](../library/DurableLibraryService.hpp) now exposes explicit
createLinux/openLinux factories. Create transfers one owner; Open restores a
fresh unattached owner. Both construct and own the concrete Linux provider.
Callers cannot supply a provider, capability, expectation, projection or fence.
Production lease eligibility remains ext4-only. Other platforms return the
provider's UnsupportedStorage result before native filesystem access.

After store open/reconciliation and optional restore, the factory exports the
binding and stages guards for active owner roots. Unattached hints trigger no
lookup. Admission precedes initial baseline save. Errors return no service;
structured binding errors include the mapped RootId when an active root index
is available. Public string argument construction can throw before entry; the
factory catches allocation failures inside its operation.

The service rechecks admission before staging an override. The coordinator has
a private fixed function/context fence installed only by the owning service.
It runs after the last cancellation/directory/token checks and before native
publication. The Unchanged return path invokes the same fence. Fixed error
storage records a failed recheck without allocation. No admission callback runs
between confirmed storage save and the service's noexcept memory swap.

Mapping drift retains prior memory and marks RecoveryRequired. A fence rejection
before publication returns NotCommitted and keeps primary bytes; publication or
sync uncertainty follows the existing explicit recovery protocol. Close releases
the store lease and retained admission guards. No root mutation, reattachment,
reopen-in-place or live application integration is exposed by this slice.

The provider charges its cached binding strings once. Service path accounting
adds only guard mapping copies; admission staging excludes the newly exported
binding from alreadyOwned, then charges it once. Coordinator input/baseline and
provider costs continue to use the same checkpoint allowance. Admission uses the
smaller of its configured payload allowance and the checkpoint allowance.

Native-service tests exercise real syscalls under the local seam-only eligibility
bypass, or production eligibility when the collector supplies an ext4 parent.
They cover baseline Create/Open, overlap rejection, mapped RootId, final and
NoChange fence rejection, confirmed-save allocation prohibition, uncertainty,
recovery, owner lifetime and factory allocation/FD rollback. The independent
fake service suite also verifies the private fence. Current counts are 28 core /
31 audio-enabled. Binding/factory per-case receipts, a fresh pinned native run
and explicit scoped acceptance remain pending. Existing 6a6337d acceptance and
historical evidence bundles are unchanged; Windows saves remain BLOCKED.

## Native admission receipt instrumentation

The binding and experimental native service targets now emit bounded per-case
[admission receipts](NATIVE-ADMISSION-EVIDENCE.md) for lease epochs, identity/mount checks,
ancestor termination and the final changed/NoChange fence. The existing collector
verifies both logs with Python 3 and records its verification exit code. Current
counts remain 28 core / 31 audio-enabled; binding checks are 74 and native service
checks are 33. A fresh pinned ext4 run is recorded in the evidence section below; explicit
scoped native factory acceptance is recorded below. Historical evidence and the `6a6337d` provider acceptance are
unchanged. Windows checkpoint acknowledgment remains blocked.

## Recorded WSL2 ext4 admission run

The maintainer supplied a [fresh pinned admission run](evidence/native-admission/2026-10-10-wsl2-ext4/README.md) at
`4f6924670d4272736984342c8294967650c60edb` on WSL2 `/dev/sdd` ext4.
All 28 compile/run exits, admission verifier and overall exit are zero. The
binding/service logs use bypass zero and contain 13 traces, 364 identity
observations and three fence cases. The published logs redact the local username
with original/published hashes and replacement counts. This is supplied execution
evidence, not independent source/run authentication. Explicit scoped factory
acceptance is recorded in the final decision below; mount-namespace alias evidence remains Skipped.
Historical `6a6337d` acceptance, Windows blockage and roadmap checkboxes are unchanged.

## Scoped native factory acceptance

The maintainer [explicitly accepted the factory/fence](NATIVE-ADMISSION-EVIDENCE.md#explicit-scoped-factory-acceptance--2026-10-10)
on 2026-10-10 for tested revision
`4f6924670d4272736984342c8294967650c60edb`, on WSL2 `/dev/sdd` ext4
`data=ordered`. This closes the OHK-BIND-030 factory/fence evidence gate only
within the recorded scope. Documentation merges do not relabel the tested
revision. Earlier Pending statements and supplied logs retain their historical
meaning; this final decision is authoritative. The `6a6337d` provider decision
remains separate and unchanged. Mount-namespace alias evidence remains Skipped;
Windows checkpoint acknowledgment, durable root mutations, application/CLI wiring,
NCN evidence and manual validation remain open. No storage release checkbox changes.

## Private staging follow-up boundary

[OHK-DURABLE-ROOT-030 slice 3.1](DURABLE-ROOT-LIFECYCLE-PLAN.md#10-current-slice-31-boundary)
now implements complete-owner staging and fake-provider registration. Experimental native services now use shared prospective guards and a scoped
operation-aware fence for registration/reattachment. Fresh affected evidence
remains pending. Historical receipts and `4f69246` scoped acceptance
are unchanged; no native root mutation acceptance follows from these fake tests.

## Prospective shared guard preparation

[NativeStoreAdmission](../library/NativeStoreBinding.hpp) now retains immutable
shared root guard objects. Private Stage validates prior-index correspondence,
rechecks unchanged guards against original identities, and admits at most one
new target under the same lease epoch. One explicitly retired old target may be
missing; same-path replacement is rejected. Candidate rechecks preserve bounded
ancestry/mount/containment rules. The service now installs a prospective
fence for experimental registration/targeted reattachment, while retaining
the old admission until commit/rollback. [Details and tests](DURABLE-ROOT-LIFECYCLE-PLAN.md#11-current-prospective-guard-primitive)
keep historical receipts and both accepted tested revisions unchanged.


## Experimental durable scan and song lifecycle

[OHK-DURABLE-ROOT-030 integration](DURABLE-ROOT-LIFECYCLE-PLAN.md#13-experimental-scan-and-logical-song-lifecycle-integration)
now stages complete owners for `scan`, `relocateSong` and `removeSong` while
sharing all original native guards. Every complete scan requires a new confirmed
checkpoint revision. Relocation invalidates source state even for an equal key;
removal preserves allocator history and never deletes source files. Discovery
and service staging share one payload allowance; diagnostics/results are owned
before commit. No fallible work is added between confirmed save and the
nonthrowing owner swap. New mutation receipts, a pinned native run and scoped
acceptance remain required. Windows/NCN/manual/application gates stay open.


## Native mutation receipt preparation

[OHK-DURABLE-ROOT-030 receipts](DURABLE-ROOT-LIFECYCLE-PLAN.md#14-bounded-mutation-receipts-and-verifier)
now cover 34 selected cases across all five service operations. Separate bounded
admission/provider traces, primary digest/projection observations, acknowledgment,
fence/pending-context outcomes and five allocation sweep receipts are checked by
verifier and negative tests. Hints, absolute paths, pointers and raw hint-bearing
wires are omitted. Supplied observations are not independent byte/provenance
proof; artifacts above the 256-byte capture cap explicitly remain unavailable.
Fresh pinned WSL2 ext4 execution and scoped mutation acceptance remain pending.
Historical evidence and the `6a6337d`/`4f69246` accepted revisions stay unchanged.
Windows remains BLOCKED; namespace aliases Skipped; application/CLI, NCN and
manual validation remain open. No storage release checkbox is completed.
