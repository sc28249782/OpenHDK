# Root reattachment contract

**Contract ID:** OHK-ROOT-030
**Status:** Accepted planning contract (PR #48); local SMF/KAR reattachment implemented in the development tree.
**Target:** 0.3.0; not part of released OpenHDK v0.2.0.

This contract supplements [OHK-LIB-030](KARAOKE-LIBRARY-CONTRACT.md).
[SPECIFICATION.md](SPECIFICATION.md) remains the authority for implemented
behavior. MUST and MUST NOT below define the target implementation obligations.
Development-tree support does not extend the released v0.2.0 baseline.

## 1. Scope

Reattachment changes the absolute directory associated with an existing RootId
only after an explicit caller request. It MUST preserve RootId, SongId, and
relative locators. It MUST NOT move, rename, edit, or delete source files,
register a replacement RootId, infer a directory from matching titles/content,
or start, stop, or replace audio. There is no new CLI operation in this slice.

The initial implementation covers the current local SMF/KAR discovery boundary.
It MUST preserve the root's source and lyric policies when those fields are
implemented. Policy changes, NCN bundles, persistence/recovery, and library
import remain separate reviewed work.

## 2. Root attachment generation

Each registered root MUST have a positive uint64 attachment generation,
initially 1. An immutable catalog snapshot MUST retain the attachment generation
for each root represented in that snapshot. This value identifies the directory
binding, not song content, playback generation, or catalog revision.

A changed directory binding MUST increment that root's attachment generation
once and publish one new catalog revision. Neither counter may wrap. An
exhausted attachment generation or catalog revision MUST reject a changed binding
without changing any state. Rejected requests MUST NOT consume a generation.
The generation MUST NOT be reset when returning to a previously used directory.

Other roots' attachment generations MUST remain unchanged. Existing private
catalog-lineage tokens retain their meaning; attachment generations add a check
within the same library and do not establish cross-library identity. A future
storage adapter requires its own reviewed restore rules; no token or database
format is selected here.

## 3. Directory validation and no-op requests

The control path MUST resolve the explicitly supplied existing directory using
the current registration rules: canonical local directory, no Windows network
root, and no overlap or equivalent-path alias with another registered root.
Other registrations reserve their canonical locations even if temporarily
missing. Directory/equivalence probe failures MUST return a structured error,
not silently waive ambiguity checks. Do not require the old directory to exist.

Canonical selection does not change source-read policy. Discovery still skips
symlinks/reparse entries and preparation uses the existing native no-follow
reader and containment checks. Reattachment does not add network access or
relax those rules.

After validating the root ID and target directory, a target with exactly the
same canonical path as the current binding MUST return an explicit Unchanged
success. It MUST NOT publish a revision, increment a generation, invalidate
songs, or scan sources. A distinct path that is an equivalent alias of the
current root MUST be rejected as AmbiguousPath if that equivalence can be
observed. A missing old directory does not prevent a genuinely new binding.
A no-op is not evidence that source bytes are unchanged.

## 4. Transaction and catalog state

The operation runs on the same serialized, non-reentrant control path as scan
and preparation. A callback flag is not synchronization. Readers may share
already-acquired immutable snapshots; no filesystem work runs in audio code.

For a changed binding, stage the directory mapping, incremented attachment
generation, and complete catalog changes before publication. Every existing
entry under that RootId MUST retain its identity/locator and become Invalid,
with all source revision tokens cleared. Current source-derived validation,
metadata, and lyric results MUST be invalidated when those fields are added;
explicit user overrides MUST be preserved. Missing and UnsupportedProfile
entries also become Invalid pending a complete scan of the new directory.
Entries under other roots MUST remain unchanged.

Reattachment validates a directory binding, not songs. It MUST NOT assign new
SongIds or mark entries Ready. A later successful complete scan uses normal
reconciliation: unchanged keys retain IDs, absent sources become Missing, new
keys get new IDs, and valid sources become Ready with new content tokens.

Check cancellation before validation work and immediately before commit.
Record the target's canonical path and native directory identity during
validation (POSIX device/inode or Windows volume/file ID), then verify both
again immediately before commit. A changed
or disappeared target, cancellation, allocation failure, or validation error
MUST leave the old directory binding, generation, catalog revision, and snapshot
unchanged. Stage all potentially throwing allocations before a non-throwing
commit; a result MUST NOT expose half of a new binding. Internal test hooks may
inject changes at staging boundaries but MUST NOT re-enter library methods.

This validation is a consistency check, not an atomic filesystem transaction
or a guarantee against ongoing external edits. Later scan and prepare calls
still revalidate containment and exact source revisions.

## 5. Preparation and existing playback inputs

Preparation MUST first enforce the existing snapshot-lineage gate. Before any
source I/O, it MUST compare the selected root's captured attachment generation
with the current binding. A missing or mismatched generation MUST return
SourceChanged at Resolve, with no PreparedSong and no change to current audio.

This rule applies even if the new directory has the same locator and identical
source bytes. Reattaching back to the original path MUST NOT make an older
snapshot eligible again. A rescan alone does not change attachment generation:
older same-library snapshots remain eligible if their source tokens still match.
A reattachment of another root MUST NOT invalidate preparation for this root.

Already-created PreparedSong objects and their owning lyric batches MUST retain
their original catalog snapshot, MIDI, lyrics, options, and source identity.
They remain usable after reattachment or later scans. Reattachment MUST NOT
stop an observer or backend: application playback replacement remains a separate
controller action with the existing clear-before-replacement requirements.

## 6. Results and acceptance

Return an explicit Updated or Unchanged success, or a structured error with the
reattachment operation and a safe diagnostic. Required categories are
InvalidRoot (unknown root or invalid directory), AmbiguousPath, SourceChanged,
Cancelled, StorageFailure, and RevisionExhausted. Failure publishes no snapshot.
No partial scan or prepared song is returned by this operation.

Minimum independently authored tests:

- move a synthetic root directory; old location missing; reattach and rescan;
  retain RootId/SongId/locator, increment attachment/catalog revisions once;
- invalidate Ready/Invalid/Missing/UnsupportedProfile entries and clear tokens;
  complete rescan restores Ready or Missing with stable IDs;
- validated same-path no-op preserves snapshot identity, states and counters;
- unknown root, absent/non-directory target, other-root overlap/equivalence,
  observable self-alias, and Windows network-root rejection;
- cancellation before work and before commit, target mutation/disappearance,
  and deterministic allocation/transaction failure: old mapping still works;
- attachment/catalog counter exhaustion rejects a changed binding without
  wrapping or publishing; a validated same-path no-op remains Unchanged;
- identical bytes at old/new locations do not admit a stale snapshot; returning
  to the old directory still rejects it; rescan-only old snapshots still work;
- another root's snapshots still prepare; foreign lineage still rejects first;
- PreparedSong and retained lyric batch survive reattachment and owner teardown;
- source symlink/reparse escape remains rejected by the existing reader.

Hardware-free fixtures and both CI jobs are required for the implementation
slice; Windows-only path behavior needs Windows coverage. NCN evidence,
persistence, metadata/indexing, application/CLI orchestration, and manual
device/lyric validation remain open gates before a v0.3.0 support claim.

## 7. Current implementation boundary

`SongDiscovery::reattachRoot` implements Updated/Unchanged results and structured
ReattachRoot diagnostics. Catalog snapshots retain RootId/attachment-generation
records. Registration publishes a root record and one new catalog revision;
complete scans retain attachment generations. A changed binding stages one
invalidated snapshot and swaps the selected path and catalog pointer without
allocation or callbacks during commit.

`DirectoryHandle` retains native directory identity through staging and final
verification. POSIX uses device/inode; Windows uses volume serial/file index.
The Windows handle permits read/write/delete sharing so external replacement
can be observed rather than blocked by the observation handle. Source reads
still use their separate no-follow containment reader. Neither probe is a
filesystem transaction or a guarantee against later external edits.

Preparation enforces catalog lineage first and then rejects missing/mismatched
root attachment generations at Resolve. PreparedSong buffers and owning lyric
batches retain their prior snapshots. This does not authenticate caller-modified
snapshot copies or individual library-local IDs.

The standalone `root-reattachment` suite exposes private counter/catalog seams
only through its target-local OPENHDK_ENABLE_TEST_SEAMS definition. Production
has no counter reset or test descriptor injection API. Checkpoint hooks inject
cancellation, directory replacement/disappearance, and bad_alloc exceptions
before commit; these do not claim exhaustive allocator fault coverage. Native
source-symlink coverage runs where the OS permits fixture creation. Observable
self-alias coverage depends on a filesystem exposing distinct equivalent
canonical paths; the implementation rejects such equivalence when detected.
Source/member metadata, root lyric policies, NCN, persistence, and CLI/audio
orchestration remain separate work.
