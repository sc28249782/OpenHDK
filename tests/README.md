# Test coverage and failure diagnostics

The hardware-free configuration registers twenty-six CTest suites: catalog checkpoint codec/restore/store protocol,
parser fixtures, MIDI diagnostics CLI, runtime mixer console, velocity curves, mixer presets,
stereo peak limiter, song catalog core, filesystem discovery, lyric text
decoding, KAR lyric extraction, media-clock consumption, media-clock
publication, generation-bound lyric observation, single-song preparation,
root reattachment, pure catalog metadata/policy models, and catalog metadata
scan/preparation integration, and override/display transactions, plus durable override service protocol.
The audio-enabled test configuration adds FluidSynth headless
playback, CLI help, and diagnostics error cases, for twenty-nine suites.
These counts describe the development tree; v0.2.0 has six/nine respectively.
Linux core CI runs twenty-six hardware-free suites; Windows bootstrap runs all twenty-nine.

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
registered alongside `catalog-checkpoint-restore` (twenty-six core / twenty-nine
audio-enabled); native stores and durable-first wiring remain pending.

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
complete the storage release gate. There are 26 core / 29 audio-enabled suites.

## Planned native checkpoint provider evidence

The [accepted native provider plan](../docs/NATIVE-CHECKPOINT-PROVIDER-PLAN.md)
records an API/fault/interruption matrix and per-platform evidence fields.
The ownership primitive has a dedicated suite; the experimental provider adds
publication regression tests without native acceptance claims. The baseline is
26 core / 29 audio-enabled suites. Fake-provider successes do not prove native
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
check UnsupportedStorage without filesystem access. This is 26 core / 29 audio
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
There are 26 core / 29 audio-enabled suites. The six seam targets require
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
defines slice 5 requirements. The new durable-library-service target has 64
numbered checks using an independent two-slot fake namespace. Tests observe old
memory during publication and require confirmed
sync before the memory swap. They cover baseline admission, complete override
staging, rollback, no-op, cleanup warnings, uncertainty/recovery, allocation
prohibition after publication and shared-ledger boundaries. Native binding and
reciprocal store/root containment need separate reviewed integration/evidence.
The target requires OPENHDK_ENABLE_TEST_SEAMS=1, configured only for this target
in CMake and included in the collector seam list. Factories accept only the test
provider category; they cannot admit a native Linux provider. Current counts are
26 core / 29 audio-enabled. Historical provider receipts remain unchanged.
