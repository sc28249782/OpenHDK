# Durable root lifecycle staging and admission plan

**Plan:** OHK-DURABLE-ROOT-030
**Status:** Accepted by review and merge of PR #77; private complete-owner staging
and fake-provider durable registration implemented. Private prospective shared
guards and experimental operation-aware register/reattach integration implemented;
experimental scan/song lifecycle and bounded mutation receipt instrumentation implemented;
fresh pinned affected mutation execution/acceptance remains pending.
**Target:** OHK-DURABLE-030 slice 3, after scoped native factory/fence acceptance.

## 1. Purpose and current boundary

The [durable service contract](DURABLE-LIBRARY-SERVICE-CONTRACT.md) currently
implements baseline admission and durable user overrides. Its private projection
path deliberately requires unchanged roots, counters and song identities. The
existing in-memory register, scan, relocate, remove and reattach APIs publish
their results immediately; wrapping them with a later save would violate
commit-before-memory-publication.

The [native binding plan](NATIVE-SERVICE-BINDING-PLAN.md) supplies lease-bound
store authority and retained root guards. Its current fence rechecks every
published root. That cannot directly repair a missing old root: the fence would
reject the old mapping before the replacement could be staged. Rebuilding all
guards from current paths would instead silently adopt replacements of unrelated
roots. Neither behavior is a suitable transaction rule.

This accepted plan defines private complete-owner staging and prospective admission.
Sections 10 and 11 record the earlier fake-provider and guard primitive slices.
Section 12 records the experimental service lifecycle integration. No new native
acceptance, CLI command or completed roadmap item is supplied. Root unregistration, root policy editing, physical source moves/deletion,
schema changes and automatic recovery remain outside scope. Logical song
relocation/removal changes catalog records only.

## 2. Private staged owner and checkpoint association

Each operation MUST start under the existing non-reentrant service guard and
verify its acknowledged baseline. Callers cannot supply a snapshot, projection,
allocator counter, directory mapping, store expectation or admission capability.

A private staging descriptor MUST retain, as one associated candidate:

- The complete candidate catalog snapshot with the current owner's lineage.
- Exact candidate allocator high-water counters, including unused/deleted IDs.
- Every registered root's ID, policy, attachment generation, attached flag,
  active path and saved hint where present.
- A captured projection derived from that same candidate, plus its association
  with the current baseline and internal store expectation.
- Prospective native guards and their RootId mapping under the same retained
  provider lease epoch, and a fully constructed success result.

Do not restore a checkpoint to create staging: restore intentionally creates a
fresh lineage, unattached roots and Invalid songs. Reuse existing validation and
reconciliation logic through private staging helpers; do not independently
reimplement SMF/KAR parsing, metadata extraction or identity reconciliation.
Candidate ID/generation allocation MUST affect only staging until commit.

Implementation MUST choose a complete staged-owner publication representation
that can be installed without throwing, for example an owned staged context
swapped into the service. It MUST prebuild all associated guard, baseline,
expectation and result transfers. Static assertions and allocation-prohibition
tests MUST cover the entire confirmed-save-to-result path. The serialized service
must expose a coherent new context only after these nonthrowing transfers; this
does not claim an atomic multi-field publication to concurrent mutable callers.

## 3. Proposed operation semantics

These names describe the planned operations, not implemented public signatures. Preserve the
existing [library](KARAOKE-LIBRARY-CONTRACT.md),
[metadata](CATALOG-METADATA-POLICY-CONTRACT.md) and
[reattachment](ROOT-REATTACHMENT-CONTRACT.md) semantics:

| Operation | Staged identity and state | Confirmed publication |
| --- | --- | --- |
| Register root | Validate policy before filesystem work/ID allocation; validate canonical directory and root overlap; verify the store is outside the root; allocate one RootId with generation 1. | Revision +1, nextRoot advances once, mapping and guards publish together. |
| Complete scan | Apply captured root policy, bounded source verification and metadata reconciliation; retain IDs for existing keys, stage new IDs for new keys, retain missing entries and overrides. | Revision +1 for every successful complete scan; nextSong advances only for new entries; attachment generation unchanged. |
| Relocate song | Validate existing IDs, locator and collisions; retain SongId/overrides, invalidate source tokens/metadata and state. No file move. | Revision +1; counters unchanged. Equal root/locator retains the current invalidating behavior, rather than introducing a new no-op. |
| Remove song | Remove the known catalog row and overrides. No file deletion. | Revision +1; counters do not decrease. Rediscovery allocates a new SongId without the removed overrides. |
| Reattach root | Validate/retain target identity; preserve RootId, SongIds, locators, policy and overrides; invalidate all songs under that root and clear source tokens/metadata. | Changed binding: revision and attachment generation +1; counters unchanged; mapping and guards publish together. |

A scan whose persistent identity rows are unchanged still publishes a new catalog
revision under the existing scan contract. That revision is serialized, so this
is a changed checkpoint requiring confirmed Saved, not a volatile-only scan or an
Unchanged store acknowledgment. Enumeration/read/hash verification remains a
consistency check, not a filesystem-wide atomic snapshot.

Same-canonical-path reattachment is NoChange only for an existing active binding
that passes the service's retained-identity checks. It performs no scan or
invalidation, and may succeed at counter exhaustion after token/fence validation.
First attachment of a restored unattached root is changed even when selecting
its saved hint. Saved hints cause no filesystem lookup until explicitly selected.
Real changes fail at relevant ID, generation or revision exhaustion without
consuming any counter. Root removal remains excluded by checkpoint history rules.

## 4. Targeted repair without unrelated identity adoption

Registration, scan, song relocation/removal and ordinary no-op requests MUST
recheck all current active guards. A changed reattachment alone may designate
one owned RootId whose old binding is being retired. Before staging, validate
store authority/epoch and every other active root against its retained identity.
The designated old root need not exist; no unrelated guard may be exempted.

Repair of a missing or replaced old directory requires a validated genuinely
different canonical target. A same-path request with detected identity drift
MUST fail closed and enter RecoveryRequired; it must not silently become a
changed reattachment or refresh authority. Supporting explicit same-path
replacement would require a separate reviewed amendment to OHK-ROOT-030.
Existing overlap/equivalence reservations and probe-error rules still apply.

The proposed target MUST be retained before staging and reverified before
publication. Unchanged roots MUST retain their old guards or be compared against
them; constructing fresh guards solely from current paths is not verification.
Retained handles prevent identity recycling while staged. These observations do
not provide hostile-writer isolation or an atomic pathname compare-and-swap.

## 5. Prospective admission and final fence

Before checkpoint publication, validate the complete prospective active root set
using component-wise no-follow walks, retained native identity/mount observations
and handle ancestry. Apply the existing same-mount, 32-active-root and bounded
walk rules. The store MUST remain outside every prospective active root;
the store parent may be above a root. Unattached roots/hints remain inactive.
Canonical text alone cannot replace retained identity evidence.

The private service-owned fence MUST select the operation's staged admission
descriptor while saving a candidate. It MUST validate unchanged mappings against
their original guards and changed/new mappings against their retained candidate
guards under the unchanged provider lease epoch. It MUST also fence NoChange
acknowledgments with the current validated admission. No public fence setter or
caller callback is added. A scoped internal installation must clear pending
context on every failure/return without exposing candidate guards as live state.

The fence remains after the coordinator's final token/directory/cancellation
checks and before native publication. No fallible admission work, filesystem
lookup, cancellation, hook or allocation may follow confirmed storage save and
precede memory publication. Binding errors retain nested detail and an owned
RootId mapping where available; do not allocate diagnostic path copies.

Rejected candidate configuration/containment retains the old service publication
and Ready state when current authority is still valid. Detected store/epoch or
unchanged-root drift enters RecoveryRequired. Only the targeted changed-root
repair above may proceed despite that root's old mapping failure. An already
RecoveryRequired service does not gain a repair bypass; explicit close,
reconcile and fresh-owner recovery remain required.

## 6. Commit and recovery outcomes

All validation, source work, allocations, candidate capture, peak accounting and
result construction MUST finish before the publication boundary. Hand the exact
candidate projection and its single operation allowance to the coordinator.
Verify confirmed Saved, matching captured revision and a present acknowledgment
token using the existing protocol. Then install the complete staged context and
its prebuilt admission/baseline/expectation by nonthrowing transfers.

| Outcome | Service behavior |
| --- | --- |
| Saved with valid acknowledgment | Publish the complete staged context once; report cleanup warnings independently of commit success. |
| Validated NoChange | Retain current context and counters; return the verified acknowledgment without a new revision. |
| Cancellation or definite prepublication failure | Retain snapshot, mappings, counters, guards and baseline; never return staged IDs as admitted identities. |
| Stale baseline/token, admission drift or acknowledgment protocol fault | Retain prior memory and enter RecoveryRequired under existing service rules. |
| CommitUncertain | Retain prior memory, fault writes, preserve coordinator artifacts and require explicit reconciliation/fresh restore. |

A late cancellation cannot undo confirmed storage commit. Process interruption
after disk commit but before memory installation recovers from the committed
checkpoint, with fresh lineage, Invalid songs and unattached roots. No exactly-once
delivery receipt or automatic adoption of a backup/candidate is promised.

## 7. Shared payload and descriptor peaks

Use one bounded operation ledger for current/candidate snapshots, source metadata,
overrides, projections, paths/hints, conversion scratch, provider/binding strings
and staged admission. Charge shared records/guards once per object identity while
retained; identical bytes in distinct owned objects are separate charges. Reuse
the coordinator's existing coexistence accounting without a second allowance.
Logical payload accounting is not a total process-memory limit.

The existing 45-descriptor admission bound applies to the peak old-plus-candidate
context, not separately to each. Propose private immutable shared guards for
unchanged roots, preserving exact identity/mount/mapping association; copied
references must not duplicate descriptors. Keep retired changed-root guards alive
until commit/rollback and count candidate target handles and transient walks.
Review the complete provider/binding/staging descriptor inventory in code.

Do not widen the 45 bound or 32-active-root limit. With the current conservative
13-descriptor overhead, 32 old root guards plus one distinct replacement guard
exceed the bound: reject before commit rather than discarding rollback guards.
Registration from 31 to 32 active roots can share the 31 unchanged guards.
Maximum record capacity does not promise every mutation fits every peak budget.

## 8. Required tests and evidence

Fake-provider tests MUST cover complete context retention at each allocation,
cancellation, write, sync, fence and acknowledgment failure; staged ID/counter
rollback; revision/generation exhaustion; scan revision-only persistence;
relocation/removal/rediscovery; and historical snapshot/PreparedSong lifetime.
Check new mappings, counters, policy and candidate projection together, not just
display text. Ban allocation/throwing work throughout confirmed-save publication.

Native tests MUST cover reciprocal containment for new roots/targets, missing-old
root repair, replaced unrelated roots, same-path drift rejection, explicit saved
hint attachment, unchanged-guard sharing and descriptor peaks/FD cleanup.
Exercise changed and NoChange fences, late target replacement, uncertainty and
fresh recovery. Namespace alias tests require harness-owned namespaces; absent
privileges remain Skipped with a reason, not Passed.

Extend bounded receipts to associate operation, published versus candidate
revision/counters/mappings, guard observations, fence outcome and retained primary
token/projection before cleanup. Separate injected observations from native ones;
expose no authority pointers/tokens. Apply the established username redaction and
original/published hash accounting to new evidence. Historical bundles stay
unchanged. Instrumentation/runtime changes require a fresh pinned affected native
run and explicit scoped acceptance; neither `6a6337d` provider acceptance nor
`4f69246` factory/fence acceptance advances automatically.

## 9. Review and implementation order

1. Review and accept this staging/repair/resource plan before runtime changes.
2. Add private complete-owner staging/capture and fake-provider durable root
   registration tests first; retain the override path's existing behavior.
3. Add shared prospective guards and operation-aware fence integration, then
   native registration and targeted reattachment in separately reviewable slices.
4. Add durable scan and logical song relocation/removal staging with the same
   projection/publication protocol and accepted source validation behavior.
5. Collect affected bounded receipts, run fresh pinned WSL2 ext4 evidence and
   seek explicit scope-bound acceptance before claiming those native mutations.

Application/CLI wiring follows these service slices. Windows checkpoint
acknowledgment remains BLOCKED; NCN evidence and manual device/lyric validation
remain separate gates. The implemented slice keeps 28 core / 31 audio-enabled suites and all roadmap
completion checkboxes unchanged.

## 10. Current slice 3.1 boundary

[DurableLibraryService](../library/DurableLibraryService.hpp) now stages an owned
[SongDiscovery](../library/SongDiscovery.hpp) with the same catalog lineage, exact
allocator counters and copied root paths/hints. Private registration reuses
existing policy/directory/overlap checks and captures that candidate owner.
Confirmed Saved, matching revision and a token precede one nonthrowing owner
swap plus prebuilt baseline/expectation transfers. Failures expose no staged
RootId, snapshot or commit receipt. Old snapshots and PreparedSong remain owned.

The original slice made registration available only through the fake-provider
test seam and rejected native services before staging. Section 12 supersedes
that implementation boundary with experimental native registration/reattachment;
it does not advance historical native acceptance.
Discovery failures carry nested structured diagnostics; uncertainty/protocol
faults retain prior memory and require explicit recovery.

The [service suite](../tests/durable_library_service_tests.cpp) now has 100
numbered checks, including allocation sweeps, shared-budget boundaries, staged
ID rollback, storage/cancellation/fence failures, old context during publish/sync,
cleanup warning, reentry, exhaustion, restore and prepared-input lifetime. The
[native suite](../tests/native_durable_service_tests.cpp) originally had 34 checks
including rejection of the unsupported registration path; it now has 68 checks
for the section 12 integration. Counts remain 28 core /
31 audio-enabled. Fake-store success is not native registration evidence.

Historical bundles and the `6a6337d` provider / `4f69246` factory/fence decisions
remain unchanged. The prospective guards and experimental operation-aware register/reattach fence
are now implemented. Next: scan/song relocation/removal and
fresh affected native receipts/acceptance. Windows checkpoint acknowledgment
remains BLOCKED; app/CLI, NCN and manual validation stay separate.

## 11. Current prospective guard primitive

[NativeStoreAdmission](../library/NativeStoreBinding.hpp) now owns immutable shared
guards instead of per-admission descriptor copies. Private Stage accepts a
complete prospective list associated by prior guard indices. Each unchanged
index is used once with its exact stored path; every old guard must be retained
except one explicit retired target. At most one new guard is admitted. Retirement
requires a different target path and does not authorize same-path identity drift.

Unchanged guards are rewalked against original identities before staging and
again in the candidate's final recheck. A designated old target may be missing;
unrelated drift remains a failure. New targets retain separate handles and must
pass same-mount, root/root and reciprocal store/root containment checks. Errors
with a Stage root index identify candidate rows; missing correspondence without
a candidate row has no index. The owning service must still validate RootId and
generation correspondence. No public caller can construct admission guards.

Stage accounts for binding and old guard paths once, plus distinct new guard
paths. Its alreadyOwned input excludes those guard strings and includes other
coexisting operation payload; this is one allowance, not an extra budget. It
checks the 13 + old guards + new guards descriptor peak against both configured
bounds before opening the candidate. Active-root/descriptor bounds cannot grow
through staged contexts. Caller/service must retain the current admission until
commit/rollback so retired handles remain alive. Guard object/allocator overhead
is bounded by counts, not represented as a total process-memory measurement.

The [binding suite](../tests/native_store_binding_tests.cpp) now has 108 checks,
including exact/beyond payload and descriptor boundaries, sharing/lifetime,
missing targeted roots, unrelated and late drift, mount faults, allocation sweeps
and epochs. The primitive slice supplied no service fence or native mutation. Section 12
now integrates service-owned candidate mapping/fencing and experimental root
transactions. A complete new receipt matrix remains pending, followed by
fresh affected native receipts and
pinned execution/acceptance. Historical `6a6337d` and `4f69246` decisions remain
revision-bound; no later runtime revision borrows their acceptance.

## 12. Experimental root service integration

[DurableLibraryService](../library/DurableLibraryService.hpp) now exposes
experimental `registerRoot(path, policy, StoreControl, alreadyOwned)` and
`reattachRoot(RootId, path, StoreControl, alreadyOwned)` on its serialized control
path. The latter returns Updated or Unchanged plus an owning snapshot and
confirmed receipt; failures have no success status/snapshot/receipt. Registration
failures have no admitted RootId. Existing in-memory APIs remain separate.

The service checks its baseline, clones the complete same-lineage owner, and
constructs prospective admission under the owned provider epoch before invoking
discovery's existing mutation validation. It retains a canonical target handle
before mutation, verifies exact candidate paths, owned RootIds, policies and
attachment generation correspondence, then captures that same candidate. Active
guard ordering is independent of checkpoint root-record order: explicit first
attachments of restored hints can arrive in any order. Hints remain inactive
until selected. No callers can supply guards, counters or mappings.

Only a changed reattachment at a different canonical path retires its designated
old guard. Every other guard is shared and reverified against the original
identity. Same-path drift faults the service; a valid same-path NoChange still
requires token/fence validation and may succeed at generation/revision exhaustion.
An already RecoveryRequired service has no repair bypass. Store/root containment,
same-mount rules and the 45-descriptor peak remain enforced by the private
primitive. Configuration/target rejection can remain Ready when old authority
is valid; detected old authority or unrelated drift enters RecoveryRequired.

A fixed borrowed pending context selects candidate guards/RootId mapping during
coordinator.save. RAII clears it on all returns and exceptions. Changed saves
require Saved, matching revision and token before nonthrowing complete-owner,
baseline, admission/mapping and expectation transfers. NoChange requires
Unchanged and retains the current context. The coordinator publication ordering
is unchanged; no fallible work occurs between confirmed save and memory install.
Nested fence errors can identify an owned candidate RootId for diagnostics;
that field is not a success/admission receipt for a staged registration.

One ledger includes old/candidate paths, snapshots, projections and guard union.
Shared guard strings are counted once; fresh target paths add storage. Source
metadata cleared in the candidate remains charged while the old snapshot owns
it. The provider already charges shared binding strings. Logical payload counts
are not total process-memory limits, and a maximum record count does not promise
every operation fits its old-plus-candidate peak.

The native service suite has 68 regression checks, including allocation sweeps,
FD rollback/cleanup, confirmed-save allocation bans, uncertainty/recovery, missing
old target repair, unrelated and late drift, reverse-order hint attachment,
same-path NoChange and exhaustion. These new cases do not yet emit a complete
bounded mutation receipt matrix. Local overlay bypass is regression coverage;
namespace alias evidence remains Skipped. Fresh affected receipts, pinned WSL2
ext4 execution and explicit scoped mutation acceptance remain required. Both
`6a6337d` and `4f69246` historical decisions and all evidence bundles are unchanged.
Next: affected scan/song and root mutation receipts/acceptance, then app/CLI wiring. Windows checkpoint acknowledgment remains BLOCKED; NCN and manual
validation stay separate. Counts remain 28 core / 31 audio-enabled.


## 13. Experimental scan and logical song lifecycle integration

`DurableLibraryService::scan`, `relocateSong` and `removeSong` now stage a complete
same-lineage owner. They reuse the existing discovery/catalog validation rather
than introducing another parser, extractor or identity algorithm. All attached
roots retain their original shared guards. The service verifies unchanged root
mappings, policies, attachment generations and nextRoot before capture. A scoped
candidate context invokes the same final admission fence before checkpoint
publication. Confirmed Saved, revision equality and a token precede nonthrowing
owner/baseline/admission/expectation installation.

Every successful complete scan increments the revision and checkpoint sequence,
even when persistent identity rows are identical. Root policy controls extraction;
source metadata and Ready/source tokens remain volatile. Diagnostics and candidate
counts are returned only after confirmed commit; failures return no staged
snapshot or receipt. Discovery and store cancellation both apply before commit.
Scan limits are reduced by retained service context so discovery does not receive
a second payload allowance. Returned diagnostic locators remain charged through
capture/save. Old-only metadata and overrides remain charged while historical
snapshots coexist with the candidate.

Relocation is logical only: it retains SongId/overrides and invalidates source
tokens/metadata, including an equal root/locator request. Removal deletes only the
catalog row; it never deletes the source or lowers allocator high-water. A later
scan rediscovers existing bytes with a new SongId and no removed overrides.
Catalog errors are nested in the service error. Neither operation refreshes root
guards or attaches saved hints.

The fake suite has 124 numbered checks and the native service suite has 78.
New coverage includes revision-only scans, diagnostics, logical source preservation,
remove/rediscovery, historical PreparedSong ownership, cancellation, prepublication
faults, uncertainty/fresh recovery, relocate/remove allocation sweeps and scan save-stage sweeps, shared budget
boundaries, old memory during publish/sync and allocation bans after publication.
Native tests cover late original-root drift for all three operations, owned RootId
mapping, primary/memory retention, pending-context cleanup and FD rollback. These
are regression assertions, not a completed bounded native mutation receipt matrix.
Local overlay eligibility bypass does not provide native ext4 acceptance.

Next: bounded receipts for all affected root/song mutation paths, a fresh pinned
WSL2 ext4 run and explicit scoped acceptance, then application/CLI wiring.
Historical `6a6337d` and `4f69246` decisions and all supplied evidence bundles stay
unchanged. Windows checkpoint acknowledgment is BLOCKED; namespace alias evidence
remains Skipped, and NCN/manual validation remain separate. Suite counts remain
28 core / 31 audio-enabled; the storage release checkbox stays open.

Scan allocation injection starts at the existing coordinator BeforeStaging
checkpoint, after filesystem discovery and capture. It does not sweep standard
library traversal allocations: the local libstdc++ recursive directory iterator
terminates on an injected allocation failure inside its constructor. Relocate
and remove are swept from operation entry. These tests do not claim a total
process-memory bound or comprehensive system-library allocation coverage.


## 14. Bounded mutation receipts and verifier

The native service target now emits a separate MUTATION receipt matrix through
[test-only helpers](../tests/NativeMutationReceipts.hpp). There are 34 cases:
5 operations times Saved/write-failure/late-fence/uncertainty/late-cancellation,
plus targeted missing-root repair, same-path NoChange, equal-key relocation,
remove/rediscovery, same-path drift, unrelated drift, reciprocal containment,
explicit saved-hint attachment and the 32-root replacement descriptor peak.
The five allocation sweeps emit operation, scope, failure count, rollback FD
observation and successful FD count. Registration legitimately adds one guard FD;
other successful sweep operations preserve their warmed baseline count. Scan
injection still starts at coordinator BeforeStaging after discovery/capture.

Before fixture cleanup, 69 primary observations report full-wire SHA-256,
checkpoint footer digest, sequence/revision/high-water and selected detached
root/song rows. They omit hints and absolute paths. Root IDs in these rows are
detached checkpoint numbers, not live identity capabilities. The final fence's
RootId mapping is reported as equality/association, not an authority value.
Original-guard sharing and old memory at the post-rename test seam are booleans;
no pointers or epoch tokens are logged. Late cancellation is observed after
rename and cannot undo a committed save. Uncertainty is followed by explicit
close/open and full captured-versus-stored projection comparison except sequence.
Restored roots remain unattached.

Admission and provider observations use the existing fixed 256-record buffers,
with overflow reported and rejected. Selected native results and injected faults
remain distinct. A seams-only retained-artifact snapshot invokes existing fstat/
bounded pread instrumentation before uncertain service closure; it never writes,
unlinks or releases an artifact. Artifact identity/size/capture status is printed
for 45 captures. The existing 256-byte capture cap remains: larger artifacts
explicitly report EFBIG/unavailable. Raw hint-bearing artifact bytes are omitted;
only a digest is printed when capture succeeds. No full artifact-byte availability
or independent reconstruction of omitted wire/path fields is claimed.

The [mutation verifier](../tests/verify-native-mutation-receipts.py) enforces
case completeness, framing/counts/overflow, projection ordering/high-water,
revision-only scans, operation-specific identity/generation rules, rollback,
acknowledgment association, typed error families, memory-before-publication,
shared guards, uncertainty/fresh recovery and allocation scope/FD rules.
[Negative checks](../tests/check-native-mutation-verifier.py) reject 10 tampered
logs: overflow, missing case, counter drift, publication ordering, guard refresh,
late cancellation, duplicate case, staged-result leakage, false acceptance and
missing injection. These checks prove selected receipt consistency, not log
provenance, source/run authentication or independent hashing of omitted bytes.

The collector records both verifier and negative-test exit codes in separate logs
covered by SHA256SUMS. No historical bundle changes. Current local overlay runs
remain regression coverage. Fresh pinned WSL2 ext4 execution with bypass disabled
and explicit scoped mutation acceptance are still required; both historical
accepted revisions remain unchanged. Namespace aliases remain Skipped, Windows
acknowledgment BLOCKED, and application/CLI, NCN and manual validation stay open.
No production behavior, publication ordering, schema, dependencies, version or
roadmap checkbox changes. Counts stay 28 core / 31 audio-enabled; the native
service target has 82 numbered check identifiers plus the 34-case matrix.
