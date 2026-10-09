# Catalog metadata and root lyric policy contract

**Contract ID:** OHK-META-030
**Status:** Accepted planning contract (PR #50); model, scan/preparation, and override/display transactions implemented.
**Target:** 0.3.0; not part of released OpenHDK v0.2.0.

This contract supplements [OHK-LIB-030](KARAOKE-LIBRARY-CONTRACT.md),
[OHK-LYR-030](LYRIC-TIMELINE-CONTRACT.md), and
[OHK-ROOT-030](ROOT-REATTACHMENT-CONTRACT.md).
[SPECIFICATION.md](SPECIFICATION.md) remains the authority for implemented
behavior. Obligations below describe the accepted target. Discovery validates canonical SMF and selected lyrics under its immutable root
policy. Preparation inherits that policy or takes a complete one-call override.
Source/member metadata, user overrides, and display resolution are integrated.

## 1. Scope and data separation

The catalog MUST distinguish source identity/member descriptors, source-derived
metadata, explicit user overrides, and display fallback. None of these text
fields may determine SongId, source keys, scan order, or source equality.
The pure model MUST contain no filesystem handle, database, UI, or synth type.

This slice covers one primary canonical SMF source and the current KAR selection
profiles. It adds no NCN member resolution, artist-tag convention, playlist,
search/full-text index, cached cue timeline, GUI, CLI flag, or database format.
It MUST NOT read legacy databases or write title/artist data into media files.

## 2. Root lyric policy

Registration MUST select source mode and a complete lyric policy. The initial
supported mode is SmfKar; a policy contains UTF-8 or TIS-620 encoding and an
optional zero-based track index. Defaults are SmfKar, UTF-8, and automatic track
selection under OHK-LYR-030. A .kar extension does not change those defaults.
Unknown modes/encodings MUST fail registration as InvalidConfiguration before
filesystem access, RootId allocation, or catalog publication. No NCN mode is
approved until its separate evidence and implementation gates close.

A selected index is not proof that every song has that track. Per-file
InvalidLyricTrack is diagnosed during extraction. Do not treat a policy error
as permission to guess encoding, retry another track, or merge lyric tracks.
Source/lyric limits remain separately configured under the existing contracts;
a policy MUST NOT increase or bypass them.

The policy MUST be retained in immutable root records and in each validated
song's metadata context. In this initial slice it is immutable for that RootId.
There is no policy-update operation. Reattachment MUST preserve it; rescans
MUST reuse it. Changing policies on an existing root requires a separate
reviewed invalidation/stale-snapshot contract. There is no new policy counter.

## 3. Source/member descriptors and compact lyric summary

A discovered candidate MUST have one PrimaryMidi member whose UTF-8 relative
locator equals its source key. The member has an optional SHA-256/byte-count
revision: present only if those bytes were successfully read. Retain the token
for diagnostic-only invalid sources when available; clear it for Missing or
unreadable sources. Reattachment clears it under OHK-ROOT-030.

Verified source kind is CanonicalSmf only after canonical parse/compile succeeds;
otherwise it is Unverified. Filename extensions remain hints. Lyric profile is
separate: NoLyrics, LyricMeta (FF05), or NamedText (identified FF01). A .mid may
contain lyrics; a .kar may have NoLyrics. Do not infer NCN or add LYR/CUR members
from matching filenames.

The PrimaryMidi member is the authoritative content token. If a compatibility
sourceRevision accessor remains, it MUST derive from that member. Do not keep
two independently mutable copies that can disagree.

Successful extraction produces a compact summary: selected profile, optional
selected track, cue count, and effective lyric policy. NoLyrics has no selected
track and zero cues. Available lyrics include action-only cues. The summary
MUST NOT retain or copy complete raw tags, cues, decoded lyric payloads, or a
KarLyricTimeline into every catalog entry. Preparation still builds owning
immutable timelines. This summary is not a persistent cue/search index.

## 4. Source title, artist, and provenance

Reuse the canonical compiler and current KAR extractor. A source title is only
the extractor's first nonempty @T value in its selected NamedText track. Preserve
its decoded UTF-8 bytes exactly, including spaces. Retain provenance identifying
the primary member, exact source revision, effective policy, and source event
position. The compact catalog need not duplicate the raw tag; the prepared
lyric timeline already retains it.

FF05 profile selection MUST NOT start reading title metadata from an unselected
FF01 track. NoLyrics and @T-only unnamed/nonqualifying tracks MUST NOT acquire a
title through a second heuristic pass. Additional @T values remain retained by
the extractor but are not classified as artist. Source artist is absent in this
slice. Do not split a title on slash, hyphen, comma, or inferred language tags.
Track names, MIDI program names, folder names and file stems are not source titles.

A complete rescan MUST replace source-derived fields from the newly verified
bytes and root policy. Invalid, Missing, UnsupportedProfile, and unreadable
entries MUST NOT expose an old title or lyric summary as current source data.
An immutable older snapshot or PreparedSong may retain its historical values.

## 5. User overrides and display resolution

User title/artist overrides are separate optional UTF-8 fields. An absent field
means use the source field when present. A present override MUST be nonempty;
empty text is rejected rather than treated as clear. Clearing an override uses
an explicit absent value. Whitespace is preserved, not trimmed or normalized.
Validate UTF-8 and the existing C0 policy (only tab/CR/LF are permitted controls).
This defines stored text, not a rendering/escaping policy or a tab width.

A request replaces the complete title/artist override record atomically on the
serialized control path. Validate fields and bounds before publication. A valid
change publishes one catalog revision; an identical record is an explicit no-op.
Unknown SongId, invalid text, allocation failure, limits, or revision exhaustion
MUST preserve the old snapshot. Override changes MUST NOT edit source members,
change attachment generation, or alter lyric extraction. Rescan, relocation,
and reattachment MUST preserve these overrides. Catalog removal discards them
with that entry; rediscovery after removal creates a new SongId without them.

Display title resolves in this order: user title, source title, filename fallback.
Display artist resolves user artist, then source artist; otherwise it is absent.
Fallback is the full primary filename stem, removing only the last extension
and preserving UTF-8 spelling. For example, dir/Live.Set.kar yields Live.Set.
It has FilenameFallback provenance and MUST NOT populate sourceTitle or SongId.
An override remains displayable on an Invalid/Missing entry; it does not make
the source Ready or playable.

## 6. Scan and preparation boundary

A metadata-enabled scan MUST parse, compile, and extract under the registered
root policy before publishing Ready. Successful NoLyrics is Ready. A selected
track/text failure makes the entry Invalid with a nested KarLyricError and no
current source title/lyric summary; other valid candidates may still commit.
Existing parser/compiler errors retain their codes and positions. This changes
future library Ready from the current music-only validation boundary; it does
not change standalone SMF diagnostics or playback CLI behavior.

Limit exhaustion, cancellation, source changes, enumeration failure, or staging
allocation failure MUST discard the entire staged scan, including metadata.
Recheck exact member revisions before publication as in current discovery.
Old source-derived metadata MUST NOT survive a complete invalidation. Historical
snapshots and explicit overrides retain their separate ownership.

Preparation with no explicit lyric override MUST inherit the selected snapshot's
root policy. Validate snapshot lineage and root attachment generation first.
An explicit override replaces the complete encoding/track policy for that one
preparation. Reject an unknown override encoding as InvalidConfiguration before
source I/O; never retry the root encoding after override failure. It MUST NOT mutate the root, catalog metadata, or overrides.
Reopen/revalidate the member and perform extraction under the effective policy;
do not reuse catalog lyric summary/title as proof for a different selection.

Ready under the root policy remains the admission gate. A one-call override
cannot rescue an Invalid entry or bypass source revision/containment checks.
A Ready entry can still fail preparation under its override. To support recovery
of an invalid root policy later, review a policy-update operation separately.

PreparedSong MUST retain effective policy and freshly extracted source metadata
alongside its original catalog snapshot and timelines. Catalog source metadata
reflects the root policy; prepared source metadata reflects the effective
preparation policy. APIs MUST distinguish those contexts. Display resolution
uses prepared source metadata and the acquired snapshot's user overrides.
Already-prepared results MUST NOT change when later override/catalog requests
publish new snapshots. No metadata operation starts or replaces audio.

## 7. Bounds, errors, and acceptance

Title, artist override, and fallback fields each have a 4,096-byte UTF-8 ceiling.
Existing smaller configured limits remain valid; reject before retained append
where possible. Source titles use the extractor's existing title bound. Charge
all owned member locators, source titles, override fields and materialized
fallback/display strings to the existing 64 MiB staged catalog budget, including
copies that coexist during staging. Shared immutable storage is counted once;
fixed-size descriptors remain subject to existing candidate/record bounds.
Do not grant a second 64 MiB budget for metadata. Lyric extraction temporaries
that coexist with staged catalog data use the remaining variable-payload budget.
Canonical SMF parse storage retains its separate existing bound. Use checked
arithmetic.

Required structured categories include InvalidConfiguration, NotFound,
InvalidText, InvalidLyricTrack, LimitExceeded, SourceChanged, Cancelled,
StorageFailure, and RevisionExhausted. Preserve nested parser/compiler/KAR errors
and source positions. Define concrete API types in the implementation review;
this contract adds no numeric error-code serialization or public CLI grammar.

Minimum independent fixtures:

- default UTF-8/automatic policy, explicit TIS-620/index, and invalid registration
  policy rejected without filesystem work or RootId/revision consumption;
- policy survives scan and reattachment; no-call override inherits it; complete
  one-call override leaves root/catalog unchanged and rebuilds selected metadata;
- FF05 preference, NamedText title provenance, NoLyrics, @T-only selection,
  multiple @T values, whitespace, and no title/artist splitting or track-name guess;
- one member/token authority, readable-invalid token, Missing/unreadable clearing,
  same-size/time replacement and source changes during staged metadata extraction;
- selected-text failure is Invalid with nested position; another song still Ready;
  successful NoLyrics is Ready; limits/cancellation roll back the entire scan;
- user set/clear/no-op, atomic title+artist replacement, rescan/relocation/
  reattachment preservation, removal/rediscovery, and snapshot lifetime;
- per-field and cumulative budgets at/beyond boundaries, checked arithmetic,
  allocation/revision failure and no partially published metadata;
- prepared metadata differs from catalog when selection is overridden, retaining
  exact source provenance and old user overrides after later mutations.

PR #50 accepted this contract. The development tree implements scan/preparation integration and
override/display transactions in reviewed slices. Hardware-free tests and both CI
jobs are required; NCN evidence, storage, application/CLI orchestration and manual
Windows device/lyric validation remain separate gates. No released behavior changes are authorized by the implementation boundary below.


## 8. Current pure model boundary

`library/CatalogMetadata.hpp` implements policy values and validators, a compact
lyric summary validator, primary-member/source-title provenance records,
separate optional user override fields, nonempty bounded UTF-8 `MetadataText`,
and a checked `MetadataPayloadBudget` ledger. Policies contain no limits;
`karOptionsForPolicy` copies caller-provided extraction limits unchanged.
Track existence is checked by extraction, not by the policy validator.

`MetadataText::create` validates configuration and the byte ceiling before
allocating. It reuses the strict UTF-8 decoder and C0 policy; empty input fails,
spaces/BOM/combining bytes are preserved, and invalid input has no partial value.
Allocation exceptions propagate. The decoder result and retained text copy
coexist temporarily; integration must charge both, as well as its other owned
copies. The ledger consumes a caller's remaining existing catalog allowance;
it does not allocate a second budget or automatically inspect object graphs.
A failed charge leaves the ledger unchanged and cannot overflow.

Pure descriptors remain control-path values, not independent proof of successful
extraction. SongDiscovery constructs catalog records from validated locators,
canonical compilation and the selected lyric extraction described in section 9.
There is no source artist field or complete lyric timeline in this compact model.
Title provenance carries historical revision evidence; only the primary member
is authoritative for current source content. Value copies own their strings.

The pure `catalog-metadata-core` suite remains separate from integration coverage.
The development tree has 24 hardware-free / 27 audio-enabled suites. Released
v0.2.0 remains unchanged. Section 9 records the current integration boundary.


## 9. Current scan/preparation integration boundary

`registerRoot(path, RootSourcePolicy)` validates mode/encoding before any
filesystem access or catalog mutation. Defaults remain SmfKar/UTF-8/automatic.
The immutable CatalogRoot policy is retained across rescans and reattachment.
There is no policy update operation. Unknown track existence is diagnosed per
source, not guessed during registration.

CatalogSong now owns one PrimaryMidi member; `locator()` and `sourceRevision()`
read directly from it. Compact source metadata is shared as const storage.
Readable-invalid SMF retains its member revision but has no verified metadata.
Canonical SMF with invalid selected lyrics retains CanonicalSmf kind and nested
KAR diagnostics, with no title/summary. Missing/unreadable/reattached sources
clear current metadata and the content token. Older owning snapshots retain
their historical data. Logical SongCatalog still accepts caller validation
results; filesystem guarantees belong to SongDiscovery, not a tamper boundary.

Scan now extracts before publishing Ready. Successful NoLyrics remains Ready.
FF05 preference, identified FF01 selection and first nonempty @T provenance
come from the existing extractor, without artist inference or filename fallback.
Selected text/track/ambiguity errors make only that source Invalid. Limit,
cancellation, allocation and consistency failures discard the entire scan.
AfterExtraction is an additional serialized checkpoint for deterministic tests;
hooks must not reenter library methods.

The existing CatalogLimits `stagedLocatorBytes` name now covers the same 64 MiB
allowance for all locator/metadata payload. Scan conservatively reserves two
copies of existing locators plus five incoming copies (enumeration, candidates,
diagnostics, verification/fold-sort or publication). Compact immutable title
storage is charged once per shared record. Catalog lookup maps use stable views;
reserve precedes view construction, and existing locators are not reassigned.
Pure commitScan reserves two existing and two incoming copies plus shared titles.
Its older locator-only boundary fixtures are adjusted to these coexistence sums.
Relocation also checks its staged locator/title allowance before copying.

Extraction takes at most half the remaining staged ceiling to cover decoder and
owned text coexistence; the existing extractor also charges fixed records under
its own bound. Empty NoLyrics results require no variable payload. Compact title
validation reserves its two temporary copies before allocation. These are
conservative payload limits, not allocator-overhead or process-memory caps.
Canonical SMF input/parse storage retains its separate bound. Re-enumeration
and exact rehash remain consistency checks, not atomic filesystem transactions.

PreparationOptions now separates optional `lyricOverride` from `lyricLimits`
and a bounded `stagedBytes` allowance. Absent override inherits the selected
snapshot root policy after lineage/attachment checks; unknown encoding fails at
Resolve before source I/O or hooks. Ready under the root policy is still required,
so an override cannot rescue an Invalid source. Preparation performs fresh
extraction and compaction, then verifies the source again before returning.
Its budget includes acquired snapshot payload, a possible diagnostic locator
copy, and live extraction/compact text. It returns no partial PreparedSong.

`PreparedSong::effectivePolicy()` and `sourceMetadata()` describe fresh effective
selection; `song().metadata` describes the acquired catalog's root selection.
The original snapshot and both timelines remain owned. Per-call selection does
not mutate catalog metadata or the root policy. Already-prepared metadata is
unchanged by later scans or reattachment.

User overrides and display resolution are implemented as recorded in section 10.
Durable storage, NCN evidence, application/CLI orchestration and manual Windows
device/lyric validation remain pending. This slice adds no audio operation, CLI grammar, version or dependency.


## 10. Current override and display transaction boundary

`SongCatalog::replaceUserOverrides` and its `SongDiscovery` wrapper take a
SongId, `CatalogOverrideRequest`, and `CatalogMetadataLimits`. Requests borrow
optional UTF-8 views for the duration of the serialized call. They replace the
whole record; absent clears that field, while present empty fails. Text remains
strict UTF-8 with the decoder's C0 policy. No trim, normalization, artist guess,
media-file write, or lyric/audio operation occurs.

CatalogSong stores optional shared const `CatalogUserOverrides`. The transaction
validates configuration and field bounds, then checks for an exact no-op before
allocating. Identical validated bytes retain the same snapshot and revision,
including at revision exhaustion. An actual change stages both fields and the
catalog copy, increments revision once, and publishes via one noexcept swap.
Unknown IDs, invalid text, limits, allocation failure and revision exhaustion
return structured `CatalogMetadataError` values with field/byte offset where
applicable, without partial publication. Serialized readers acquire snapshots;
this does not authorize concurrent calls on the catalog writer itself.

Rescan, relocation, root reattachment and invalidation preserve the shared
user record. Removal discards it; later discovery creates a different SongId
without overrides. Source/member tokens, attachment generations and lyric
selection are independent of override edits. Historical snapshots retain the
old record; Invalid/Missing entries may still display overrides without becoming
playable.

`display(acquiredSnapshot, SongId, limits)` validates lineage and resolves an
owning immutable `CatalogDisplayMetadata`, without filesystem work. Title order
is user override, selected source title, then full filename stem; artist is user
artist or absent because no source-artist convention is accepted. Display fields
carry UserOverride, SourceTitle or FilenameFallback origin. SourceTitle retains
exact revision/policy/event provenance. Fallback never populates source metadata.
Only the last extension is removed: `Live.Set.kar` becomes `Live.Set`; a leading
dot without a later dot is part of the filename (`.mid` stays `.mid`). The resolver
preserves UTF-8 spelling and performs no rendering/escaping or display layout.

Preparation materializes `PreparedSong::displayMetadata()` from freshly extracted
effective source metadata plus overrides in the acquired catalog snapshot. This
is distinct from catalog `display`, whose source selection is the root policy.
Already-prepared display/source/override values survive later edits, rescans,
reattachment, and owner destruction. No display lookup revalidates source bytes;
preparation retains its separate Ready/revision/containment gates.

Each field has a default 4,096-byte ceiling. Smaller positive limits are valid;
zero or larger limits are InvalidConfiguration. `stagedBytes` draws on the same
bounded 64 MiB variable-payload allowance. Shared source and override records
are charged once by object identity; separately owned equal records cost twice.
Transaction staging charges two locator copies, retained shared text, and two
validation/output copies per requested field before growth. Display lookup
charges one retained catalog locator copy and shared text, then reserves two
copies per materialized field. Preparation includes the acquired catalog,
error-locator reserve, live extraction and fresh metadata before display charges.
Failed charges do not poison shared-record credit. Identical no-ops need no
staging allowance because no text/snapshot copy is created.

The existing scan/relocation/reattachment/add/remove payload checks include user
records. Historical snapshots held externally can extend object lifetimes; the
per-operation allowance is not a cap on all externally retained snapshots,
allocator overhead, or total process memory. Canonical SMF parse and extractor
record bounds remain separate. Storage, NCN evidence, application/CLI orchestration
and manual device/lyric validation remain pending. Released v0.2.0 is unchanged.


## 11. Accepted persistence boundary

[OHK-STORE-030](CATALOG-PERSISTENCE-CONTRACT.md) accepts preserving explicit
user overrides, identity/allocator history and root policies in a bounded
checkpoint. It deliberately omits source-derived metadata, validation authority
and materialized display strings. A restored fresh owner rebuilds source
metadata only after explicit root attachment and complete scanning. Existing
in-process snapshots/prepared display retain their original ownership. The
pure codec and fresh-owner restore are implemented; native stores and durable
wiring remain pending. Current override/display support does not claim persistence or authorize automatic legacy DB migration.
