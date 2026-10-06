# Karaoke library contract proposal

**Contract ID:** OHK-LIB-030
**Status:** Proposed pre-implementation contract; requires review and acceptance.
**Target:** 0.3.0; not part of released OpenHDK v0.2.0.

`SPECIFICATION.md` remains the authority for implemented behavior. MUST and
MUST NOT below define obligations for a future implementation after this
proposal is accepted. They do not claim that library services exist today.
Lyric extraction and NCN format rules are in
[LYRIC-TIMELINE-CONTRACT.md](LYRIC-TIMELINE-CONTRACT.md).

## 1. Scope and ownership

The library indexes explicitly registered local song roots. It discovers song
sources, validates them, stores catalog metadata, and resolves a selected song
into immutable playback input. It MUST NOT edit, move, rename, or delete source
files. Removing a catalog entry MUST affect only catalog data.

No legacy database, user song folder, BASS wrapper, Qt class, or private media
is imported automatically. The core model MUST be independent of filesystem,
database, UI, and synthesizer implementation types.

The first target covers canonical SMF format 0/1 in `.mid`, `.midi`, and `.kar`
files, plus the explicitly selected NCN24 profile defined in the companion
contract. Filename extensions are discovery hints, not proof of format.
HNK/HNK3, archives, remote URLs, playlists, installers, UI rendering, database
migration, permissive SMF parsing, and external MIDI remain outside this target.

## 2. Song identity and catalog model

A library owns persistent opaque `RootId` and `SongId` values. A `SongId` MUST
be unique in that library and MUST NOT derive from title, artist, filename stem,
filesystem enumeration order, or a content hash. New IDs are assigned only
when a complete scan transaction commits, including diagnostic-only entries. An ID is not a globally portable
song identifier. Tests may supply a deterministic ID allocator.

A source key is `(RootId, relative locator)`. A locator is the primary SMF path
relative to its registered root, stored as UTF-8 with `/` separators. It MUST
contain no absolute path, empty component, `.` or `..`. Preserve spelling and
Unicode scalar values; do not normalize Unicode or use the current locale.
Compare locator keys by unsigned UTF-8 bytes, case-sensitively. To keep a
Windows-compatible catalog, reject sibling candidates whose relative locators
are equal under ASCII case folding, even on a case-sensitive filesystem.
An operating system may resolve additional aliases to the same path;
registration and scanning MUST reject those ambiguous aliases rather than
silently select one. Hard links at distinct paths are not path aliases. Case-only renamed
paths require the same explicit relocation operation as other renames.

| Field | Required meaning |
| --- | --- |
| SongId, RootId, relative locator | Catalog identity and source location. |
| Source kind/profile | SMF, KAR lyric profile, or explicitly selected NCN24. |
| Source members | Primary MIDI and, for NCN24, exact LYR/CUR locators. |
| Validation state | Ready, Invalid, Missing, or UnsupportedProfile. |
| Title/artist | Optional text with source/provenance; absent is not invented. |
| Display fallback | Full filename stem when title is absent; not an identity. |
| Lyric selection/encoding | Explicit profile, selected track if supplied, and encoding. |
| Source revision | Exact content comparison token for every member. |
| Catalog revision | Monotonic committed revision for immutable reader snapshots. |

A revision token MUST detect changed content even when size and modification
time are unchanged. The implementation must specify its digest algorithm or
byte comparison before integration. It must not use a hash as SongId or merge
songs solely because their bytes match.

Rescanning an unchanged key preserves SongId. Replacing bytes at that key
preserves SongId but revalidates metadata and lyrics. The same bytes at a
different key create a separate song. Titles may be duplicated. A missing
source keeps its SongId and becomes Missing after a complete scan. An explicit
relocation validates the new key, retains SongId, and fails atomically if the
new key already belongs to another entry. It does not rename the media file.
Reattaching a root to a new absolute directory preserves RootId and relative
keys only after explicit user selection and validation.

## 3. Deterministic discovery

A root registration specifies an existing directory, source mode, and lyric
encoding/selection policy. Root paths MUST be resolved on the control path.
Reject overlapping or equivalent roots in one library. Do not infer a root
from the current working directory or scan the user's home automatically.

SMF/KAR roots recursively discover regular `.mid`, `.midi`, and `.kar` files;
extension matching uses ASCII case folding. Skip unrelated files. Do not follow
symbolic links, Windows junctions, or other reparse-point entries. Reject any
candidate that resolves outside its root. Hard-linked files at distinct valid
locators remain distinct entries. Directory failures MUST be reported.

Collect and sort candidates by unsigned UTF-8 relative locator before assigning
new IDs or reporting results. Metadata text never determines scan order.
Duplicate source keys and aliases are errors, not last-file-wins behavior.
For an NCN root, only `Song` MIDI candidates form songs; Lyrics and Cursor
members are resolved by the companion profile, never as independent songs.

A scan MUST return candidate counts and per-file diagnostics. Validate source
bytes before changing a Ready entry. An invalid candidate keeps its identity
and becomes Invalid; old parsed lyrics MUST NOT remain usable as current data.
Unsupported profiles become UnsupportedProfile. One bad song does not hide
other valid candidates in a complete scan.

Use staged changes and one atomic catalog publication. Cancellation, root
enumeration failure, database failure, source changes during preparation, or
limit exhaustion MUST discard the entire staged scan. In particular, an
incomplete scan MUST NOT mark unvisited songs Missing. A complete scan may
publish Ready/Invalid/UnsupportedProfile/Missing states together.

## 4. Bounded input

The initial control-path limits are defaults, not filesystem capabilities:

| Limit | Default |
| --- | ---: |
| Candidate songs per scan | 10,000 |
| Directory depth below root | 32 |
| One SMF source | 64 MiB |
| Lyric source bytes per song (selected KAR payloads or complete LYR) | 4 MiB |
| Lyric cues per song | 100,000 |
| One UTF-8 locator or title/artist field | 4,096 bytes |
| Total staged variable-length catalog/lyric payload | 64 MiB |

A MiB is 1,048,576 bytes. The last limit counts owned source-lyric bytes,
decoded text, locators, and metadata retained in the staged result; event and
record counts additionally bound fixed-size storage. Process one SMF at a time
and release its temporary parse storage before processing the next candidate.

Every limit MUST be positive, checked before allocation/append where possible,
and included in the scan configuration. Smaller configured limits support
boundary tests. Increasing a default requires a reviewed contract change.
Arithmetic overflow or an exhausted limit MUST return a structured error; do
not silently truncate sources, lyrics, or catalog entries.

## 5. Database and concurrency boundary

The catalog is owned by OpenHDK, outside source roots, at an explicitly selected
application-data path. Source metadata and user overrides are separate fields;
rescans MUST NOT overwrite explicit user overrides. Database APIs MUST be
absent from the audio callback. One control writer owns catalog mutation;
readers receive immutable catalog snapshots with one committed revision.

Persistent storage must record a schema version and reject unsupported newer
schemas without modifying the database. A scan, relocation, or metadata update
is transactional: either all of its staged catalog changes commit or none do.
Failed creation/migration MUST preserve the previous valid database. Queries
MUST treat paths and metadata as data, not executable SQL or shell text.

An in-memory implementation is the first test boundary, not a claim of durable
storage. Database engine selection, exact schema, transaction/recovery mechanics,
and dependency/license review require a separate reviewed change before the
persistent adapter is implemented. No SQLite/Qt dependency is approved here.
An existing HandyKaraoke database MUST NOT be opened for automatic migration.

## 6. Playback preparation

Resolve a song by SongId from a snapshot. Ready is necessary but not sufficient:
reopen every source member, revalidate containment, compare content revision,
and prepare immutable MIDI and lyric timelines before starting audio. A
changed, missing, unreadable, invalid, or unsupported source fails preparation
without starting or replacing playback. No partial NCN bundle is playable.

MIDI preparation reuses the released canonical parser/compiler. Lyrics use the
same compiled media clock and event ordering; they do not dispatch MIDI or
change controllers, gains, presets, velocity curves, or the limiter. A library
rescan MUST NOT replace buffers used by an active prepared song. Playback owns
its snapshot until completion/stop. Filesystem and database work stay outside
the render callback.

New CLI/library operations and callback-visible progress publication require
separate integration contracts before implementation. This proposal adds no
command-line flags, playlist behavior, seeking, or UI framework.

## 7. Errors and acceptance

Errors MUST carry a stable domain/code, operation, optional source member and
byte/event position, and a safe diagnostic. Proposed categories include
InvalidRoot, AmbiguousPath, SourceUnreadable, SourceChanged, InvalidSmf,
UnsupportedProfile, InvalidText, MissingMember, InvalidCursor, LimitExceeded,
Cancelled, StorageFailure, and UnsupportedSchema. Preserve nested SMF errors
rather than replacing their exact code/offset. Library errors are not synth
errors. Callers may continue using the last committed snapshot after failure.

Minimum independently authored tests:

- deterministic discovery across different enumeration orders;
- ID stability on rescan/replacement, separate IDs for duplicate bytes/titles,
  explicit relocation conflicts, and root reattachment;
- mixed extension case, UTF-8 paths, alias collisions, overlapping roots,
  symlink/junction escapes, unreadable and disappearing sources;
- incomplete/cancelled scan rollback versus complete-scan Missing states;
- unchanged size/time with changed content; member replacement during prepare;
- every limit at and beyond its boundary, overflow, and no partial publication;
- catalog-only removal, user override preservation, immutable active snapshots;
- in-memory transaction tests, then persistent schema/recovery tests after
  storage selection; and lyric tests in the companion contract.

## 8. Implementation and release gates

Review in slices: pure identity/catalog model; discovery and in-memory scan
transactions; KAR extraction and lyric timelines; NCN profile evidence and
normalization; persistent adapter; then application/audio integration.

Before claiming v0.3.0 support, approve storage and progress-publication designs,
close the NCN evidence gate, pass hardware-free regression and both CI jobs,
and report manual Windows playback/lyric timing with identified revisions and
external assets. A headless lyric inspection surface may validate integration
without selecting a GUI framework. Source-only release rules still apply.
