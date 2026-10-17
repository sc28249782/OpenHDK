# OpenHDK roadmap

OpenHDK versioning begins with the `0.1.0` source-release baseline. It is
independent from HandyKaraoke 3.0.0-alpha.1, which remains the
compatibility/recovery release.

## 0.1.0 — released source baseline

- [x] Build CMake skeleton and Windows CI.
- [x] Define backend boundary and dependency policy.
- [x] Add a FluidSynth + miniaudio proof of concept for a known MIDI/SF2 fixture.
- [x] Add non-invasive audio-device enumeration and endpoint diagnostics.
- [x] Manually validate Windows device output with an external MIDI/SoundFont
  pair; no private asset was added to the repository.
- Do not port the HandyKaraoke UI or user song folders yet.
- [x] Select an output endpoint by the index shown by --list-devices.
- [x] Reject unavailable endpoint indices and --device with --no-device.
- [x] Set initial FluidSynth output gain with --volume or mute it with --mute.
- [x] Apply launch-time per-MIDI-channel gain trims while preserving SMF CC7
  automation and the GM default channel volume.
- [x] Parse SMF formats 0 and 1 deterministically from bounded byte input,
  including MThd/MTrk validation and malformed-input coverage.
- [x] Decode supported SMF events with bounded malformed-input handling.
- [x] Compile an ordered, tempo-aware SMF timeline with checked arithmetic.
- [x] Add a deterministic PlaybackSession render boundary.
- [x] Dispatch rendered SMF channel events to an
  abstract MIDI command sink, preserving timeline order without timing or I/O.
- [x] Add read-only per-channel SMF diagnostics for note-on counts, observed
  programs, CC7/CC11 history, optional CC39/CC43 14-bit pairs, and deterministic
  event positions, including an OpenHDK effective-CC7 view after CC121, without
  requiring a SoundFont or audio device.
- [x] Add runtime channel gain, mute, solo, reset, and an interactive Windows
  mixer console without changing source SMF controller automation.

The `v0.1.0` source release includes this completed baseline. Its Windows
audio-device check has passed with external, non-distributable validation
assets. It does not provide a complete user-facing playback application.

## 0.2.0 — released mixer and dynamics baseline

- [x] Specify the velocity-curve, named-preset, and master-limiter contracts
  before implementation.
- [x] Add deterministic velocity curves while preserving source MIDI event data
  and the existing CC7/CC11 channel-volume contract.
- [x] Add named channel mute/solo presets without altering source SMF events.
- [x] Add a real-time-safe master limiter after the runtime mixer.
- [x] Extend hardware-free regression coverage for each mixer and dynamics
  contract, including limiter peak behavior.
- [x] Manually validate Windows output with an external MIDI/SoundFont pair;
  do not commit those private assets.

The maintainer reported successful Windows tests and device listening on
2026-10-06. See [V020-WINDOWS-ACCEPTANCE.md](V020-WINDOWS-ACCEPTANCE.md).
The local revision, clean tree, and asset pair are confirmed. Soft reduced
drum loudness as expected from a global velocity curve; linear remains the
default. The signed `v0.2.0` tag and source-only GitHub Release were published
on 2026-10-06 at commit `0175cac`. The release gates below passed; the
acceptance record includes the final commit checks.

Exit gate: hardware-free tests and both CI workflows pass; limiter behavior is
covered deterministically; Windows output is manually validated; no crash
occurs when a SoundFont or audio device is unavailable.

## 0.3.0 — karaoke library (planned)

- [x] Review and accept [library](KARAOKE-LIBRARY-CONTRACT.md) and
  [lyric timeline](LYRIC-TIMELINE-CONTRACT.md) proposals before implementation.
- [x] Implement pure catalog identity and immutable snapshots.
- [x] Add bounded SMF/KAR discovery, content revision checks, and in-memory
  scan transactions with synthetic fixtures.
- [x] Add root reattachment and source/member metadata before library integration.
  Local SMF/KAR root reattachment is implemented under
  [OHK-ROOT-030](ROOT-REATTACHMENT-CONTRACT.md), including attachment generations,
  transactional invalidation and stale-root checks. Metadata/root policies are connected to scan/preparation under accepted
  [OHK-META-030](CATALOG-METADATA-POLICY-CONTRACT.md), including compact title
  provenance, lyric-ready validation and complete per-call selection overrides.
  Atomic user override replacement and owning display resolution are implemented;
  prepared display retains its acquired overrides and fresh effective metadata.
- [x] Implement pure KAR extraction, FF 05/identified FF 01 selection, explicit
  text encoding, and deterministic media-clock lyric-consumer tests.
  Running-backend publication and the generation-bound observer are implemented;
  application/audio/CLI lyric orchestration remains unfinished below.
- [ ] Close the NCN24 format evidence gate before implementing its bundle
  normalizer; do not claim generic NCN compatibility.
  A [sanitized legacy runtime evidence status](LEGACY-RUNTIME-EVIDENCE-STATUS.md)
  records private startup, handoff, ownership, and stop-acknowledgement probes.
  These probes do not close the NCN24 or Windows checkpoint gates and do not
  import Qt or BASS-family dependencies into OpenHDK core.
- [ ] Select and review persistent storage/schema/recovery and backend
  media-position publication before implementing those adapters. The
  [media-clock handoff contract](MEDIA-CLOCK-HANDOFF-CONTRACT.md) is accepted;
  its atomic cell and FluidSynth publication are implemented, while persistent
  storage and application lyric orchestration remain open. A serialized observer
  binds lyrics to acknowledged generations. Single-song SMF/KAR preparation
  verifies catalog identity/source tokens; application integration is still needed.
  The [accepted persistence contract](CATALOG-PERSISTENCE-CONTRACT.md) defines schema-1
  checkpoints, fresh-owner restore, explicit reattachment and save/recovery.
  The pure schema-1 codec and fresh-owner restore are implemented;
  the detached coordinator has fake-provider protocol tests. Native providers,
  durability and durable-first mutation wiring remain separate acceptance gates;
  codec/restore/protocol alone do not complete the storage checkbox.
  The [accepted native provider plan](NATIVE-CHECKPOINT-PROVIDER-PLAN.md) puts
  Linux ownership/publication tests first and separates Windows API/synchronization
  review from native acceptance. Linux ownership/lease tests are implemented;
  an experimental Linux provider now implements staging/publication/sync/reconciliation.
  [Scoped Linux acceptance](NATIVE-LINUX-CHECKPOINT-EVIDENCE.md) is recorded for
  provider/test revision `6a6337d` on WSL2 `/dev/sdd` ext4 after PR #65 review and
  explicit maintainer confirmation. It does not approve broader environments or
  power-loss guarantees. The Windows synchronization gate remains BLOCKED;
  Live checkpoint capture now owns matching snapshot/counter/root context;
  remaining durable mutations and fresh service evidence prevent completing this checkbox.
  The [accepted durable service contract](DURABLE-LIBRARY-SERVICE-CONTRACT.md)
  defines exclusive owner/store admission and override-first staging. The service
  primitive and fake-provider lifecycle/commit tests are implemented; construction
  has seam-only fake construction and experimental owning Linux factories.
  Native service evidence and other durable mutations remain separate slices. This does not complete the storage checkbox.
- [ ] Integrate library preparation and lyric observation without changing
  released MIDI, mixer, or limiter semantics; validate Windows playback.
- Research staged runtime layout and installers separately; this proposal
  does not authorize a binary release or select a UI framework.

These are proposed contracts, not an accepted implementation milestone. Any
future source port requires provenance and attribution under
`MIGRATION-BOUNDARY.md`; copying the legacy database/UI is not implied.

Exit gate: proposal and adapter designs accepted; NCN24 evidence complete;
identity, discovery, rollback, encoding, selection, and timing regressions pass
without hardware; both CI workflows pass; identified Windows playback/lyric
checks pass. If a gate remains open, do not claim full 0.3.0 support.

## 0.4.0 — multi-SoundFont synthesis

- Specify a backend-independent SoundFont library, identity, load/unload, and
  per-SoundFont gain contract before implementation.
- Add deterministic GM program, bank, and drum mappings to SoundFont layers.
- Add an instrument-family classifier only as a documented, testable mapping;
  it MUST NOT replace source MIDI events or controller state.
- Add headless coverage for mapping selection, missing SoundFonts, and fallback
  errors without requiring a physical audio device.

Exit gate: mappings and SoundFont lifecycle are deterministic and covered by
hardware-free tests; no BASS-family API or asset enters the repository.

## 0.5.0 — mixer and bus engine

Use [SoundCraft source lessons](SOUNDCRAFT-REFERENCE.md) alongside the Buai mixer
feature reference when proposing contracts. These references do not select code,
dependencies or detailed semantics, and do not change 0.3.0 work order.

- Specify backend-independent mixer strips, instrument groups, named groups,
  mute/solo/gain/pan, and a directed bus-routing graph before implementation.
- Add configurable buses incrementally, beginning with a small deterministic
  graph rather than a fixed legacy stream count.
- Specify a versioned Mixer Profile with bounded validation, resource identity,
  migration, and transactional load behavior. Keep it distinct from v0.2
  flags-only presets; enable fields only after their feature contracts exist.
- Specify a separate master-gain stage before master DSP/limiting, including
  migration from the existing synth gain without applying gain twice.
- Preserve the real-time callback boundary while applying mixer changes.
  Specify off-callback graph preparation and retirement, separate immutable
  topology from DSP state, and define mono pan versus stereo balance.
- Define processor latency units and bounded compensation preparation before
  adding latency-bearing routes; full compensation may remain a later slice.

Exit gate: graph validity, routing, mute/solo semantics, and state changes have
deterministic regression coverage; Windows output is manually validated.

## 0.6.0 — DSP and effects

Study the pinned [DSP reference](SOUNDCRAFT-REFERENCE.md). Define processor
lifecycle, parameter validation/smoothing, bypass, latency/tail and metering
placement before implementation. Use independent numerical fixtures and explicit
realtime/offline comparison tolerances; upstream feature names prove no standard
conformance or callback safety.

- Specify insert, send, return, parameter, bypass, and preset contracts before
  adding effects.
- Use the 0.2.0 master limiter as the foundation, then add parametric EQ,
  compressor, and reverb/chorus in that order. Add per-bus inserts and
  send/return paths only after those contracts are stable.
- Keep an optional VST3-host feasibility study isolated from the playback
  process and outside the default product path.

## 0.7.0 — audio routing

- Add bus-to-output-channel routing for one multi-channel audio endpoint.
- Keep PCM buses/endpoints distinct from MIDI destinations in the routing
  model. External MIDI output remains deferred until note ownership,
  timing, device identity, fallback, and route-change cleanup are specified.
- Define channel-layout, endpoint-capability, and safe-fallback behavior.
- Defer multiple physical audio devices until a separate clock-domain,
  buffering, resampling, and drift policy is accepted.

## 0.8.0 — mixer-focused UI

- Select a UI framework only after the mixer and routing contracts are stable.
- Add mixer strips, meters, faders, group/bus routing, and System/Dark/Light
  themes through design tokens rather than copied UI code.
- Show named bus destinations on mixer strips without requiring a dialog;
  expose master volume and route/fallback state with accessible labels.
- Consider a mixer-only companion mode only after the required MIDI/IPC input
  contract has been designed separately.

## Scope boundaries

The future milestones above are plans, not implemented release guarantees.
KAR/NCN work requires the separate 0.3.0 contracts; Qt or another UI framework
requires a later reviewed selection.

The released v0.2.0 SMF parser, timeline, PlaybackSession, and dispatcher
remain limited to SMF formats 0 and 1. Qt migration, KAR/NCN parsing or
playback, HNK/HNK3 compatibility, VST2/VST3 integration, and physical MIDI
hardware I/O are outside that released baseline. BASS-family artifacts remain
excluded from all milestones. Audio callbacks must not perform
file I/O, allocation, parsing, or UI work.

## Later work

- HNK3 authoring/container design.
- Explicit opt-in drum-tuning playback policy with PreserveSource as the
  proposed default; specify RPN/NRPN state and destination scope before code.
- Physical MIDI hardware regression rig, including busy/disconnected devices,
  same-device selection, reconnection, and note cleanup on route changes.
- External MIDI input and mixer-sidecar mode.
- Cross-platform work after Windows behaviour is stable.

Native durable service slice 2 now implements the accepted
[binding/admission primitives](NATIVE-SERVICE-BINDING-PLAN.md): lease-epoch
capability and bounded same-mount root ancestry. The final prepublication fence
and experimental native factory are implemented; fresh pinned integration evidence remains pending.
The accepted detached Linux revision and Windows BLOCKED gate remain unchanged.

The [experimental factory boundary](NATIVE-SERVICE-BINDING-PLAN.md#11-experimental-factory-and-final-admission-fence)
records explicit Create/Open, private final and NoChange admission checks, and
shared binding/guard accounting. Future durable root lifecycle and application
wiring remain separate slices; scoped factory acceptance is recorded below; later mutation evidence remains separate.

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

## Durable root lifecycle slice 3.1

[OHK-DURABLE-ROOT-030](DURABLE-ROOT-LIFECYCLE-PLAN.md#10-current-slice-31-boundary)
is accepted, with private complete-owner staging and fake-provider registration
implemented. Shared guards, operation-aware native registration/reattachment and
scan/logical song relocation/removal are implemented experimentally.
Next: affected mutation receipts, fresh pinned native execution and scoped acceptance. This does
not complete storage or application/CLI work; Windows, NCN and manual gates stay
open, and existing roadmap completion checkboxes remain unchanged.

## Durable root lifecycle prospective primitive

[Slice 3.2 guard preparation](DURABLE-ROOT-LIFECYCLE-PLAN.md#11-current-prospective-guard-primitive)
now shares unchanged retained guards and privately stages one new/replacement
guard with bounded rollback coexistence. Owned RootId/generation mapping,
operation-aware service fencing and experimental native registration/targeted
reattachment are now implemented. Scan/song lifecycle and fresh scoped mutation
receipts/run/acceptance remain pending. No storage checkbox changes; Windows checkpoint
acknowledgment, NCN, app/CLI and manual validation remain separate gates.


## Durable mutation receipt instrumentation

The [34-case receipt matrix](DURABLE-ROOT-LIFECYCLE-PLAN.md#14-bounded-mutation-receipts-and-verifier)
and positive/negative verifiers are implemented. Next: fresh pinned WSL2 ext4
execution with no eligibility bypass, evidence review and explicit scoped
acceptance, then application/CLI wiring. Historical acceptances and storage
checkbox status do not advance. Windows/NCN/manual gates remain open.

## Pinned WSL2 ext4 mutation evidence

The maintainer supplied a [fresh mutation run](evidence/native-mutations/2026-10-11-wsl2-ext4/README.md) at exact tested revision
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
2026-10-11 at 11:25:58 +07:00. The [decision record](NATIVE-ADMISSION-EVIDENCE.md#explicit-scoped-native-mutation-acceptance)
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

## Library CLI orchestration proposal

[OHK-CLI-030](LIBRARY-CLI-ORCHESTRATION-PLAN.md) proposes a bounded hardware-free
management session, service-owned preparation query and later acknowledged-generation
playback controller. Status is Proposed; review and acceptance precede implementation.
It adds no runtime CLI, tests, dependencies or completed checkbox. Restored roots
remain unattached until explicit user action; Windows checkpoint acknowledgment
remains BLOCKED. Fresh affected application evidence, NCN and manual validation
remain open; historical scoped runtime acceptances do not advance.

## Pure library CLI models

[OHK-CLI-030](LIBRARY-CLI-ORCHESTRATION-PLAN.md) is accepted. The first slice supplies pure
entry/command parsing, numeric selectors and bounded JSON Lines output models.
It dispatches no service operation, constructs no live ID, reads no filesystem
and creates no executable/session. Preparation and playback remain later slices.
The new hardware-free target raises current counts to 29 core / 32 audio-enabled;
earlier slice counts describe their historical development stages. Windows
checkpoint acknowledgment remains BLOCKED; native evidence labels and release
checkboxes do not advance. Application execution, NCN and manual validation
remain open.

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

[CLI evidence procedure](LIBRARY-CLI-EVIDENCE.md) adds a bounded 37-case production-executable
subprocess matrix, six child-exit receipts, before/after checkpoint observations,
a strict verifier and 15 supplied-log negative checks. The pinned collector builds
OpenHDKLibrary without seams and runs the matrix on an eligible ext4 parent.
Eight offline Python checks run in Linux CI; CTest counts remain 32 core / 35 audio.
This adds no production behavior, native acceptance, playback or release checkbox.
A [private pinned WSL2 ext4 run](LIBRARY-CLI-EVIDENCE.md#recorded-private-wsl2-ext4-run--2026-10-16)
at `80b673ee5e96a423cbd5974f3b57c34a6fbf78ce` recorded passing
management/preparation CLI observations. The redacted derived summary omits the
raw run log and cannot be rerun through the stock receipt verifier. The maintainer
[explicitly accepted step 4](LIBRARY-CLI-EVIDENCE.md#explicit-scoped-cli-acceptance--2026-10-17)
on 2026-10-17 only for that tested revision and recorded WSL2 ext4 scope, with
sanitizer setting 0 and eligibility bypass off. Next: the separate playback
controller in step 5, followed by affected native/manual evidence. No playback
acceptance or release checkbox changes follow from this decision. Earlier
`6a6337d` / `4f69246` / `54f404a` acceptances and bundles do not advance; Windows
checkpoint acknowledgment remains BLOCKED, and NCN/manual gates remain open.

## NCN24 format evidence review

The [format evidence supplement](NCN24-FORMAT-EVIDENCE-SUPPLEMENT.md) records
competing cursor timing, line/scalar mapping and high-byte FF behavior, with
independently authored byte vectors and a lawful bundle comparison procedure.
Source inspection and arithmetic do not close the NCN evidence gate. The
NCN24 target includes accepted first-SUB EOF; representative observations and explicit
review remain Pending. No NCN parser/normalizer, suite-count, release-checkbox
or historical native acceptance change is introduced.

Offline [NCN24 fixture tools](../tests/fixtures/ncn24/README.md) now check 69 original synthetic cases,
with an independent four-SMF/three-TIS-620 API probe and negative/boundary tests.
This is fixture tooling, not an application NCN parser/normalizer. All receipts
keep representative observations and NCN acceptance Pending. Existing CTest
counts, roadmap checkboxes, evidence bundles and native acceptances are unchanged.

The [NCN representative observation procedure](NCN-REPRESENTATIVE-OBSERVATION-RUNBOOK.md) now has a bounded
read-only member inventory and supplied-record verifier. They expose no private
lyrics/paths and do not run playback or validate the full tempo map. Tests use
synthetic members/mock records only. Actual representative execution, mapping/
sentinel review and NCN acceptance remain Pending; no runtime, CTest-count,
checkbox, historical evidence bundle or native acceptance change is introduced.

## Accepted NCN24 SUB logical EOF tooling

The [first-SUB EOF amendment](NCN24-TERMINAL-SUB-PROPOSAL.md) was accepted with PR #94.
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

The [OHK-NCN-CUR-030 amendment](NCN24-CUR-FRAMING-PROPOSAL.md) was accepted with PR #96.
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

## First playback controller slice (OHK-CLI-030 step 5)

The [step 5 plan](LIBRARY-CLI-ORCHESTRATION-PLAN.md) now has its first
implemented slice: a serialized control-path `app/PlaybackController` that
retains an owning `PreparedSong` with its snapshot revision identity, starts
through the existing `AudioBackend` acknowledged-generation interface, and binds
`LyricClockObserver` only with the admitted start generation. A start rejected
while active preserves the current binding and playback untouched; idle starts
clear the observer before the backend start; zero or non-monotonic
acknowledgements and failed binds stop the observer before the backend stop
and release the prepared handle once the backend stop confirms; an unconfirmed
stop (false return or exception) keeps the prepared handle, keeps library
mutations excluded, surfaces the backend failure and allows an explicit stop()
retry, with the destructor attempting the ordered stop once more and then the
backend's noexcept shutdown(). Terminal clock phases keep the controller gated
until an explicit stop, and same-backend generation reuse and Exhausted
replacement rules are documented at the controller seam. `pollDisplay()`
performs one committed clock read per call on the control thread and follows
the existing observer phase contract. `libraryMutationAllowed()` is the mutation/play
exclusion boundary for later CLI wiring; `LibraryCliSession` and application
entry points are unchanged in this slice. Tests are hardware-free with a fake
backend, the real publication cell and synthetic in-memory SMF/KAR fixtures;
the new target raises current counts to 33 core / 36 audio-enabled. No native or
manual playback evidence is claimed by this slice; affected evidence, NCN and
manual validation remain open, and the Windows checkpoint acknowledgment stays
BLOCKED.

## Audio-enabled library session slice (OHK-CLI-030 step 5 slice 2)

The step 5 integration slice wires the first production entrypoint: an
audio-gated `OpenHDKLibraryAudio` executable that owns the durable service, the
guarded management session, the concrete FluidSynth backend and the playback
controller in a lifetime order where the backend outlives the controller. The
management session gained an optional mutation-admission guard evaluated at its
dispatch boundary: mutating verbs are rejected with a structured PlaybackActive
error while playback is active or a backend stop is unconfirmed, query verbs
remain available, and the absent guard keeps the parser-only OpenHDKLibrary
behavior unchanged. The audio entry supports play SONG-ID, stop and status
through the controller's acknowledged-generation path, reports backend
refusals, throwing starts and unconfirmed stops as errors instead of success,
fails clearly when backend initialization fails, and stops the controller before
closing the service on session exit. The audio loop preserves the management
reader's bounded-line grammar error and reports an unconfirmed final stop with
a nonzero exit. Tests are hardware-free with a shared fake backend, a real
test-provider service and synthetic in-memory SMF/KAR fixtures, including final
line, oversized line, terminal quit and output loss checks; CTest target counts
rise to 34 core / 38 audio-enabled. Real FluidSynth playback against a prepared
library and the affected native/manual evidence for this entry remain missing,
so step 5 is not
complete. Windows checkpoint acknowledgment remains BLOCKED and NCN evidence
remains OPEN.
