# Durable library CLI and playback orchestration plan

**Plan:** OHK-CLI-030
**Status:** Proposed; requires review and acceptance before implementation.
**Target:** OHK-DURABLE-030 slice 4, after scoped mutation acceptance.

## 1. Purpose and current boundary

The durable service implements Create/Open, metadata overrides and all five
root/song mutations. Scoped mutation acceptance binds revision `54f404a` to the
recorded WSL2 `/dev/sdd` ext4 environment. The current audio executable still
plays an arbitrary MIDI path through `readMidiBytes`; it has no library command.
The service exposes snapshots but no preparation query. This proposal supplies
no runtime feature, test, dependency, version change or completed checkbox.

Use the existing service as the only mutation authority. Do not create a second
catalog owner, import detached IDs as live authority, edit checkpoint bytes,
save after an in-memory mutation, or bypass the native admission fence.

The first executable is a separate hardware-free `OpenHDKLibrary` control tool.
It can build with `OPENHDK_BUILD_AUDIO_POC=OFF`, without FluidSynth/miniaudio.
The existing `OpenHDK` playback options remain compatible. Playback integration
follows management and preparation slices; do not require audio dependencies to
create or inspect a library. No GUI, NCN normalizer, MIDI hardware, VST or
Windows checkpoint implementation is added by this plan.

## 2. Entry modes and session lifetime

Proposed entry grammar:

```text
OpenHDKLibrary --store-dir ABSOLUTE_DIR --store-name PRIMARY create
OpenHDKLibrary --store-dir ABSOLUTE_DIR --store-name PRIMARY session
OpenHDKLibrary --help
```

Parse the complete argument list before provider acquisition. Reject unknown,
duplicate, incomplete or conflicting options. Create admits an empty owner only
on ExpectedAbsent; never overwrite an existing primary. Session opens only the
explicit primary, validates/reconciles it, and restores a fresh unattached owner.
A missing/invalid primary is an error; do not create it, attach saved hints,
rescan sources or promote a staging file automatically. Parent directories must
already exist. Provider acquisition can create its stable lock/probes before
admission; do not claim zero filesystem effects on factory failure.

Session holds one exclusive service/lease until quit, EOF or terminal failure.
Commands run serially on the control thread. Restore retains IDs and hints,
not source Ready authority: a new session must explicitly reattach roots and
scan before preparation. Keeping one session permits attach, scan and play to
share the same admitted owner. Do not persist raw handles or live ID authority.

Non-Linux execution returns UnsupportedStorage before native store I/O. On Linux,
production eligibility rejects unsupported filesystems; no test bypass or
runtime bypass flag is exposed. Filesystem eligibility alone does not extend
revision/environment-bound acceptance to every Linux installation.

## 3. Bounded command grammar

Session consumes one command per input line. Proposed verbs:

| Command | Effect |
| --- | --- |
| `help` / `quit` | Show grammar / close the owned service |
| `roots` | Show the acquired snapshot root records and attachment status |
| `songs ROOT OFFSET COUNT` | Show a bounded page of songs and display metadata |
| `register PATH [utf8\|tis620] [auto\|TRACK]` | Register a SmfKar root with explicit complete policy |
| `reattach ROOT PATH` | Explicit durable root binding transaction |
| `scan ROOT` | Complete bounded scan under the registered root policy |
| `relocate SONG ROOT LOCATOR` | Logical identity-preserving relocation; no file move |
| `remove SONG` | Catalog removal; no source deletion |
| `overrides SONG TITLE ARTIST` | Whole-record replacement; two fields staged together |
| `prepare SONG` | Later slice: bounded preparation and display summary; no audio |

`ROOT`/`SONG` are unsigned decimal selectors with no sign, whitespace, overflow,
zero or sentinel value. Resolve them against records in the service's acquired
snapshot, then pass the existing typed IDs from those records. Do not add public
ID constructors or accept owner/store tokens, projections or arbitrary snapshots.
IDs are library-local selectors; do not promise globally unique or cross-store
identity. Missing selectors fail without staging a mutation.

Tokens are separated by ASCII space/tab. Double-quoted tokens preserve exact
UTF-8 bytes and support only escaped quote and backslash; reject unknown escapes,
unclosed quotes and trailing junk. Unquoted `-` means absent only for the two
override fields; quoted `"-"` is literal text. Quoted empty override text fails;
absence clears. Both fields are required, so partial title-only updates cannot
silently erase artist. Policy arguments must appear as a complete pair or both
be absent (registered defaults); explicit TRACK validity is source-dependent.
No shell expansion, command substitution, environment expansion or executable
launch is performed. A path with spaces is a quoted token, not a shell command.

Input limit: 16 KiB per line, at most eight tokens, individual text limit 4 KiB.
Read incrementally; detect an oversized line before growing beyond the limit,
drain through newline and return LimitExceeded. EOF inside a quote is invalid.
Page COUNT is 1..100; OFFSET must fit size_t. Reject out-of-range configuration
before I/O. Limits are fixed defaults in the first slice, not user overrides that
can disable bounds. Source-dependent parser/lyric limits remain those of existing
services; do not substitute a less bounded CLI file reader.

## 4. Output, errors and committed results

Emit bounded JSON Lines records using an internal reviewed string escaper, with
no third-party JSON dependency. Escape quote/backslash and all C0 controls;
preserve valid UTF-8 bytes without trim, normalization or inferred artist.
Native path/locator bytes that are not valid UTF-8 need an explicit byte-safe
representation, not lossy conversion. Never write raw terminal control bytes.
Do not publish raw checkpoint wires, absolute saved hints, pointers or tickets
in routine listing/result records. User-requested source paths stay input only.

Listing includes numeric selectors, revision, generation, song state and display
origin. Use `resolveCatalogDisplay`; filename fallback is not source metadata.
Root listing reports attached/unattached and policy without auto-resolving hints.
Bound an output page to 2 MiB, a result/diagnostic batch to 2 MiB and a per-record
output to 64 KiB. Build each complete bounded response before emitting it; failed
listing/display construction emits an error, not an apparently complete page.
If diagnostics cannot fit, report explicit output LimitExceeded plus the known
mutation outcome, never silently omit rows and claim a complete scan report.

Keep structured error families/operation/field/offset and owned RootId mapping.
Do not confuse binding, provider publication and service status enums. Saved is
reported only from the service's confirmed result; cleanup warnings accompany
Saved. NoChange retains the current revision and still verifies the primary and
fence. Failed mutations expose no staged identity, snapshot or receipt.

Output formatting occurs after a service call returns; it must not add fallible
work inside confirmed-save-to-memory-swap. Prefer preallocated result envelopes,
but do not claim that all ostream/OS output is nonthrowing or exactly once.
If output fails after commit, return output failure with commit outcome retained
internally; never claim rollback, repeat the mutation automatically or suppress
a known commit. Users must reopen/list to determine state after receipt loss.

Proposed process exits: 0 orderly session completion; 1 rejected command(s) while
Ready; 64 entry syntax/configuration error; 69 unsupported native storage;
74 output I/O failure; 75 Busy acquisition; 76 RecoveryRequired. A session with
ordinary rejected commands continues while Ready and remembers a nonzero exit.
RecoveryRequired terminates mutation dispatch and closes explicitly; recovery
requires a new user invocation. No automatic retry, backup adoption or reattach.
EOF/quit closes successfully admitted ownership without deleting the stable lock.

## 5. Preparation query and playback controller

Add a serialized read-only service preparation entry only after reviewing its
shape. It resolves a service-owned acquired snapshot/typed SongId and calls
existing `SongDiscovery::prepare` through the internal owner. It shares the
service reentry guard, accepts bounded options/cancellation and returns owning
`PreparedSong`. It must not export the owner, bypass lineage/attachment checks,
rescue Invalid rows, mutate catalog revision or claim preparation is a save.
Reject Closed/RecoveryRequired before owner access. Read failures retain memory
and primary. Define explicitly which native authority drift requires recovery;
do not convert all InvalidLyrics/SourceChanged errors into storage failures.

Audio orchestration is a separate reviewed controller that retains PreparedSong,
its snapshot/source revision, compiled timeline and lyric timeline. It passes
prepared MIDI to the existing backend. Bind the observer only with the admitted
start generation; rejected start establishes no binding. Poll committed backend
clock on the control/UI thread; no wall-time interpolation. Stop/clear observer
before replacement, stop, recovery or closing playback context. Backend callback
continues to perform no allocation, deallocation, locking, I/O or logging.

First playback slice serializes control actions and playback: reject library
mutation while playback is active rather than promise live catalog/audio graph
replacement. Snapshot/PreparedSong ownership still survives subsequent mutation.
Display uses prepared effective-policy metadata and acquired overrides. NoLyrics
is valid with no cue display; Failed/Stopped/unavailable clears through the
existing observer contract. Terminal Finished may retain the observed prefix.
Windows legacy `--midi` playback/manual validation stays separate from blocked
Windows durable checkpoint acknowledgment. Do not open a Linux store through
DrvFS or claim a Windows playback test validates Linux persistence.

## 6. Resource and receipt boundaries

Input/output buffers, acquired snapshots, prepared values and controller strings
must be charged to explicit application bounds and passed as already-owned bytes
when service APIs require them. One service operation retains the existing
shared 64 MiB logical staging allowance; helpers do not obtain fresh allowances.
Application line/page caps are separate named bounds, not a total process-memory
or open-descriptor claim. Keep prepared playback retention in later admission
budget calculations; do not assume historical snapshots are free.

Validate all syntax and output configuration before staging. No CLI formatter,
input callback or user code is installed as the private store fence. No source
traversal occurs on the audio callback. Preserve cancellation-before-commit and
late-cancellation-after-commit semantics. Signal handling, if added, may only set
an appropriate flag; it must not call service/streams from a signal handler.

## 7. Tests and evidence

- Pure bounded lexer/selectors: overflow, signs, duplicates, quoted Thai/BOM/
  combining bytes, explicit absence versus empty, line/token caps and bad escapes.
- Renderer: exact display origin, controls/quote escaping, byte-safe native text,
  complete-page limits, output failure after known Saved and cleanup warnings.
- Hardware-free command adapter with independent fake service/provider fixtures:
  create/open modes, no implicit attach/scan, every verb, ordinary rejection,
  Busy/RecoveryRequired, EOF cleanup, staged IDs withheld and no automatic retry.
- Preparation: service reentry/state, foreign/stale snapshots unavailable through
  query API, registered policy inheritance/override, source change and lifetime.
- Controller with backend seam: acknowledged generation only, clear-before-start,
  every observer phase, NoLyrics, prepared lifetime and mutation/play exclusion.
- Production smoke: help/syntax in parser-only builds; non-Linux rejection before
  native I/O; existing audio/mixer/diagnostic commands unchanged.
- Fresh pinned WSL2 ext4 CLI session receipts: Create, reopen-unattached, explicit
  attach/scan, overrides, relocation/removal/rediscovery, Busy, recovery and output
  failure. Record exact build/run exits and redaction hashes before acceptance.
- Later manual Windows audio/lyric tests remain their own gate. NCN evidence is
  not inferred from synthetic KAR or durable SMF/KAR CLI success.

Changed application/service/controller code does not inherit accepted runtime
revision labels from `6a6337d`, `4f69246` or `54f404a`. Fresh affected evidence and
scoped review are required. Missing native environment/namespace capability is
Pending/Skipped with reason, never Passed. Keep historical bundles immutable.

## 8. Implementation order and review gates

1. Accept this plan; implement pure bounded command/selector/output models.
2. Hardware-free management session executable and service adapter; fake tests,
   parser-only build, native rejection path and unchanged existing app options.
3. Add service-owned preparation query and summary command with lifetime tests.
4. Fresh pinned management/preparation CLI run and scoped review.
5. Separate playback controller plus existing audio app integration; generation
   and callback/lifetime tests, then affected native/manual evidence.
6. Review release readiness against storage, NCN and manual checklists. Do not
   complete storage/release checkboxes from this proposal or management alone.

Review command grammar, output/exit semantics, preparation authority-drift policy
and playback exclusion before implementation. Windows native checkpoint remains
BLOCKED until its unprivileged publication-sync protocol is separately reviewed.
