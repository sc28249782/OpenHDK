# Test coverage and failure diagnostics

At current main, Linux core CI registers 34 hardware-free CTest suites.
The audio-enabled configuration registers 38 on Linux. Windows bootstrap
builds the audio session integration target and runs 37 suites: its native-store
fixture is Linux-only, and the Windows checkpoint remains BLOCKED. Earlier
counts in sections below describe the development stage at which each suite
was added; the released v0.2.0 baseline had six/nine suites respectively.

`library-audio-session` uses synthetic SMF/KAR and a fake backend with a real
native-store service on Linux. It checks play/stop/status admission, mutation
exclusion, backend refusal and stop retry, active quit ordering, final input
line, oversized line, terminal quit and failed output. It does not run real
FluidSynth playback or accept step 5 native/manual evidence.

## Planned 0.3.0 acceptance

The accepted library and lyric contracts define target test requirements in
[the library contract](../docs/KARAOKE-LIBRARY-CONTRACT.md) and
[the lyric contract](../docs/LYRIC-TIMELINE-CONTRACT.md).
`song-catalog-core` checks logical IDs, unsigned UTF-8 ordering, strict locators,
ASCII case collisions, state changes on complete scans, rollback including a
late staging failure, boundary limits, relocation conflicts, catalog-only
removal, and old snapshot preservation. It performs no filesystem or media I/O.
`song-discovery` uses isolated synthetic directories and SMFs. It checks pinned
SHA-256 vectors, mixed extension case, UTF-8 paths, root overlap/aliases,
per-file nested parse/compile errors, content changes with unchanged size/time,
complete Missing states, detected-change/cancellation rollback, depth/count/byte
limits, hard links, and no-follow symlink escape reads. Windows additionally
tests sharing-denied files; Linux CI tests source/directory permissions. A local
privileged process skips permission tests, and Windows symlink tests report
unavailable fixtures when the account lacks symlink privilege. Windows reparse
code is compiled and executed in Windows CI; this does not establish coverage
of every junction/mount variant.

`lyric-text-decoder` checks every TIS-620 byte against fixed UTF-8 strings
independently generated with Python's strict codec, malformed UTF-8 with exact
byte offsets, C0 policy, BOM/combining-mark preservation, no charset fallback,
invalid configuration, and input/output limits. It preserves CR/LF and marker
characters; it does not implement lyric selection, display actions, or timing.

`kar-lyric-timeline` checks FF05 precedence, ambiguity, explicit track restriction,
named FF01 qualification, metadata/title separation, and all eight contract
display vectors. It validates exact compiler timing/provenance (PPQN 3, tempo
changes, and distinct ticks with equal media times), byte preservation, Thai
metadata/text, structured offsets, no fallback or partial timeline, ownership
beyond the source lifetime, and input/cue/title/staging limits. Staging vectors
include metadata/title and TIS-to-UTF-8 expansion. The suite is synthetic and
performs no media or filesystem I/O.

`lyric-media-consumer` checks inclusive due batches, equal-time order, once-only
whole-cue operation emission, equal-position polls, backward-call rollback,
reset/stop/reprepare, NoLyrics, null preparation, and owning batch lifetime.
Known frame-derived positions from a real PlaybackSession cover pause/resume,
completion without changing session state, stop/restart, and partition-invariant
cue order. These hardware-free tests do not establish backend/device/GUI lyric
synchronization; application/library orchestration remains pending. The accepted
[media-clock handoff contract](../docs/MEDIA-CLOCK-HANDOFF-CONTRACT.md) defines
the required concurrency, lifecycle, failure, and exhaustion tests. Pure-cell
coverage now checks coherent concurrent snapshots, deterministic unstable
reads, generation non-reuse, and both counter exhaustion boundaries. Backend
lifecycle and real-render cases remain acceptance requirements rather than
current pure-cell coverage. The Windows headless smoke test additionally checks
compiled generation acknowledgment, first-block progression, retained finish,
explicit stop, restart, legacy unavailable state, and shutdown. Deterministic
mid-block failure injection checks that failure retains the previous committed
position.

Remaining application/library integration tests must use independently
authored fixtures. The `root-reattachment` suite covers the accepted
[OHK-ROOT-030 boundary](../docs/ROOT-REATTACHMENT-CONTRACT.md): move/rescan,
invalidation of every catalog state, same-path no-op, initial/late cancellation,
injected bad_alloc rollback, native-directory replacement/disappearance,
attachment/catalog exhaustion, stale/foreign snapshot rejection, unrelated-root
preservation, and retained prepared buffers/batches. Its target-local test seams
are not enabled in production. Windows UNC rejection runs on Windows; source
symlink fixtures report a skip if the OS denies link creation. Observable
self-alias cases require filesystem support and are not claimed as current
fixture coverage. NCN24 requires a reviewed evidence supplement before normalizer implementation. No private song or SoundFont
is a repository test asset.

## Failure diagnostics

`stereo-peak-limiter` checks deterministic contract vectors, extreme finite
inputs, NaN/Infinity sanitization, linked stereo balance, unchanged samples
below the ceiling, and equal results across block partitions. The tolerance
for float output and ceiling checks is `1e-6`. Incomplete interleaved frames
fail without changing PCM. The audio smoke test checks bounded finite output
from compiled and legacy playback and a dense maximum-velocity chord.

`midi-mixer-presets` checks name validation, save/overwrite/recall/delete/list,
all-channel flags, revision counts, gain preservation, and existing channel
setters. A control writer and render reader also check that mute/solo masks
remain one complete pair during repeated recall. This is a core-only test;
the console test now covers command argument validation, overwrite, recall,
sorted list, deletion, error reporting, gain preservation, and one revision
per recall. The headless audio smoke test exercises preset mute/recall around
CC7 automation and a later note-on, lifecycle persistence, and legacy rejection.

`midi-velocity-curves` checks the pure velocity helper without audio hardware.
It covers fixed vectors, all positive MIDI velocities with an independent
nearest-integer oracle, monotonicity, velocity zero, and invalid inputs. The
dispatcher tests cover all three curves, zero-velocity note-off, explicit
note-off velocity, adjacent CC7/CC11, program and pitch-bend messages, ignored
pressure events, default linear behavior, invalid curve rejection before
dispatch, and preservation of source event data and times. The audio smoke
test also plays soft and hard compiled sessions and checks legacy rejection.

The C++ test executables use `TestCheck.hpp` to report failed conditions.
`OPENHDK_FAIL_IF(code, condition)` returns `code` from the test function when
`condition` is true. It writes the source file, line, exit code, and condition
to standard error. CTest shows this message with `--output-on-failure`.

For example, a failing MIDI division check reports:

```text
tests/smf_parser_tests.cpp:150: test failed (exit code 1)
  unexpected condition: !expectsSuccess(format0, 0U, 97U, 1U)
```

The line number depends on the source revision. Keep fixture names descriptive
so the reported condition identifies the behavior under test. The diagnostics
CLI test also prints its unexpected output when a channel summary differs.

The helper evaluates the condition once. It preserves short-circuit evaluation
and remains enabled in Release builds with `NDEBUG`. It returns from the test
function, so local objects still run their destructors. Use a nonzero exit code
and call it only from a test function that returns `int`.

To inspect a failure, run CTest after building the tests:

```sh
ctest --test-dir build --output-on-failure
```

A failing test must show its condition and source location. A passing test
keeps its existing exit code of zero. The helper is used only by tests; it is
not part of the audio callback or the production application.

## Generation-bound lyric observer

`lyric-clock-observer` checks acknowledged binding, rejection without mutation,
Preparing/Unstable holds, Playing/Paused/Finished progression, equal polls,
partition-invariant cue indices, terminal and generation/source mismatch clears,
exhaustion, malformed snapshots, backward time, and historical batch ownership.
Actual publication-cell reads and PlaybackSession positions feed the adapter;
NoLyrics remains a valid bound timeline. The Windows headless smoke test also
observes start/first-block/finish through the public FluidSynth clock.

An observer belongs to one backend lifetime. Controllers must clear it before
requesting stop, replacement, or shutdown. Tests do not establish SongId/source
matching, a complete CLI lyric service, or measured device synchronization.

## Single-song preparation

`song-preparation` uses temporary, synthetic SMF/KAR files. It covers SongId
lookup, Ready/token admission, source-size limits, unchanged-size/mtime edits,
rescan identity stability, changed/deleted/symlink-substituted sources,
cancellation, nested parser/compiler/KAR errors, explicit TIS-620 selection,
lyric limits, and NoLyrics. Prepared inputs survive rescan and owner destruction.
A prepared lyric timeline is fed into the generation-bound observer without
changing its retained catalog identity. No audio start is part of preparation.

Native symlink setup may be unavailable on Windows; that fixture runs when the
OS permits creation. Existing discovery coverage retains the native path checks.
Root policy, compact metadata and override/display integration are covered below.
NCN members, persistent storage and
application playback lifecycle remain separate acceptance work.

Preparation lineage regression checks use two distinct libraries with equal
local RootId/SongId, locators, and source bytes. Foreign and unowned snapshots
fail at Resolve without invoking preparation hooks; old same-library snapshots
still prepare after a rescan. A retained snapshot from a destroyed owner cannot
be adopted by a new owner. This is a lineage check, not snapshot tamper detection.

## Catalog metadata and root-policy coverage

[OHK-META-030](../docs/CATALOG-METADATA-POLICY-CONTRACT.md) is accepted.
`catalog-metadata-core` checks default/explicit/unknown policy values,
unchanged extraction limits, source-dependent track validation boundaries,
compact summary consistency, exact UTF-8 preservation, independent malformed
vectors and offsets, C0 controls, empty-versus-absent semantics, 4096-byte and
multibyte boundaries, owning override/provenance copies, and checked budget
charges at/beyond the ceiling (including SIZE_MAX and coexisting copies).
It uses no filesystem, synth, private seam, or media asset.

`catalog-metadata-integration` uses independent synthetic SMF/KAR files in
isolated temporary roots. It checks invalid registration before filesystem work
and ID/revision consumption, immutable root policies, inherited and overridden
selection, FF05 preference without title leakage, NoLyrics/@T-only behavior,
exact title/provenance, primary-member accessor authority, invalid/missing/
reattached clearing with historical ownership, per-file nested lyric errors,
non-rescue of Invalid entries, changed files, cancellation and injected bad_alloc
rollback, and source/metadata coexistence limits. Title fixtures cover 4096/4097
bytes; no real media asset or audio operation is used. Hooks are deterministic
fault injection, not exhaustive allocator coverage. Updated pure catalog boundary
fixtures account for retained/staged locators, including Missing entries.
`catalog-override-display` covers 57 checks for complete title/artist replacement,
explicit clear, no-op, strict UTF-8/C0 validation, exact byte limits, filename
fallback/provenance, retained source and override contexts, revision exhaustion,
rescan/relocation/reattachment/removal, prepared effective-selection display,
owner destruction, and shared payload bounds. It sweeps real allocation-failure
points in isolated replacement/display calls; this is not a process-wide memory
limit or exhaustive coverage of all library allocators. The standalone target
uses `OPENHDK_ENABLE_TEST_SEAMS=1` only for private revision exhaustion setup.
A test-only global allocator is disarmed before assertions and fixture cleanup.
Existing preparation budget fixtures now reserve materialized display text too.


## Catalog checkpoint codec and pending storage acceptance

[OHK-STORE-030](../docs/CATALOG-PERSISTENCE-CONTRACT.md) defines future tests
for independent schema/hash vectors, corruption/version/bounds rejection,
allocator history, fresh-owner lineage, unattached roots and overrides, stale
save tokens, locking, allocation/cancellation rollback, uncertain publication,
explicit reconciliation and native process-interruption recovery. Provider tests
must distinguish file synchronization from namespace/durability acknowledgment.
Windows behavior requires Windows fixtures. Fake failure tests do not prove
physical power-loss survival. The pure `catalog-checkpoint-codec` suite is now
registered alongside `catalog-checkpoint-restore` (twenty-eight core / thirty
audio-enabled); broader native acceptance and remaining durable-first wiring stay pending.

The codec suite compares production output with checked-in independent Python
struct/hashlib vectors, including Thai text, combining sequences, allocator gaps
and exact overrides. Regenerate with
`python tests/fixtures/generate_checkpoint_vectors.py`. It covers all-byte digest
tampering, recomputed-digest structural corruption, canonical ordering, invalid
IDs/references/aliases/policies/text, bounds and operation-wide coexistence,
caller-retained payload charges and deterministic allocation-failure sweeps.
No test opens a source/store path or claims recovery/durability.


## Catalog fresh-owner restore

`catalog-checkpoint-restore` tests validate projection/byte factory admission,
independent fixture data, new lineage, retained IDs/high-water gaps/generations,
exact overrides and Invalid/unattached state. Existing hint paths grant no scan
or preparation access. Explicit first attachment/rescan, active same-path no-op,
stale and foreign snapshots, hint-only duplicate roots, exhausted history,
coexistence limits and hint charges on later operations are covered. Allocation
failure sweeps cover decode/factory and first-attachment publication; cancellation
after staging retains the unbound owner. Owning queries, snapshots, displays,
prepared songs and cue batches retain their data after owner destruction.
These source-root filesystem fixtures test explicit attachment/discovery only;
no test reads/writes a checkpoint file or claims native store durability.

## Detached checkpoint store protocol

`catalog_checkpoint_store_tests.cpp` registers `catalog-checkpoint-store`.
An in-memory provider simulates leases, directory identity, artifact staging,
publication outcomes and reconciliation. Tests inject each candidate/prior
create/write/sync/read failure, compare retained primary bytes, and require no
success acknowledgment on failure. They cover stale/foreign expectations,
nonblocking/reentrant ownership, history rollback, sequence exhaustion/no-op,
cancellation, external edits, uncertainty with old/new primary, missing/corrupt
primary, explicit reopen, cleanup warnings and exact 191/192-byte empty-create
coexistence bounds. Allocation-failure sweeps check prepublication rollback;
blocking allocations at publish tests the acknowledgment path.

These simulations do not prove native OS locking, path capability validation,
crash recovery or durability. The test adds no filesystem provider and does not
complete the storage release gate. There are 28 core / 31 audio-enabled suites.

## Planned native checkpoint provider evidence

The [accepted native provider plan](../docs/NATIVE-CHECKPOINT-PROVIDER-PLAN.md)
records an API/fault/interruption matrix and per-platform evidence fields.
The ownership primitive has a dedicated suite; the experimental provider adds
publication regression tests without native acceptance claims. The baseline is
28 core / 31 audio-enabled suites. Fake-provider successes do not prove native
lock ownership, namespace synchronization or durability.

Future native tests must verify the filesystem used by the test directory and
record the exact provider/test revision. Test same-provider reuse, independent
owners in one process and independent subprocesses. For Linux, do not treat
an inherited flock descriptor as an independent contender. Cut-point tests
terminate only an owned child after a phase handshake, use finite timeouts,
and inspect primary/artifacts before harness cleanup. Injected API faults and
real subprocess interruption must have separate results. Missing NTFS/ext4
execution remains Pending/Skipped evidence. Windows acknowledgment tests require
a reviewed synchronization sequence first; they cannot replace that review.

## Linux checkpoint lease

`linux_checkpoint_lease_tests.cpp` registers linux-checkpoint-lease with
OPENHDK_ENABLE_TEST_SEAMS=1 only on that target. On Linux it runs real openat,
metadata, process-registry and flock checks. A test-only filesystem bypass allows
ownership probes on overlay/tmpfs; production ext4 eligibility stays enabled.
The local overlay run is not native ext4 publication evidence. Non-Linux runs
exercise only UnsupportedStorage without I/O, not Windows native ownership.

The suite verifies same-object/same-process contention, fresh exec contenders,
process-exit lock release, regular single-link primary/lock admission, FIFO and
ancestor/final symlink rejection, directory/lock replacement, preserved bytes,
exact configuration bounds, allocation-failure reservation cleanup and the
64-owner registry cap. Parent-owned child
PIDs have bounded waits and cleanup. No source media or unowned directory is
removed. Publication, synchronization and durability tests remain pending.

## Experimental Linux checkpoint provider

`linux_checkpoint_provider_tests.cpp` is registered as linux-checkpoint-provider.
CMake enables OPENHDK_ENABLE_TEST_SEAMS for this target only. Linux regression
uses real openat/flock/rename/fsync in a harness-owned temporary directory, with
filesystem eligibility bypass when native ext4 is unavailable. Non-Linux builds
check UnsupportedStorage without filesystem access. This is 28 core / 31 audio
suites, not a storage acceptance claim.

Checks cover create/update/reopen/no-op/stale tickets, bounded artifact reads,
foreign/consumed/stale capabilities, identity-mismatch preservation, staging
faults, uncertainty/faulted-store behavior, cleanup warnings, missing/corrupt
primary, shared retention limits, allocation failures, short-I/O/EINTR and
allocation prohibition after rename. Four fresh-exec interruption points run
twice each with finite waits. Native-run instructions and pending obligations
are in [the evidence record](../docs/NATIVE-LINUX-CHECKPOINT-EVIDENCE.md).


## Bounded Linux checkpoint receipts

`linux-checkpoint-evidence` is a separate synthetic evidence target. Its test-only
fixed buffers distinguish selected real native returns from injected failures,
capture tiny artifact bytes before cleanup, and retain receipts from owned
children before terminating them. It asserts pre/post-publication revision
results and prohibits allocation after the instrumented update's rename.
Non-Linux execution reports Skipped; ordinary Linux CI uses the private bypass
and does not establish native ext4 acceptance.

Use `bash tests/run-linux-checkpoint-evidence.sh FULL_COMMIT_SHA EXT4_PARENT`
from a clean pinned checkout for a new native collection. The script records
compiler/test/overall exit codes and log hashes, keeping failure bundles too.
See [receipt semantics and pending gates](../docs/NATIVE-LINUX-CHECKPOINT-EVIDENCE.md).
There are 28 core / 31 audio-enabled suites. The nine seam targets require
OPENHDK_ENABLE_TEST_SEAMS=1 when compiled directly; CMake sets it per target.


The same evidence target now adds case receipts for same-provider/process and
fresh-exec ownership, OS release after owned-child termination, unsafe path/file
types and directory replacement, forced seven-byte I/O with injected EINTR,
partial-write EIO, allocation-failure budgets, external primary/stage replacement,
ExpectedAbsent EEXIST, invalid/schema-0/schema-2 primary and missing primary after
uncertainty. Each refusal checks byte preservation or explicit absence before
harness cleanup. The partial-write seam is test-only. Source-corruption tests do
not repair or select staged alternatives. Aggregate lease errors do not constitute
a full syscall trace. This extends the existing target; suite counts are unchanged.
The stored 9310ab3 receipts predate these cases. The separate
[6a6337d matrix run](../docs/evidence/linux-checkpoint/2026-10-09-matrix/README.md)
records their native WSL2 ext4 execution. The maintainer explicitly accepted
that exact revision/environment after PR #65 review; see the
[scoped decision](../docs/NATIVE-LINUX-CHECKPOINT-EVIDENCE.md#explicit-scoped-acceptance--2026-10-09).
This acceptance does not alter submitted logs, expand platform coverage or close
Windows/live-wiring gates.

## Live checkpoint capture

`catalog-checkpoint-capture` exercises serialized acquisition from a live
SongDiscovery owner. The acquired value retains the owner snapshot and an
immutable complete detached projection. It checks allocator gaps/exhaustion,
root policy/generation and attached/restored hints, exact override bytes,
codec/restore roundtrip, reattachment and override edits after acquisition,
retained lifetime, count/text/wire/coexistence limits and allocation failures.
Deleting an attached fixture directory before capture proves that acquisition
uses retained mappings rather than probing or reading source files.

The adapter accepts no arbitrary acquired-snapshot argument. Callers acquire
through the adapter before later saving that historical projection; it never
reads newer counters or bindings on that projection's behalf. Owner access
must be serialized. Windows hint preflight reserves three UTF-8 bytes per
UTF-16 unit and may conservatively reject a boundary. Logical payload accounting
includes retained catalog metadata once per shared record and output copies;
it is not a whole-process memory cap. Capture performs no save, durability
acknowledgment, mutation publication or automatic root attachment. Commit-before-
publish mutation wiring remains a separate slice; Windows save stays gated.

## Durable override service tests

[OHK-DURABLE-030](../docs/DURABLE-LIBRARY-SERVICE-CONTRACT.md#9-required-tests-and-implementation-order)
defines slice 5 requirements. The new durable-library-service target has 68
numbered checks using an independent two-slot fake namespace. Tests observe old
memory during publication and require confirmed
sync before the memory swap. They cover baseline admission, complete override
staging, rollback, no-op, cleanup warnings, uncertainty/recovery, allocation
prohibition after publication and shared-ledger boundaries. Native binding and
reciprocal store/root containment need separate reviewed integration/evidence.
The target requires OPENHDK_ENABLE_TEST_SEAMS=1, configured only for this target
in CMake and included in the collector seam list. Factories accept only the test
provider category; they cannot accept a native Linux provider as a fake provider. Separate
experimental Linux factories construct their own native provider. Current counts are
28 core / 31 audio-enabled. Historical provider receipts remain unchanged.

## Native binding primitive tests

[OHK-BIND-030](../docs/NATIVE-SERVICE-BINDING-PLAN.md#10-current-bindingancestry-primitive)
is accepted. The native-store-binding target checks private construction,
lease-epoch/lifetime, root ancestry, replacement, bounds and allocation-failure
FD cleanup. It requires OPENHDK_ENABLE_TEST_SEAMS=1 only on its CMake target;
the collector includes that target in its seam list. Missing mount-ID and
root-mount mismatch are explicit injected tests. `/proc` exercises real
other-mount rejection without modifying that mount. The lexical-bypass seam
checks the ancestor algorithm, not bind-mount support.

Mount-alias namespace coverage is reported Skipped; no namespace harness is
implemented. Local runs bypass ext4 eligibility only under seams. Collector
runs with OPENHDK_NATIVE_CHECKPOINT_DIR use production lease eligibility and
retain the Pending native acceptance marker. The final coordinator fence and experimental
native factory now have a separate integration target. There are 28 core / 31 audio-enabled
suites. Historical evidence bundles stay unchanged.

## Experimental native service tests

The native-durable-service target has 26 numbered checks for explicit Create/Open,
overlap rejection, RootId diagnostics, final and NoChange admission fences,
confirmed-save allocation prohibition, CommitUncertain recovery and factory
allocation/FD rollback. The independent fake service target has 68 checks,
including final/NoChange fence refusal. These targets use seams only in their
own CMake definitions and collector registrations. Local native tests use the
private eligibility bypass. OPENHDK_NATIVE_CHECKPOINT_DIR makes the collector
path use production ext4 eligibility, including allocation sweeps.

Current counts are 28 core / 31 audio-enabled; per-case admission receipts and
fresh native acceptance remain pending. Existing bundles are unchanged.

## Native admission receipt instrumentation

The binding and experimental native service targets now emit bounded per-case
[admission receipts](../docs/NATIVE-ADMISSION-EVIDENCE.md) for lease epochs, identity/mount checks,
ancestor termination and the final changed/NoChange fence. The existing collector
verifies both logs with Python 3 and records its verification exit code. Current
counts remain 28 core / 31 audio-enabled; binding checks are 74 and native service
checks are 33. A fresh pinned ext4 run is recorded in the evidence section below; explicit
scoped native factory acceptance is recorded below. Historical evidence and the `6a6337d` provider acceptance are
unchanged. Windows checkpoint acknowledgment remains blocked.

## Recorded WSL2 ext4 admission run

The maintainer supplied a [fresh pinned admission run](../docs/evidence/native-admission/2026-10-10-wsl2-ext4/README.md) at
`4f6924670d4272736984342c8294967650c60edb` on WSL2 `/dev/sdd` ext4.
All 28 compile/run exits, admission verifier and overall exit are zero. The
binding/service logs use bypass zero and contain 13 traces, 364 identity
observations and three fence cases. The published logs redact the local username
with original/published hashes and replacement counts. This is supplied execution
evidence, not independent source/run authentication. Explicit scoped factory
acceptance is recorded in the final decision below; mount-namespace alias evidence remains Skipped.
Historical `6a6337d` acceptance, Windows blockage and roadmap checkboxes are unchanged.

## Scoped native factory acceptance

The maintainer [explicitly accepted the factory/fence](../docs/NATIVE-ADMISSION-EVIDENCE.md#explicit-scoped-factory-acceptance--2026-10-10)
on 2026-10-10 for tested revision
`4f6924670d4272736984342c8294967650c60edb`, on WSL2 `/dev/sdd` ext4
`data=ordered`. This closes the OHK-BIND-030 factory/fence evidence gate only
within the recorded scope. Documentation merges do not relabel the tested
revision. Earlier Pending statements and supplied logs retain their historical
meaning; this final decision is authoritative. The `6a6337d` provider decision
remains separate and unchanged. Mount-namespace alias evidence remains Skipped;
Windows checkpoint acknowledgment, durable root mutations, application/CLI wiring,
NCN evidence and manual validation remain open. No storage release checkbox changes.

## Complete-owner staging and fake registration tests

The [durable service suite](durable_library_service_tests.cpp) now has 100
numbered checks. Registration tests use real harness-owned source directories
with an independent fake checkpoint store. They cover failed-stage ID rollback,
policy/path/overlap errors, cancellation, all staged artifact failure phases,
old owner at publish/sync, no allocation after publication, reentry, cleanup
warning, exhaustion, stale/protocol/fence/uncertain recovery behavior, shared
payload boundaries and snapshot/PreparedSong lifetime. Captured candidate
mappings/counters are compared with decoded primary bytes and fresh capture.

The [native service suite](native_durable_service_tests.cpp) now has 68 checks.
Experimental registration/reattachment cases cover store/root rejection,
rollback/retry and ID high-water, missing-old repair, reverse-order saved-hint
attachment and guard mapping, same-path drift, unrelated drift, late candidate
replacement, changed/NoChange fencing, uncertainty/fresh recovery, revision/
generation exhaustion, allocation sweeps with FD cleanup, and no allocation
after publication. These assertions add no scoped native mutation acceptance. Both targets still
require OPENHDK_ENABLE_TEST_SEAMS=1 through CMake. Counts remain 28 core /
31 audio-enabled. [Next native staging work](../docs/DURABLE-ROOT-LIFECYCLE-PLAN.md#10-current-slice-31-boundary)
requires fresh affected receipts, pinned execution and scoped acceptance. Historical
receipts and accepted tested revisions are not relabeled.

## Prospective native guard primitive tests

The [binding suite](native_store_binding_tests.cpp) now has 108 numbered checks
(74 previously). New cases verify guard identity sharing, exactly one added FD,
old/candidate lifetimes, exact/beyond payload accounting, duplicate/missing prior
indices, same-path rejection, missing targeted-root repair, unrelated/late root
replacement, candidate replacement, injected mount mismatch, allocation-failure
FD cleanup, 31-to-32 sharing, 32-plus-replacement rejection and stale/foreign epochs.
They use harness-owned paths and the existing eligibility selection: local overlay
bypass is regression coverage, not native ext4 acceptance. Namespace aliases
remain Skipped. Current native service tests exercise experimental registration/reattachment.

There are still 28 core / 31 audio-enabled suites. Existing receipt/verifier
coverage remains separate from new prospective cases, which do not emit a
complete acceptance record. Shared-guard representation changes require fresh
affected native evidence before extending support claims. No historical bundle
is edited. [Service fence integration](../docs/DURABLE-ROOT-LIFECYCLE-PLAN.md#12-experimental-root-service-integration)
now uses a scoped candidate context; new mutation receipts remain pending.


## Durable scan and logical song lifecycle tests

The [fake service suite](durable_library_service_tests.cpp) now has 124 numbered
checks, and the [native service suite](native_durable_service_tests.cpp) has 78.
New cases exercise complete-scan revision/sequence increments, Ready metadata and
override preservation, equal-key invalidation, logical relocation/source
preservation, removal/high-water/rediscovery, retained PreparedSong buffers,
invalid-source diagnostics, cancellation, fake artifact phase faults,
uncertainty/recovery, relocate/remove allocation sweeps and scan save-stage sweeps, exact/beyond
shared staging budgets, old memory during publish/sync and postpublication
allocation bans. Native cases add late original-root drift rejection for scan,
relocate/remove with unchanged primary bytes/memory, RootId error mapping,
pending-context cleanup and FD rollback.

These added cases are regression assertions; the existing receipt verifier does
not declare complete mutation evidence. Fresh bounded mutation receipts, a pinned
native ext4 run and explicit scoped review remain pending. Local overlay bypass
is not ext4 acceptance. Counts remain 28 core / 31 audio-enabled, historical
bundles/accepted revisions stay unchanged, and Windows acknowledgment remains
BLOCKED. [Current integration boundary](../docs/DURABLE-ROOT-LIFECYCLE-PLAN.md#13-experimental-scan-and-logical-song-lifecycle-integration).

Scan allocation injection starts at the existing coordinator BeforeStaging
checkpoint, after filesystem discovery and capture. It does not sweep standard
library traversal allocations: the local libstdc++ recursive directory iterator
terminates on an injected allocation failure inside its constructor. Relocate
and remove are swept from operation entry. These tests do not claim a total
process-memory bound or comprehensive system-library allocation coverage.


## Native mutation receipt matrix

The native service target now has 82 numbered check identifiers plus a
[34-case matrix](NativeMutationReceipts.hpp): five operations times Saved,
write-failure, late-fence drift, uncertainty and late cancellation; targeted
missing-root repair, same-path NoChange/drift, unrelated drift, reciprocal
containment, explicit hint attachment, descriptor peak, equal-key relocation
and remove/rediscovery. Receipts report 69 primary snapshots, 32 bounded admission
traces, 25 bounded provider traces, 45 artifact observations and five allocation
sweeps. All are emitted before harness fixture cleanup. No new CMake suite is
added (28 core / 31 audio-enabled), and all instrumentation remains seams-only.

[verify-native-mutation-receipts.py](verify-native-mutation-receipts.py) checks the
selected matrix. [check-native-mutation-verifier.py](check-native-mutation-verifier.py)
requires a valid service log and rejects ten altered variants. The collector runs
both and records their exits/hashes. Run locally with the native service log:

```sh
python3 tests/verify-native-mutation-receipts.py native_durable_service_tests.run.log
python3 tests/check-native-mutation-verifier.py native_durable_service_tests.run.log
```

Hints/absolute paths and raw hint-bearing checkpoint/artifact wires are omitted
for privacy; numeric IDs in projection rows are detached checkpoint values.
Digest syntax/association and supplied observations are checked, not independent
full-wire reconstruction, source/run provenance or universal durability. Existing
artifact capture remains capped at 256 bytes; oversized captures report EFBIG
and unavailable bytes. Scan allocation injection starts at store staging;
registration adds one owned FD on success, while failure sweeps preserve FDs.
Pending/Skipped remain explicit. New native mutation acceptance requires a fresh
pinned ext4 run, review and scoped maintainer decision; historical bundles stay
unchanged. See [current receipt boundary](../docs/DURABLE-ROOT-LIFECYCLE-PLAN.md#14-bounded-mutation-receipts-and-verifier).

## Pinned WSL2 ext4 mutation evidence

The maintainer supplied a [fresh mutation run](../docs/evidence/native-mutations/2026-10-11-wsl2-ext4/README.md) at exact tested revision
`54f404ae8422499de950f5c96527c9023274a9b6` on WSL2 `/dev/sdd` ext4,
`data=ordered`. All 28 compile/run exits, three verifier exits and overall exit
are zero; filesystem bypass is zero. The rerun verifier matches 34 mutation
cases, 69 primary receipts, 32 admission traces, 25 IO traces, 45 artifact
observations and five allocation sweeps. Ten altered-log variants are rejected.

The artifact cap remains 256 bytes: seven captures are available and 38 report
oversize EFBIG/unavailable; raw mutation bytes are omitted. Supplied observations
are not independent byte/source/run authentication. Scan allocation injection
starts at store staging, not standard-library traversal. Username redaction has
original/published hash records; historical bundles are unchanged.

Fresh execution is now recorded; explicit scoped mutation acceptance remains
Pending. Earlier preparation statements describe the pre-run sequence. This
documentation does not advance `6a6337d` or `4f69246` acceptance. Namespace aliases
remain Skipped; Windows checkpoint acknowledgment remains BLOCKED. App/CLI,
NCN and manual validation remain open, with no storage release checkbox change.

## Scoped native mutation acceptance

The maintainer confirmed the reviewed native mutation evidence on
2026-10-11 at 11:25:58 +07:00. The [decision record](../docs/NATIVE-ADMISSION-EVIDENCE.md#explicit-scoped-native-mutation-acceptance)
accepts exact tested revision `54f404ae8422499de950f5c96527c9023274a9b6`
on WSL2 `/dev/sdd` ext4 with `data=ordered`, within the supplied selected
transaction observations. This closes the OHK-DURABLE-ROOT-030 slice 3
native mutation evidence-and-acceptance gate only for that revision/environment.
Documentation merge `001732d` is not a tested runtime revision; later docs
commits do not advance the accepted tested revision. Historical provider
`6a6337d` and factory/fence `4f69246` decisions remain separate and unchanged.

Earlier Pending text records the pre-decision sequence; supplied bundles,
READMEs, logs, hashes and verification/redaction records stay unchanged.
Mutation bytes omitted from receipts are not independently hashed; 38 oversize
artifact observations remain EFBIG/unavailable. Scan allocation coverage starts
at store staging. Namespace aliases remain Skipped. This does not approve
bare-metal Linux, Windows/NTFS, DrvFS/cross-OS, hostile-writer isolation, atomic
pathname CAS or power-loss recovery. Windows checkpoint acknowledgment remains
BLOCKED. App/CLI wiring, NCN evidence and manual validation remain open; the
storage release checkbox stays unchecked. Next work is slice 4 app/CLI wiring.

## Library CLI orchestration proposal (historical stage)

[OHK-CLI-030](../docs/LIBRARY-CLI-ORCHESTRATION-PLAN.md) originally proposed a
hardware-free management session, service-owned preparation query and later
acknowledged-generation playback controller. This paragraph records the proposal
stage; those components and the first audio-enabled integration slice are now
implemented. Synthetic checks do not accept native/manual playback. Restored roots
remain unattached until explicit user action; Windows checkpoint acknowledgment
remains BLOCKED. Fresh affected application evidence, NCN and manual validation
remain open; historical scoped runtime acceptances do not advance.

## Pure library CLI models

[OHK-CLI-030](../docs/LIBRARY-CLI-ORCHESTRATION-PLAN.md) is accepted. The first slice supplies pure
entry/command parsing, numeric selectors and bounded JSON Lines output models.
It dispatches no service operation, constructs no live ID, reads no filesystem
and creates no executable/session. Preparation and playback remain later slices.
The new hardware-free target raises current counts to 29 core / 32 audio-enabled;
earlier slice counts describe their historical development stages. Windows
checkpoint acknowledgment remains BLOCKED; native evidence labels and release
checkboxes do not advance. Application execution, NCN and manual validation
remain open.

`library-cli-models` checks pure entry/command syntax, 16 KiB line draining,
8-token/4 KiB bounds, byte-preserving quoted text, absent/empty override semantics,
policy/selector ranges, JSON escape/native byte representation, complete output
limits and allocation-failure withholding. It also checks the pure known-commit
receipt-loss classification; no actual service mutation/output stream is invoked.
The suite requires no test-seam define, audio dependency or native filesystem.

## Library management session (OHK-CLI-030 step 2)

`OpenHDKLibrary` now builds independently of the audio proof of concept. The
serialized owning session dispatches register, reattach, scan, relocate, remove
and whole-record overrides through `DurableLibraryService`. Decimal selectors
resolve only against the acquired snapshot; diagnostic ID accessors cannot
construct identity authority. Read-only `rootAttached` and `queryBudget` expose
neither native paths nor mutable owner/store state. Open restores unattached
roots; no saved hint attachment or automatic scan occurs.

Complete JSON Lines batches precede delivery. Confirmed Saved/NoChange is
recorded before allocating response storage; output failure stops dispatch with
exit 74 and never retries a mutation. RecoveryRequired ends the session with
exit 76. Listing has bounded pages, owning display resolution and byte-safe
locator output; root hints, tickets, pointers and raw checkpoint bytes are not
printed. Error records preserve nested families; signed native errors are decimal
strings. A conservative application peak reserve shares the service payload
ledger. These logical bounds do not claim a total process-memory limit.

The fake-provider session suite adds 52 numbered checks; the executable help
smoke test also runs with audio OFF. Current CTest counts are 31 core / 34
audio-enabled (30 hardware-free C++ suites plus the library help smoke test).
Preparation queries and playback are later slices. Native CLI execution remains
experimental pending fresh pinned session evidence and explicit scoped review;
prior acceptances at `6a6337d`, `4f69246` and `54f404a` do not advance. Windows
checkpoint acknowledgment remains BLOCKED; NCN, manual validation and release
checkboxes remain open.

`library-cli-session` uses an independent two-slot fake checkpoint provider and
harness-owned song fixtures. It covers every management verb, revision-only scan,
selector rejection before save, metadata absence/literal/empty rules, owning
historical snapshots, logical relocation/removal and rediscovery, explicit hint
attachment, Busy and uncertainty/recovery, cleanup warning, bounded line draining,
EOF/quit, shared-budget rejection, and failed input/output. A publication-time
allocation ban proves response failure retains a known commit without retry.
The suite requires `OPENHDK_ENABLE_TEST_SEAMS=1`, set on its CMake target only;
`OpenHDKLibrary` itself is compiled without seams. This fake-provider execution
is regression coverage, not native CLI filesystem acceptance.

## Service-owned preparation (OHK-CLI-030 step 3)

`DurableLibraryService::prepare` now accepts a typed SongId, bounded options and
cancellation through the same serialized reentry/state guard. It acquires only
its current owned snapshot and reuses `SongDiscovery::prepare`; caller snapshots,
root mappings and store expectations cannot be injected. The result owns
PreparedSong or a structured service/preparation error, without a commit receipt.
No catalog revision, sequence, primary bytes or publication changes for this query.

Authority policy: provider directory/lease and all retained native root guards
are checked before and after preparation. Failed authority validation requires
RecoveryRequired and withholds any prepared value; a late content error is retained
as nested preparation detail. SourceChanged/InvalidLyrics/NotReady/unattached-root
errors remain preparation failures while authority is valid and service stays
Ready. Content changes do not become storage failures. Closed/RecoveryRequired
and Busy reject before owner access. These checks are consistency observations,
not an atomic filesystem snapshot or primary checkpoint freshness proof.

The application/provider/admission/baseline/root-path retention shares the same
bounded preparation allowance. Discovery charges catalog strings and saved hints
once; its new caller retention parameter defaults to zero for existing callers.
`prepare SONG` emits a bounded JSON summary with snapshot revision, event count,
last event time, lyric profile/track/cue count, effective encoding, source byte
count and display title/artist/origin. `last_event_us` is not audio duration.
Summary has no Saved acknowledgment, playback start, raw payload or native hint.
Nested preparation/SMF/track/lyric diagnostics preserve their own families.

Current counts are 32 core / 35 audio-enabled (31 hardware-free C++ suites plus
library help). The new preparation suite covers 35 numbered checks, allocation
sweeps and Linux root replacement under the private filesystem bypass locally;
Windows runs the hardware-free subset. The collector's seams list includes both
new CLI/preparation targets so the forthcoming pinned collector run can build
all current targets. This does not supply CLI session acceptance receipts.
Next: fresh pinned management/preparation session evidence and scoped review,
then playback controller. Prior `6a6337d`/`4f69246`/`54f404a` acceptances and bundles
stay unchanged; Windows checkpoint acknowledgment is BLOCKED and NCN/manual/
release gates stay open.

## Management/preparation CLI evidence instrumentation

[CLI evidence procedure](../docs/LIBRARY-CLI-EVIDENCE.md) adds a bounded 37-case production-executable
subprocess matrix, six child-exit receipts, before/after checkpoint observations,
a strict verifier and 15 supplied-log negative checks. The pinned collector builds
OpenHDKLibrary without seams and runs the matrix on an eligible ext4 parent.
Eight offline Python checks run in Linux CI; CTest counts remain 32 core / 35 audio.
This adds no production behavior, native acceptance, playback or release checkbox.
Fresh pinned WSL2 ext4 execution and explicit scoped review are Pending. Earlier
`6a6337d` / `4f69246` / `54f404a` acceptances and bundles do not advance; Windows
checkpoint acknowledgment remains BLOCKED, and NCN/manual gates remain open.

## NCN24 format evidence review

The [format evidence supplement](../docs/NCN24-FORMAT-EVIDENCE-SUPPLEMENT.md) records
competing cursor timing, line/scalar mapping and high-byte FF behavior, with
independently authored byte vectors and a lawful bundle comparison procedure.
Source inspection and arithmetic do not close the NCN evidence gate. The
NCN24 target includes accepted first-SUB EOF; representative observations and explicit
review remain Pending. No NCN parser/normalizer, suite-count, release-checkbox
or historical native acceptance change is introduced.

Offline [NCN24 fixture tools](fixtures/ncn24/README.md) now check 69 original synthetic cases,
with an independent four-SMF/three-TIS-620 API probe and negative/boundary tests.
This is fixture tooling, not an application NCN parser/normalizer. All receipts
keep representative observations and NCN acceptance Pending. Existing CTest
counts, roadmap checkboxes, evidence bundles and native acceptances are unchanged.

The [NCN representative observation procedure](../docs/NCN-REPRESENTATIVE-OBSERVATION-RUNBOOK.md) now has a bounded
read-only member inventory and supplied-record verifier. They expose no private
lyrics/paths and do not run playback or validate the full tempo map. Tests use
synthetic members/mock records only. Actual representative execution, mapping/
sentinel review and NCN acceptance remain Pending; no runtime, CTest-count,
checkbox, historical evidence bundle or native acceptance change is introduced.

## Legacy edge hypotheses

The [legacy edge analysis](../docs/NCN-LEGACY-EDGE-ANALYSIS.md) examines pinned reader handling
of SUB text, incomplete CUR words and decreasing cursor positions.
Four original edge vectors and 32 offline tests apply first-SUB EOF and optional
terminal FF framing while preserving non-FF odd-tail/decreasing rejection and
checking EOF/framing/limit/schema boundaries.
No measured mapping, authenticated playback or normalizer acceptance is claimed.
NCN evidence remains OPEN; no contract, CTest-count or checkbox change.

## Accepted NCN24 SUB logical EOF tooling

The [first-SUB EOF amendment](../docs/NCN24-TERMINAL-SUB-PROPOSAL.md) was accepted with PR #94.
The planning contract and offline fixture/checker/inventory tools now select
logical text before the first `1A`. They retain complete raw members for hashes,
source caps and accounting. Shared text decoders remain unchanged. The separate
CUR framing amendment is described below.
The earlier SUB-only migration used schema 2. Current fixture/receipts and
inventory use schema 3, including the separate accepted CUR amendment.
The observation verifier
labels schema 1 as historical strict-C0 interpretation and schema 2 as first-SUB
EOF; it does not reinterpret or backfill old receipts.
No application NCN parser/normalizer exists. NCN evidence remains OPEN, with
runtime counts, release checkboxes, historical bundles and storage acceptance
revisions unchanged.

## Accepted NCN24 optional CUR terminator tooling

The [OHK-NCN-CUR-030 amendment](../docs/NCN24-CUR-FRAMING-PROPOSAL.md) was accepted with PR #96.
The contract and schema-3 offline tools now select complete little-endian words
at physical EOF, or exclude exactly one terminal FF byte from odd-length CUR.
Non-FF odd tails still reject. Complete FF words, padding/trailer hypotheses,
ordering, mapping and timing rules remain unchanged. Raw member hashes, caps
and shared accounting include the marker. The manifest has 69 cases (51 prior
plus 18 CUR framing vectors); current checker input rejects schemas 1 and 2.
Inventory keeps raw trailing-byte statistics beside framing provenance.
The observation record remains schema 1; its verifier labels inventory schemas
1/2 as Unframed CUR and schema 3 as OptionalTerminalFF, with arithmetic checks.
Preserve submitted receipts and rerun samples into new files. No application
NCN normalizer exists; the evidence gate remains OPEN. Runtime/suite counts,
checkboxes, historical bundles and storage acceptances do not change. Windows
checkpoint acknowledgment remains BLOCKED.
