# Catalog persistence contract

**Contract ID:** OHK-STORE-030
**Status:** Accepted by review and merge of PR #54; pure codec, fresh-owner restore and detached fake-provider store protocol implemented; experimental Linux provider and live capture implemented; scoped WSL2 ext4 acceptance recorded; Windows and durable-first wiring pending.
**Target:** 0.3.0; not part of released OpenHDK v0.2.0.

This contract supplements [OHK-LIB-030](KARAOKE-LIBRARY-CONTRACT.md),
[OHK-ROOT-030](ROOT-REATTACHMENT-CONTRACT.md), and
[OHK-META-030](CATALOG-METADATA-POLICY-CONTRACT.md).
[SPECIFICATION.md](SPECIFICATION.md) remains the authority for implemented
behavior. The types and wire fields below are accepted design obligations.
A pure detached projection codec and fresh-owner restore factory are implemented.
A detached store coordinator is implemented with fake-provider tests. The experimental Linux provider and a live checkpoint capture adapter are implemented.
Durable-first mutation wiring remains pending; no new dependency is introduced.

## 1. Storage selection and scope

Use one versioned binary checkpoint for the complete catalog identity and
user metadata projection. The caller explicitly selects its local path outside
song roots. `.ohkcat` is the proposed extension, not proof of valid format.
No automatic home-directory search, legacy database import, SQL execution,
media-file edit, autosave, GUI, CLI grammar, network store, or audio operation
is introduced. No BASS-family or Qt artifact is permitted.

| Option | Decision for this first adapter | Reason |
| --- | --- | --- |
| Bounded binary checkpoint | Adopt | Whole-catalog replacement fits the current immutable model; no third-party engine is needed. |
| SQLite C API | Defer | Queries, indexes and incremental persistent transactions are not required for this first slice; a later engine choice needs pins, license and recovery review. |
| Application-settings dump or legacy DB | Reject | It would mix unrelated runtime settings or import legacy schema assumptions. |

This is an explicit checkpoint boundary. An in-memory mutation is not durable
merely because a store is open. Saving an already-acquired snapshot MUST NOT
mutate, roll back, or silently switch the live catalog. The result identifies
only the captured revision that was saved. Newer live revisions can remain
unsaved. No operation may label them saved on that result's behalf.

OHK-LIB-030's durable mutation rule remains a separate integration obligation:
when persistent mutations are wired, stage the complete change, commit storage,
then publish memory without throwing. A pre-commit store failure MUST discard
that staged mutation. A checkpoint-only adapter MUST NOT claim that scan,
relocation or override mutations are automatically durable. Review this wiring
before the complete persistent library service or v0.3.0 is advertised.

## 2. Persisted projection and identity

Persist opaque numeric RootId/SongId values without changing their existing
library-local meaning. The codec accesses them through a reviewed internal
adapter; it MUST NOT add public path/text-to-ID construction. ID allocation
continues from stored high-water counters, including gaps left by removal.
Never derive an ID from a title, filename, hash, path hint or record order.

The projection contains:

- catalog revision and next-root/next-song counters;
- root IDs, attachment generations and complete immutable SmfKar lyric policy;
- optional last-directory hints for explicit later reattachment;
- song IDs, root IDs, PrimaryMidi relative locators and optional user title/artist.

Do not persist Ready authority, source content tokens, source-derived metadata,
full MIDI/lyric timelines, display strings, playback generations, device handles,
callback state, presets or in-process snapshot-lineage tokens. Source metadata
and filename display fallback are rebuilt under the existing contracts.
Path hints are stored data, not instructions to open a directory. Duplicate
hints do not merge roots. Never probe or attach a hint during decoding.

All IDs are positive and less than UINT64_MAX. Next-ID counters are positive,
strictly greater than every corresponding retained ID, and at most UINT64_MAX.
UINT64_MAX is the existing exhausted-allocation sentinel. Do not recompute the
counters from retained rows: that could reuse a removed ID. Catalog revision is
uint64 and may be zero or exhausted; attachment generations must be positive.
Counters MUST NOT wrap. A checksum is corruption evidence, not source identity,
SongId, signature, encryption, or authentication of an edited checkpoint.

## 3. Schema 1 wire format

All integers are unsigned little-endian with explicit widths. No native struct
layout, padding, enum ordinal, size_t, locale, JSON coercion or compiler ABI is
serialized. Parse field lengths with checked arithmetic before allocation.
Text lengths are byte lengths, without a trailing NUL.

### Fixed header: 64 bytes

| Offset | Width | Field |
| ---: | ---: | --- |
| 0 | 8 | Magic: ASCII `OHKCAT` followed by two zero bytes. |
| 8 | 4 | Schema version: 1. |
| 12 | 4 | Reserved: 0. |
| 16 | 8 | Payload byte count, excluding header and digest. |
| 24 | 8 | Store sequence: positive, initially 1. |
| 32 | 8 | Captured catalog revision. |
| 40 | 8 | Next RootId counter. |
| 48 | 8 | Next SongId counter. |
| 56 | 4 | Root record count. |
| 60 | 4 | Song record count. |

The payload contains roots in increasing numeric RootId order, followed by
songs in increasing numeric SongId order. IDs are not assigned by this order.
The final 32 bytes are SHA-256 over exactly header plus payload, as raw digest
bytes. The complete file length MUST equal 64 + payload length + 32. Reject
truncation, extra records, trailing data or arithmetic overflow. Use the existing
SHA-256 algorithm contract; an incremental codec hash needs partition-invariant
vectors before use. This does not add a cryptographic library or FIPS claim.

### Root record: 32 fixed bytes, then hint bytes

| Relative offset | Width | Field |
| ---: | ---: | --- |
| 0 | 8 | RootId. |
| 8 | 8 | Attachment generation. |
| 16 | 1 | Source mode: 0 = SmfKar. |
| 17 | 1 | Encoding: 0 = UTF-8, 1 = TIS-620. |
| 18 | 1 | Track index present: 0 or 1. |
| 19 | 1 | Directory hint present: 0 or 1. |
| 20 | 8 | Track index; zero when absent. |
| 28 | 4 | Hint UTF-8 byte count; zero when absent. |
| 32 | variable | Hint bytes. |

A present hint is nonempty bounded UTF-8, with no NUL/C0/DEL control bytes.
It preserves the stored spelling; it is not canonicalized or executed by the
codec. Directory hints may be unusable on another platform. Their presence does
not authorize access. Registration/reattachment applies native path rules later.
The live adapter must reject an unrepresentable hint rather than silently drop it.

An explicit track index is valid policy data even if a song lacks that track.
Reject values not representable by the receiving implementation's size_t as
UnsupportedRepresentation; do not truncate them or switch to auto-selection.
Unknown source modes or encodings fail as UnsupportedProfile. Boolean values
other than 0/1 and nonzero absent-index fields are InvalidCheckpoint.

### Song record: 32 fixed bytes, then locator/title/artist bytes

| Relative offset | Width | Field |
| ---: | ---: | --- |
| 0 | 8 | SongId. |
| 8 | 8 | Referenced RootId. |
| 16 | 1 | Member role: 0 = PrimaryMidi. |
| 17 | 3 | Reserved: all zero. |
| 20 | 4 | Locator UTF-8 byte count. |
| 24 | 4 | User title byte count; zero means absent. |
| 28 | 4 | User artist byte count; zero means absent. |
| 32 | variable | Locator, then title, then artist bytes, without separators. |

Locators reuse isCatalogLocator and OHK-LIB-030 byte comparison/collision rules.
Every song must reference a retained root. Reject duplicate IDs, duplicate keys,
ASCII-folded collisions within a root, unknown roles, nonzero reserved bytes,
invalid UTF-8 and invalid override controls. A zero locator length is invalid.
Override length zero encodes absence; nonzero text follows MetadataText's strict
UTF-8/C0 policy. Preserve whitespace, combining sequences and spelling exactly.
There is no artist inference, Unicode normalization or lossy text repair.

Schema 0 and newer schemas are UnsupportedSchema. Do not migrate, down-convert,
truncate, repair, or overwrite them. A future migration requires a separately
reviewed format/backup/rollback contract. Files without this magic, including
HandyKaraoke databases, are InvalidCheckpoint and MUST remain unchanged.

## 4. Bounds and independent fixtures

| Bound | Default maximum |
| --- | ---: |
| Total checkpoint bytes, including header/digest | 64 MiB |
| Roots in a checkpoint | 1,024 |
| Songs in a checkpoint | 100,000 |
| Selected store path and each locator, hint, title or artist | 4,096 UTF-8 bytes |
| Coexisting variable payload during one store/codec operation | 64 MiB |

These are new store bounds. They do not increase the 10,000-candidate per-scan
limit or reduce it to a total catalog limit. Encode/decode rejects an oversized
projection rather than omit rows. Smaller positive configured limits support
boundary tests; increasing these defaults needs contract review.

The operation ledger MUST include retained snapshot strings, owned wire/read
buffers, decoded strings and validation/output copies that coexist. Shared
immutable source/override records count once by object identity. Do not grant a
second metadata allowance. A 64 MiB file ceiling does not promise that every
file at that ceiling fits simultaneous decode staging. Streaming with bounded
chunks may reduce coexistence. Fixed header/hash state and descriptors have
separate record bounds; neither ceiling is a total process-memory guarantee.

The following fixtures are generated independently with Python struct/hashlib,
not with the production encoder. They define the header/record layout and hash
coverage. Expanded strings must also be generated independently for limits.

| Fixture | Payload | Total bytes | SHA-256 of header + payload |
| --- | --- | ---: | --- |
| Empty library: sequence 1, revision 0, next IDs 1/1, counts 0/0 | Empty | 96 | 77e94f5a03f8009e2c10f73af13ad71a4eb1d423bb9d60f410dddb97519ea74b |
| One root: sequence 1, revision 1, next IDs 2/1, counts 1/0 | RootId 1, generation 1, default policy, no index/hint | 128 | 2e65d98593205489fdc86eb76ce38c85b018f5f6c73dcb1688174ea127f2205f |

The root-only payload is `0100000000000000010000000000000000000000000000000000000000000000`.
The empty header is
`4f484b43415400000100000000000000000000000000000001000000000000000000000000000000010000000000000001000000000000000000000000000000`.
The root-only header is
`4f484b43415400000100000000000000200000000000000001000000000000000100000000000000020000000000000001000000000000000100000000000000`.

## 5. Restore and root reattachment

Decode into a detached validated projection first. A successful restore creates
one fresh catalog/discovery owner with a new private in-process lineage token.
Never replace the owner used by an existing PreparedSong/observer in place.
The old owner and its prepared buffers remain unchanged if decoding or factory
construction fails. Full validation precedes returning any restored owner.

Retain stored RootId, SongId, overrides, policy, high-water counters and attachment
generations. The fresh owner's baseline catalog revision equals the captured
revision. This is a new lineage epoch, not another publication in the old owner.
All restored songs initially become Invalid with no current token/metadata.
Roots have no active filesystem binding. Hints remain advisory data. This
projection cannot be prepared or advertised Ready, even when hints still exist.
No startup scan, source read, device start or backend clock binding is implied.

An explicit first reattachment applies OHK-ROOT-030 validation. Since a restored
root has no active binding, attaching even its last hint is a changed binding.
Increment its stored attachment generation and catalog revision once, invalidate
its entries, then require a complete scan before Ready. Same-path Unchanged
applies only after a real active binding exists. Reattachment back to a hint
cannot revive a snapshot captured before that attachment. Counter exhaustion
rejects changed binding; a restored exhausted catalog may still expose history
and display overrides without wrapping counters or enabling preparation.

Old in-process tokens MUST NOT be imported or shared with the restored owner.
Foreign snapshots remain rejected before I/O, even with equal numeric IDs and
bytes. Runtime generations belong to backend lifetimes and cannot establish
persistent song identity. Serialized IDs are library-local, not globally unique
or authenticated. Snapshot copies remain subject to the existing trusted-caller
boundary; this proposal is not a hostile-file or directory sandbox.

## 6. Store ownership and save admission

One serialized, non-reentrant control writer owns a store and its bound library
for the store lifetime. Database/file work MUST stay outside the audio callback.
Readers use already-acquired immutable snapshots. Saving a foreign/unowned
snapshot or a stale attachment/path-map pair MUST fail before writing.

Open only a caller-selected existing local parent directory outside all registered
song roots. Native providers must reject aliases, symlinks/reparse entries,
nonregular targets, hard-linked checkpoint targets and Windows UNC paths.
Retain directory identity/handles through staging. Registration or reattachment
must not place a song root around an active store; integration must enforce this
reciprocal boundary before enabling persistent library operations.

Acquire one nonblocking OS-backed exclusive store lock for reads and writes.
Return Busy rather than spin or wait indefinitely. Use an in-process ownership
guard too: POSIX process locks alone do not distinguish two owners in one process.
The provider must hold and validate a stable regular lock file/handle until close;
do not unlink a live lock file or treat an existing filename as a stale lock.
OS ownership releases on process exit. Locks coordinate participating writers;
noncooperating writers are not made safe by an advisory lock.

Create requires ExpectedAbsent and MUST NOT overwrite an existing file. Updating
requires the last acknowledged StoreCommitToken (sequence plus the final digest
defined in section 3, not another hash including that digest). Under the lock, reread and validate the current checkpoint and compare
the token immediately before publication. Missing, changed, corrupted or newer
schema targets fail closed. A valid create starts sequence 1; an actual update
increments it once. An exact projection no-op returns Unchanged with the same
token, including at sequence exhaustion. Projection equality includes captured revision,
allocator history, generations, policies, hints, locators and overrides. It excludes
only the publication sequence and digest. Do not compare checksums that include
a newly incremented header sequence when deciding that no-op.

The candidate captured revision and ID/attachment high-water values MUST NOT
roll back the acknowledged stored values. ID associations cannot be reassigned.
Compare retained identities with the prior projection; removal may discard rows
but cannot lower allocators. Older acquired snapshots cannot overwrite newer
checkpoints merely because the caller supplies the current token. A newly
introduced ID must be at or above the prior next-ID high water. An existing
SongId may change its source key only through the already-accepted relocation
semantics; this does not authorize ID reuse for a removed row. Retained roots
cannot lose generation history or be dropped: root removal is not in this scope.
An equal captured revision with a different persistent projection is rejected,
not treated as a new save. These checks supplement the trusted-owner boundary;
they do not authenticate caller-modified snapshot copies. Paths for the
snapshot's generation must come from the matching captured registered mapping
or retained restored hint, never from a newer root binding.

## 7. Publication, commit outcome and recovery

1. Validate configuration, lineage, projection, expected token and provider.
2. Create a new exclusive regular temporary file in the retained store directory.
   Do not truncate the target. Bound writes and checksum the complete candidate.
3. Finish and synchronize the temporary file. Close/reopen and fully verify its
   length, schema, content and digest under the same directory identity. For an
   update, also retain a verified, synchronized copy of the acknowledged previous
   checkpoint in an operation-owned recovery artifact before replacing the target.
   Do not create a hard link or overwrite an existing user backup.
4. Recheck cancellation, directory identity and current target/token under lock.
5. Attempt one provider-defined namespace publication. Do not fall back to
   copy-over-target, delete-then-rename, cross-volume copy, or ignored ACL errors.
6. Complete the provider's required synchronization and acknowledge the captured
   revision/new token. Prepare result/recovery-artifact descriptors before
   publication; no allocating result construction may follow this boundary.

Before publication begins, cancellation, validation, allocation, write, flush,
lock or verification failure returns a definite not-committed error. The primary
checkpoint and live snapshot remain unchanged. Cleanup may remove only the
operation's own verified candidate/prior-copy files; never an unowned suffix match, source,
unowned backup, lock file or invalid primary checkpoint.

Keep the previous valid recovery artifact until a confirmed save. If publication
or required synchronization is uncertain, preserve that artifact and the verified
candidate wherever it still exists for explicit reconciliation; do not delete
either during error cleanup.
A confirmed save may clean up only these operation-owned artifacts. Cleanup failure
must not misreport a committed save as rollback; report it separately. One operation
uses the primary plus at most one candidate and one prior copy, each subject to
the file bound. This does not cap space used by abandoned artifacts.

After publication begins, do not honor cancellation as proof of rollback.
An error whose disk outcome cannot be established returns CommitUncertain with
no Saved/Unchanged acknowledgment. The store becomes faulted and refuses writes
until an explicit close/reopen fully validates the primary checkpoint and the
provider completes the required reconciliation synchronization. If it cannot,
remain unwritable. Reopening a visible file alone does not retroactively prove
the failed operation was durable. Retain current live/previous prepared inputs; do not automatically adopt the candidate,
restore a backup, retry the write, or report that the old disk file is unchanged.
Future durable-first mutation wiring must publish memory only after confirmed
commit; uncertainty blocks that mutation and requires reconciliation first.

Reopen reads only the explicitly selected primary checkpoint. It must return a
complete valid old/new projection, NotFound, or a structured corruption/error.
It MUST NOT promote abandoned temporary files, select the highest sequence from
nearby files, scan sources, or perform automatic repair. Manual recovery/export
requires a separate explicit caller action and a fresh restore owner. Checksums
and full semantic validation detect incomplete data; they do not authenticate
external edits or prove freshness against a deliberately restored old file.

## 8. Native provider evidence and honest limits

The [native provider plan](NATIVE-CHECKPOINT-PROVIDER-PLAN.md) proposes API
choices and an evidence matrix for slice 4, accepted by review/merge of PR #58.
A Linux ownership lease and experimental staging/publication provider are
implemented. Acceptance is [recorded](NATIVE-LINUX-CHECKPOINT-EVIDENCE.md) only for revision
6a6337d on WSL2 /dev/sdd ext4. Its Windows synchronization gate remains BLOCKED.

An atomic namespace update and a durable acknowledgment are different claims.
The provider review must identify exact open/lock/publication/flush APIs, handle
sharing, supported local filesystems and classification of every failure phase.
A native API name is not evidence of an atomic whole-filesystem transaction or
universal survival after power loss. Unsupported providers fail before writing.

POSIX provider work must account for both file-data synchronization and directory
entry synchronization; file fsync alone is not a directory publication protocol.
Windows provider work must account for FlushFileBuffers, sharing/ACL failures,
creation versus replacement, and partial replacement errors. In particular,
ReplaceFileW documents an unsupported REPLACEFILE_WRITE_THROUGH flag and failures
that can alter file names. Do not call every false return a rolled-back save.
Provider review/tests must close these points before native durability is claimed.

The first acceptance targets are native Windows/local NTFS and native Linux/local
ext4. A filesystem label alone does not close the evidence gate. Network shares,
cloud-sync directories, WSL cross-OS/DrvFS storage and removable-media guarantees
are not approved by this proposal. This does not restrict song discovery's
separate registered-root policy. Simulated fault tests cover protocol decisions;
subprocess interruption tests cover the tested provider/filesystem. Neither test
proves that every controller, disk cache or power-loss failure is covered.

Platform references used to derive these requirements:

- [POSIX rename](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rename.html)
  and [general crash/synchronization concepts](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/V1_chap04.html).
- [Linux fsync documentation](https://man7.org/linux/man-pages/man2/fsync.2.html)
  and [POSIX fcntl](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fcntl.html).
- [ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew),
  [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers),
  and [LockFileEx](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex).

These references describe platform behavior, not copied implementation or a new
dependency. No Qt/SQLite package, schema migration, binary asset or source port
is approved. Any future dependency still requires DEPENDENCY-POLICY review.

## 9. Errors, implementation slices and acceptance

Structured errors identify domain/code, operation, optional field/record/byte
offset and native error, plus whether the disk outcome is definite or uncertain.
No partial projection, restored owner or success token is returned on failure.
Required categories: InvalidConfiguration, InvalidCheckpoint, UnsupportedSchema,
UnsupportedProfile, UnsupportedRepresentation, LimitExceeded, NotFound, Busy,
ForeignSnapshot, SourceChanged, StaleCheckpoint, InvalidText, Cancelled,
StorageFailure, CounterExhausted, InvalidStorePath, AmbiguousPath,
UnsupportedStorage and CommitUncertain.

Implement in separately reviewed slices:

1. Pure bounded schema/projection codec, canonical wire vectors and hash coverage.
2. Fresh-owner restore and unattached-root/ID/counter/override lifecycle rules.
3. Store ownership and fake-provider save/uncertainty protocol tests.
4. Native providers with Windows/Linux file, lock, interruption and recovery tests.
5. Durable-first library/controller wiring, dirty-versus-saved revision handling,
   application operations and manual Windows validation under a reviewed contract.

Minimum independent acceptance fixtures:

- exact schema vectors, numeric byte order, canonical ordering and hash scope;
- round trip IDs, allocator gaps, generations, policies and exact overrides;
  no source authority, Ready state, display cache, or runtime token imported;
- duplicates, foreign references, collision aliases, malformed UTF-8/C0,
  boolean/reserved fields, absent-field padding, truncation and trailing data;
- older/newer schema rejection without file mutation; checksum tampering;
  unknown profiles and unrepresentable track indices with no fallback;
- per-field, file/count and total coexistence limits at/beyond boundaries;
  checked arithmetic and deterministic allocation failure with no partial output;
- fresh lineage, foreign-old-snapshot rejection, unattached root admission,
  explicit same-hint attachment increments, rescan ID/override preservation,
  exhausted counters and retained old PreparedSong/display/batch lifetimes;
- create-if-absent, same/different-process lock contention, stale token, no-op,
  counter exhaustion and monotonic identity/allocator/captured-revision admission;
- failures before/after publication, cancellation ordering, flush/ACL errors,
  path substitution, hard-link/reparse rejection, disk outcome uncertainty,
  faulted-store write rejection and explicit reopen reconciliation;
- ignored abandoned/unowned temps and invalid primary files; no auto-repair;
  subprocess interruption shows only fully verified results or explicit errors;
- later durable-first mutation failure retains old memory; confirmed commit
  precedes one memory publication; newer unsaved live revision remains dirty.

Hardware-free regressions and both CI workflows must pass on exact heads.
Windows-only native behavior requires Windows tests. The current development
baseline is 24 core / 27 audio-enabled suites. Codec, restore and detached protocol have separate
hardware-free suites;
the full storage milestone checkbox remains open. NCN evidence, application/audio/CLI orchestration,
manual device/lyric validation and full v0.3.0 release acceptance remain open.


## 10. Current pure codec boundary

`library/CatalogCheckpointCodec.hpp` implements schema 1 with detached
`CatalogCheckpointProjection`, `CheckpointRoot` and `CheckpointSong` values.
Numeric projection IDs cannot construct live RootId/SongId. No snapshot adapter,
restore owner, filesystem access, save token, lock, recovery or durability claim
is introduced. Unknown profiles remain rejected; NCN evidence is unchanged.

`encodeCatalogCheckpoint` validates the complete projection and emits roots and
songs in ascending ID order without changing its input. `decodeCatalogCheckpoint`
requires that canonical order and validates digest, lengths, padding, identities,
references, aliases and text before returning a complete owning projection.
The sequence is wire data only here; the future store must enforce save admission
and monotonic history. Neither operation imports source authority or runtime
lineage. A checksum remains corruption evidence, not authentication.

Both operations accept lower positive `CatalogCheckpointLimits` and an optional
`alreadyOwned` byte charge for other caller-retained payload. Encode charges all
projection strings plus the complete output wire. Decode charges the entire
borrowed input wire plus all decoded strings. Both reserve twice the largest
field's logical byte length for canonical UTF-8 validation temporaries. The
allowance includes these values together, not independent 64 MiB budgets.
Caller payload outside these explicit inputs must be supplied as `alreadyOwned`;
future snapshot adapters must deduplicate shared records by identity. Fixed
record/view/map descriptors use the root/song count bounds; allocation capacity
and allocator overhead are not a total process-memory ceiling.

Validation uses the existing strict UTF-8 decoder/C0 policy, locator rules and
SHA-256 algorithm. Hint validation additionally rejects tab/CR/LF/DEL and never
opens or normalizes a path. Override whitespace and scalar sequences remain
exact. Allocations are control-path-only; bad_alloc maps to StorageFailure and
never returns partial output. Codec errors identify Encode/Decode, field, optional
record and byte offset. Text offsets are relative to that field's payload;
wire structural offsets are absolute in the input buffer. Native outcome/error
fields belong to later store operations, which this slice does not implement.

Independent Python struct/hashlib fixtures define empty, root-only and rich
Thai/combining-text wire bytes, including allocator gaps, duplicate advisory
hints and an exhausted generation. Tests also cover malformed wire/projections,
canonicalization, checksum coverage, exact/beyond bounds, retained input charges
and allocation-failure sweeps. Fresh-owner restore is implemented in section 11. Later slices still require
native evidence and durable-first wiring.


## 11. Current fresh-owner restore boundary

`library/CatalogCheckpointRestore.hpp` implements slice 2.
`CatalogCheckpointRestore::fromProjection` validates a complete detached
projection again, including caller-built values, before returning a unique
SongDiscovery owner. `fromBytes` treats its input only as wire data and uses the
existing decoder; no checkpoint file is opened. No existing owner is passed,
replaced or mutated. Decode errors keep their Decode operation/offset; factory
errors identify Restore. Failed validation or allocation returns no owner.

Opaque ID construction stays in a private SongCatalog restore member, reachable
only through the validated adapter. The new owner receives its own private
lineage token. It retains stored IDs, high-water counters, catalog revision,
root generations/policies and exact user overrides. All songs are Invalid with
no source token or metadata. No prepared song, lyric binding, Ready authority,
audio operation or in-process token is imported. Public numeric-ID construction
remains unavailable. Serialized identity remains library-local, not authenticated.

Every restored root is represented with an empty inactive native path and an
optional owned saved hint string. `rootAttachment` returns an owning attached
flag/hint query without I/O; query allocation exceptions propagate. Saved hints
remain historical data after attachment and are not a current-path authority.
Future checkpoint capture must use the matching captured binding/generation,
not substitute such a saved hint for an active path. This slice does not export
live snapshots to checkpoint projections.

Scan and preparation reject unattached roots before their filesystem/control
hooks. Registration and overlap checks ignore inactive hints. The first explicit
reattachment, even to the saved hint's existing directory, validates the target,
retains/rechecks native directory identity, and increments generation/revision
once. Only after all checks and allocations succeed do nonthrowing path/catalog
swaps and the attached flag publish on the serialized control path. Failure
leaves the root unattached. Existing active same-path no-op semantics remain.
A complete scan then revalidates source bytes/lyrics and preserves IDs/overrides;
pre-attachment snapshots cannot prepare after the generation change. Other roots
remain independent. Duplicate hints are legal saved data; attaching two roots to
actual overlapping directories still fails under OHK-ROOT-030.

High-water gaps are retained, never recalculated. Exhausted history can be
restored, queried and displayed; changed identity/revision/attachment operations
reject without wrap. Existing equal-override no-ops remain valid at revision
exhaustion. Discovery maps exhausted ID allocation during registration/scan to
RevisionExhausted, not an allocation/storage failure.

Factory staging charges retained projection strings, new owner strings and
three largest-field logical copies for validation/MetadataText coexistence.
`fromBytes` additionally retains the full wire charge across decode and restore.
The optional alreadyOwned charge covers other caller-retained payload; no second
allowance is granted. Count-bounded descriptors and allocator overhead remain
separate from logical bytes. Imported saved hints are also charged once in later
scan, preparation, override, display and root-staging operations; mutations do not
copy their registry strings. Fresh hints and all metadata allocations remain on
the serialized control path. This is not an audio callback operation.

The hardware-free restore suite exercises independent schema fixtures, existing
and missing hints, first attach/rescan, stale/foreign snapshots, allocator gaps,
exhausted history, exact/beyond coexistence bounds, retained payload charges and
allocation/cancellation failure. Historical snapshots, prepared songs, display
values, query strings and lyric batches continue to own their original data.
Native save/lock/recovery providers, durable-first wiring, NCN evidence,
application orchestration and manual release acceptance remain open.

## 12. Current detached store protocol boundary

`library/CatalogCheckpointStore.hpp` implements a serialized, nonblocking
coordinator over an abstract `CheckpointStoreProvider`. The hardware-free
`catalog-checkpoint-store` suite supplies an in-memory namespace provider only.
There is no shipped filesystem provider or evidence of native lock, rename,
directory synchronization, crash recovery or power-loss durability.

Open acquires the provider lease, validates the primary if present, and requires
provider reconciliation before enabling writes. Missing primary is an explicit
ExpectedAbsent ticket. Tickets retain a private store-object identity; foreign
store tickets and stale sequence/digest expectations fail before staging.
This identity is NOT SongDiscovery snapshot provenance. Live capture now acquires matching owner context; durable-first mutations remain
a separate slice 5 integration obligation.
A provider must outlive its coordinator. Calls and queries use a serialized
control path; recursive operations return Busy without releasing its lease.

Save validates the complete detached projection and retained history, compares
the current primary token under the lease, and stages canonical candidate bytes
with the next sequence. Exact projection no-ops exclude only the publication
sequence and remain valid at sequence exhaustion. Changed projections require a
newer catalog revision, nondecreasing allocator history, all prior roots, immutable
root policy and monotonic generations. Changed hints require a newer generation;
new IDs cannot reuse historical gaps. Song removals and explicit relocations are
permitted. Schema/digest failures do not authorize repair or fallback.

Candidate and prior-copy artifacts are exclusively created, written, synchronized
and byte-verified before a final primary/token and directory identity recheck.
The provider owns native capability validation and must never treat cleanup as
permission to delete the primary or an unowned file. Cancellation is checked only
before publication. The result is constructed before publication, and provider
publication, synchronization, reconciliation and cleanup are nonthrowing and
allocation-free. Definite precommit failures clean owned stages. Published plus
confirmed synchronization acknowledges only the captured revision; cleanup failure
is a separate warning. Cleanup does not revoke an acknowledged save.

Indeterminate publication or post-publication synchronization returns
CommitUncertain, no success token, retained artifact capabilities and a faulted
store. No retry/save is admitted until close and validated reopen/reconciliation.
Close releases the lease, preserving the fault latch and artifacts. Reopen never
chooses an artifact or prior copy as primary; missing/corrupt primary after an
uncertain save cannot silently create a fresh checkpoint.

One coexistence ledger charges caller-retained payload, input strings, current
wire, decoded current strings, candidate projection/wire, and verification reads
before allocation/growth. Caller alreadyOwned must include separately retained
open-result or other payload. Count-bounded descriptors/allocator overhead are
separate; provider backing storage is not a process-memory allowance. Provider
reads receive the remaining bound and must reject before allocating beyond it.
This remains logical payload accounting, not a whole-process memory ceiling.

Tests cover stage fault injection, byte verification, cancellation, directory
replacement, external primary edits, nonblocking ownership/reentry, stale/foreign
tickets, no-op/exhaustion, history rejection, uncertainty with old/new primary,
explicit reconciliation, cleanup warnings, exact coexistence bounds and injected
allocation failures. A publish-time allocation prohibition tests the no-allocation
acknowledgment path. These tests prove coordinator behavior, not OS semantics.
Scoped WSL2 ext4 provider acceptance and live capture are recorded below. NCN
evidence, Windows provider acceptance, durable-first wiring, application
orchestration and manual Windows acceptance remain open. No storage roadmap
checkbox is completed by this slice.

## Experimental Linux provider boundary

`LinuxCheckpointProvider` implements the detached provider interface with two
fixed artifact slots and lease-bound process-wide, non-reused capability IDs.
Paths/names use the accepted lease limits. The provider additionally reserves
the .ohk-stage- primary-name prefix before I/O so stages cannot become another
participating store's selected primary. Artifacts have 96-byte fixed name
buffers, at most 32 exclusive-create attempts, and a 64 MiB wire ceiling. Provider
retained strings plus both fixed name buffers are charged by retainedBytes into
the coordinator's existing allowance; this is not a second payload budget.
Native descriptors, allocator/map overhead and stack scratch are not a process
memory ceiling. At most eight native descriptors coexist during checks/probes.

Acquire probes no-replace rename using only exclusively created artifact entries,
then synchronizes/cleans those entries. It never probes by replacing primary.
Staging uses bounded positional I/O, short-I/O/EINTR handling, artifact fsync and
parent fsync; verification reopens the owned artifact. Update rechecks the last
read primary's native identity. Create uses RENAME_NOREPLACE with no fallback.
Known create EEXIST is NotCommitted/StaleCheckpoint; unknown rename returns are
Uncertain. Post-rename parent sync failure is Uncertain. Reconcile synchronizes
the validated primary and parent, never promoting an artifact.

Cleanup of a consumed candidate closes its handle and never unlinks primary.
Identity-mismatched/unconfirmed entries are preserved with an error/warning.
Failed cleanup can occupy a slot until explicit close/reopen; release preserves
abandoned entries and never scans suffixes. These are trusted-directory checks,
not hostile-writer isolation. The scoped Linux acceptance is recorded separately.
Windows synchronization and durable-first mutations remain pending; live capture
is provided by the separate adapter below.

## Current live checkpoint capture boundary

`CatalogCheckpointCapture::acquire(owner, limits, alreadyOwned)` acquires the
current owner's snapshot, allocator high-water counters and matching registered
root mapping in one serialized control-path operation. It returns an owning
`CapturedCatalogCheckpoint` with const projection/context access. It accepts
no arbitrary snapshot argument, constructs no public RootId/SongId and performs
no filesystem probe/read/hash, callback or audio work. Concurrent owner access
is not supported; the caller must serialize acquisition with owner mutations.

Attached roots copy the retained canonical path as a UTF-8 advisory hint.
Unattached restored roots preserve their saved hint or absence. Root policy and
attachment generation come from the same acquired snapshot. Songs copy only
persistent identity/member locator and exact user override bytes. Ready/state,
source tokens/metadata, compiled timelines and runtime lineage are not encoded.
The acquired context owns the snapshot but does not import its token into wire.

The projection retains the exact current counters, including removed-ID gaps
and exhaustion. Its sequence 1 is a codec-valid placeholder; the store controls
actual publication sequence admission. Later edits, rescans, reattachment or
owner destruction do not rewrite a captured projection. Capture does not accept
an old snapshot and combine it with newer mappings/counters. Saving a captured
projection remains detached and must obey the store's stale/history checks.

Preflight validates limits/counts before projection copies. The shared logical
ledger charges caller-owned bytes, retained snapshot locators/source metadata/
user overrides (shared records once by identity), registered native path/saved
hint bytes, per-row output text and two largest-field temporaries. Windows
reserves three UTF-8 bytes per UTF-16 hint unit before conversion. This is a
conservative bound: it can reject a text/byte boundary even if the eventual
UTF-8 hint would fit. Fixed descriptors are count-bounded; allocator overhead
is not a process-memory guarantee. Complete codec shape/semantic checks enforce
wire capacity without producing a wire buffer.

Failures return no acquired value and preserve the live owner. Errors identify
CheckpointOperation::Capture; allocation failure is StorageFailure and native
path conversion failure is UnsupportedRepresentation/Hint. Historical acquired
values remain usable. The adapter is an internal numeric-ID export boundary,
not authentication of detached values that a caller later copies/modifies.

This closes live capture only. It does not make scans/overrides/reattachment
durable, write a checkpoint, publish a staged mutation or reconcile a faulted
store. Commit-before-memory-publication, scoped native revalidation for provider/
test changes, Windows synchronization, NCN/application integration and manual
release validation remain separate gates. The storage checkbox stays open.

## Proposed durable service integration

[OHK-DURABLE-030](DURABLE-LIBRARY-SERVICE-CONTRACT.md) proposes the exclusive
service and acknowledged baseline needed for slice 5. Its first planned mutation
is user override replacement. It requires private staging, confirmed storage
commit and a non-throwing memory swap. Uncertainty or baseline mismatch blocks
writes and requires explicit recovery into a fresh owner. No mutable owner/store
or arbitrary caller token is exposed through the planned service.

The native provider interface does not yet supply the retained directory binding
capability needed for service/root admission. Fake-provider tests and native
factory/containment evidence are separate implementation slices. The proposal
changes no implemented API, checkpoint schema, provider acceptance or roadmap
completion state. Other persistent mutations require their own staging work.
