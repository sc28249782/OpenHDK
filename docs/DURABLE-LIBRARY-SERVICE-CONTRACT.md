# Durable library service contract

**Contract:** OHK-DURABLE-030
**Status:** Accepted by review and merge of PR #69; override service primitive
and fake-provider lifecycle/commit tests implemented. Native admission and other
durable mutations remain pending.
**Target:** 0.3.0 storage slice 5, after immutable live capture.

## 1. Purpose and scope

The [persistence contract](CATALOG-PERSISTENCE-CONTRACT.md) requires storage
commit before memory publication for persistent mutations. The detached store
cannot enforce this rule for a caller's library. Its expectation identifies a
store object, not a library owner. The live capture adapter acquires matching
snapshot, counter and root context, but it does not stage or persist a mutation.

This contract defines the service that joins those boundaries. The first
implementation slice MUST support complete user title/artist override replacement
only. It MUST reuse the accepted metadata validation and display rules. It MUST
NOT change checkpoint schema 1, source files, source validation state, root
policy/generation, allocator counters, lyric selection or playback bindings.

Durable root registration, scanning, relocation, removal and reattachment require
later staging slices. Existing in-memory APIs retain their current behavior.
This contract does not make those APIs durable. It adds no CLI, audio callback,
NCN support, database package or Windows synchronization sequence.

## 2. Exclusive service ownership

A service MUST exclusively own one SongDiscovery instance and one store
coordinator/provider lifetime. Do not bind borrowed mutable owner/store references.
Do not expose a mutable owner, catalog, store, provider or StoreExpectation to the
caller. Already-acquired const snapshots, displays and PreparedSong values may
outlive the service. They do not grant mutation access.

All service operations run on one serialized, non-reentrant control path. A
nonblocking operation guard MUST reject nested mutation/open/close calls with
Busy before staging or provider calls. Concurrent calls are outside the public
control-path contract; the guard does not make other state queries thread-safe.
Provider/control test hooks MUST NOT mutate the owner or re-enter service work.
Snapshot readers on other threads use only values acquired on the control path.

The provider MUST outlive its coordinator. Destruction/close MUST release only
owned resources. They MUST NOT infer a successful checkpoint from outstanding
artifacts. No service operation runs in the audio callback.

The service keeps its acknowledgment internally. A caller MUST NOT supply an
arbitrary snapshot, projection, token, root mapping or allocator state as the
candidate for a durable mutation. Detached capture/save remain separate APIs
with their existing trusted-caller scope.

## 3. Store/root admission

Before enabling native service writes, prove that the selected store parent is
outside every active registered song root. Reuse the native provider's retained
directory identity and validated canonical mapping. A caller-supplied path string
or a filesystem label is insufficient evidence of that binding.

The planned native factory MUST provide an owning, non-serializable binding
capability from the same provider lease. It identifies the retained parent
mapping/identity and selected primary name. It MUST NOT be publicly constructible
from numeric IDs or arbitrary paths. Provider checks remain responsible for
name-to-identity revalidation during save. Service containment checks supplement
those checks; they do not establish hostile-writer isolation or pathname CAS.

Check containment before initial checkpoint publication. Future durable root
registration/reattachment MUST enforce the reciprocal rule before staging a root
around the bound store. Those operations MUST remain unavailable through the
first service slice. Saved hints for unattached roots are advisory and establish
no active containment boundary. Attaching a hinted root later requires the same
reciprocal admission check.

The current provider interface does not expose this binding capability. Native
factory integration therefore requires a reviewed interface/implementation slice
and its affected acceptance evidence. Do not substitute a guessed parent path.
Fake-provider service tests may use a harness-owned capability behind test seams.
That capability MUST NOT enable a native provider or claim filesystem acceptance.

## 4. Establish an acknowledged baseline

Service states are Unbound, Ready, RecoveryRequired and Closed. Service Ready
means an admitted storage baseline; it does not mean any song is Ready.
The service starts in Unbound and accepts one explicit initialization mode.
The factory returns no writable service on any admission, allocation, decode,
restore, save or reconciliation failure.

| Mode | Required storage result | Owner baseline |
| --- | --- | --- |
| Create from an exclusively transferred in-memory owner | Open returns ExpectedAbsent; commit that owner's current capture | The exact transferred context becomes the baseline only after confirmed save |
| Open an existing checkpoint | Open validates the primary and completes provider reconciliation | Restore into a fresh owner; roots remain unattached and songs Invalid |

Create MUST NOT overwrite an existing primary. A create failure returns no
service and no durability acknowledgment. If create returns CommitUncertain,
report uncertainty and recovery descriptors. Do not return the transferred owner
as a writable service or automatically retry creation. Already-acquired immutable
inputs remain valid; the factory does not publish a candidate mutation to them.

For existing storage, use the fresh-owner restore adapter. Reuse persisted IDs,
counter high water, root generations/policies, hints and overrides. Do not import
runtime lineage, Ready authority or source tokens. Capture the restored owner
and compare its persistent projection with the opened projection, excluding only
store sequence. The stored sequence/token still comes from the validated open.

On successful initialization, retain the current snapshot/context and the
acknowledged store expectation as one baseline. The service MUST NOT claim an
arbitrary dirty owner is synchronized merely because it knows a current token.
There is no implicit adoption of a caller's newer in-memory mutations.

## 5. Override staging and commit

A Ready service performs complete title/artist replacement as follows.

1. Acquire the operation guard. Validate configuration and find the selected
   SongId in the service's current snapshot. Do not derive IDs from metadata.
2. Validate the complete override request. Absence clears a field; a present
   empty string fails. Preserve exact accepted UTF-8 bytes. Equal values follow
   the no-op rule below. A changed request requires a new catalog revision.
3. Stage a private catalog snapshot with the existing lineage and unchanged
   roots, identity, source state and shared metadata. Replace both override
   fields in this snapshot. Do not publish it through the live owner.
4. Construct the candidate projection from this staged snapshot and the exact
   captured counter/root mapping belonging to the unchanged live baseline.
   A private staging descriptor MUST carry that association. Do not add a public
   arbitrary-snapshot overload to live capture. Reuse codec validation/bounds.
5. Prepare the operation result and all data required for memory publication.
   Complete every allocating/throwing operation before calling the store save
   path's namespace-publication boundary. Retain the old snapshot/context.
6. Call the coordinator with the private candidate and internal expectation.
   The coordinator validates the token/history and performs its accepted protocol.
7. On a confirmed Saved acknowledgment, publish the already-staged snapshot with
   one noexcept shared_ptr swap. Install the returned expectation and baseline
   context with non-throwing moves. Return the prepared result and cleanup warning.

The staged snapshot advances catalog revision once for an actual override change.
It MUST NOT consume RootId/SongId or attachment generations. Staging keeps the
same in-process lineage so historical same-owner snapshots retain their accepted
prepare semantics. Restoring from disk remains a separate fresh-lineage action.

The adapter MUST NOT report success unless the coordinator acknowledges the
candidate's captured revision and expected successful status. These are internal
protocol invariants, not an excuse to invent a rolled-back disk result. An
inconsistent acknowledgment from a defective/custom provider chain MUST retain
old memory, block service writes and report an explicit protocol fault requiring
recovery. Do not allocate an error message or throw after storage publication.

After confirmed save, cancellation MUST NOT prevent memory publication. No hook,
callback, path conversion, validation, allocating result construction or fallible
source check is permitted between storage acknowledgment and the memory swap.
Use static assertions and allocation-prohibition tests for the actual result,
expectation and publication operations. Final shared_ptr release may deallocate
on this control path; this is not a callback handoff or a no-deallocation claim.

## 6. No-op and failure outcomes

An equal valid request stages no new snapshot and consumes no revision. The
service still asks the coordinator to verify the current primary/token and exact
baseline projection. Return NoChange only after a confirmed Unchanged receipt.
This allows no-op at revision/sequence exhaustion without fabricating a new
commit. Validation, allocation or external-storage failure may still reject the
operation. NoChange does not claim that other in-memory operations became durable.

| Observation | Memory and baseline | Service action |
| --- | --- | --- |
| Invalid request, staging/budget/allocation failure | Retain exact old publication and acknowledgment | Return structured failure; no save call for staging failures |
| Definite NotCommitted store error | Retain exact old publication and acknowledgment | Return nested store error and cleanup warning; admit a later explicit request |
| StaleCheckpoint or detected baseline/protocol mismatch | Retain old publication | Enter RecoveryRequired; do not install a foreign/new disk token |
| CommitUncertain | Retain old publication; do not adopt the staged candidate | Enter RecoveryRequired; preserve coordinator fault/artifact information |
| Saved plus cleanup warning | Publish the staged snapshot and new acknowledgment | Return committed success with separate warning |
| Confirmed Unchanged | Retain exact publication and token | Return NoChange |

Cancellation is honored only before storage publication under the coordinator's
existing rule. Exceptions in unsupported caller hooks/providers are outside the
provider contract; production integration MUST NOT introduce throwing callbacks
at or after publication. Return structured, nonallocating operation/field errors
and nested metadata/capture/store errors. Never return a partial staged snapshot
as a usable live owner.

## 7. Recovery and process interruption

RecoveryRequired rejects persistent mutation and save attempts. Already-acquired
snapshots/prepared values remain readable. The service MUST NOT automatically
retry, choose a backup/artifact, restore a candidate or mark old memory durable.

Recovery is an explicit caller action: close the faulted service, validate/reconcile
the explicitly selected primary through a new open, and restore a fresh owner.
Return a replacement service only after complete admission. Do not rewrite the
old owner's lineage or mutate historical PreparedSong/observer values in place.
Missing/invalid primary fails closed. A visible old/new primary does not prove
that the uncertain operation was previously durable.

A process can terminate after confirmed disk save and before the memory swap.
On restart, the selected committed checkpoint is the recovery input; volatile
memory from the terminated process is gone. A crash after memory publication but
before returning a result can leave the caller without a success receipt. Do not
promise exactly-once caller delivery or automatic retry deduplication. There is
no power-loss guarantee beyond the accepted provider/environment evidence.

The first slice MUST test this ordering with a fake provider. Native process-cut
coverage for the service boundary is separate acceptance work. It MUST identify
the exact revision/environment and distinguish injection from observed native
behavior. The existing provider acceptance remains pinned to `6a6337d` on the
recorded WSL2 `/dev/sdd` ext4 environment. Documentation does not extend it.
Windows checkpoint acknowledgment remains BLOCKED.

## 8. Bounded coexistence

Use one logical operation allowance, at most 64 MiB by default. Existing codec,
metadata and source limits remain upper bounds and may be reduced for tests.
Reject invalid configuration or an over-budget operation before copying/growth.
Do not assign a fresh allowance to each helper.

The ledger MUST cover caller-retained payload declared through alreadyOwned,
current/staged snapshot locator copies, source/override records shared once by
object identity, changed override validation/retained bytes, registered path/hint
storage, retained baseline/candidate projection strings and the coordinator's provider/wire/verify
storage. Distinct identical objects are distinct charges. Old/new records that
remain alive during the operation both count. Conversion/validation temporaries
must be reserved before allocation. Do not charge candidate projection strings
both as adapter alreadyOwned and again as coordinator-owned input; define the
handoff ledger explicitly in implementation tests.

An operation result exposes an owning snapshot only after success. Snapshots
already retained elsewhere are caller-owned payload if they extend peak storage
beyond the service's known current/staged objects. Descriptor counts stay bounded
by existing root/song limits. Native descriptors, allocator overhead and stack
scratch are not included in a total process-memory claim.

## 9. Required tests and implementation order

The first implementation PR MUST cover these cases with an independent fake
provider and existing synthetic fixtures. No user MIDI/SF2/legacy database is
required.

- Exclusive owner/store admission; arbitrary snapshot/token/projection injection
  is unavailable through the service mutation API.
- ExpectedAbsent create, existing-primary rejection, fresh-owner open/restore,
  exact baseline comparison and no auto-attachment/source scan.
- Complete two-field override staging, absence/empty semantics, exact Thai/BOM/
  combining bytes, missing SongId and unchanged source/identity/counters.
- All precommit failure phases preserve the exact old snapshot pointer, token,
  projection and allocator history. Include allocation sweeps and cancellation.
- Store publication observes old live memory. Confirmed sync occurs before the
  single memory publication. No app allocation or throw occurs after publication.
- CommitUncertain retains memory/artifacts and blocks writes. Stale primary and
  inconsistent acknowledgment require explicit recovery. A failed recovery does
  not alter the old owner or historical values.
- Cleanup warning accompanies committed memory. Late cancellation does not undo
  or suppress publication. No-op verifies primary and works at counter exhaustion.
- Exact/beyond shared-ledger boundaries, no double allowance, same-provider/store
  reentry and immutable context lifetime after close/recovery/destruction.
- Test-only store/root capabilities cannot enable native writes. Native factory
  tests later cover store-inside-root and reciprocal root admission.

Implement in reviewable slices under this accepted contract:

1. Private override staging/projection and fake-provider service lifecycle,
   baseline admission and confirmed-commit publication.
2. Reviewed provider binding capability and native factory containment admission;
   run the affected scoped evidence before claiming native durable service support.
3. Separate durable staging for register/scan/relocate/remove/reattach, with
   counter/root-map publication and their own filesystem consistency rules.
4. Application/CLI wiring and manual validation under the release checklist.

Do not expose a partial first slice as a complete persistent song library. This
contract completes no roadmap checkbox and changes no released v0.2.0 behavior.

## 10. Current override service implementation boundary

`library/DurableLibraryService.hpp` implements the service state/result types,
exclusive owner/provider/coordinator lifetimes, same-lineage override staging,
private projection association and acknowledgment-before-memory publication.
It exposes snapshot/state queries, override replacement and close. It exposes
no mutable owner/store, arbitrary candidate projection or expectation input.
The query surface does not yet add library/application preparation orchestration.

There is no production construction factory. Under OPENHDK_ENABLE_TEST_SEAMS,
`DurableLibraryServiceTestAccess` admits only an owned
`DurableLibraryTestProvider`, an in-memory test provider category. A native
LinuxCheckpointProvider cannot be passed to these factories. This is a protocol
primitive tested with an independent two-slot namespace model, not native durable
library support. Native binding/root containment admission remains slice 2.

Create consumes an owner, opens ExpectedAbsent and saves its current capture.
Open validates/reconciles a primary, restores a fresh unattached owner and
compares its capture with the opened projection excluding sequence. Factories
return no writable service or success receipt on failure. There is no reopening
of a faulted owner in place; close followed by a separate open returns a new owner.

Override staging uses a private SongCatalog view of the current snapshot/counters.
The existing metadata transaction writes only that view. Its snapshot retains
lineage, source records and validation state. The private projector uses the
baseline's retained root mapping/counters and verifies unchanged roots and source
keys. It does not convert current paths again or accept a caller snapshot.
A confirmed Saved receipt with the matching revision precedes one noexcept swap.
Receipt and expectation transfers have compile-time nothrow assertions. NoChange
requires the store's confirmed Unchanged result. Protocol mismatch or stale/uncertain
storage enters RecoveryRequired and leaves the live snapshot unchanged.

The operation ledger charges the baseline projection and retained registered
paths during metadata staging. Private projection staging charges current/staged
locator copies and shared source/override records once per identity, paths,
baseline strings, output strings and validation temporaries. At save handoff,
provider bytes and candidate projection input strings are charged by the
coordinator, not again by adapter alreadyOwned. NoChange/create share the same
baseline/input projection and do not charge a second projection copy. New owning
results retained beyond the operation are caller-retained payload in later work.
Descriptor counts remain bounded; this is not a total process-memory ceiling.

The new CTest target defines test seams only for its translation unit. Its 64
numbered checks include complete field replacement, Thai/BOM/combining bytes,
factory and mutation allocation sweeps, exact/beyond coexistence bounds, Ready
source/prepared lifetime retention, old memory at publish/sync, cleanup warning,
late cancellation, allocation prohibition starting at namespace publication,
uncertainty with old/new primary, explicit recovery, no-op/exhaustion and reentry.
A test-only malformed acknowledgment confirms protocol-fault handling without
claiming that a native provider produces that behavior.

The evidence collector adds this target to its seam list. Historical receipts,
provider implementation/tests and the accepted revision/environment are unchanged.
A future collector run must pin its own new revision; this change does not relabel
`6a6337d` or prove native service acceptance. Windows synchronization, other durable
mutation staging, NCN, application wiring and manual validation remain open.

## 11. Native binding primitive and pending factory

[OHK-BIND-030](NATIVE-SERVICE-BINDING-PLAN.md) is accepted. Private lease-epoch
capability and retained-handle containment primitives are implemented; the
coordinator fence and production native factory remain pending.
Its first factory would reject active roots on different mounts and cap active
roots at 32. These are implemented primitive limits, not enabled native service support.
Fresh affected native evidence and review precede factory acceptance.
