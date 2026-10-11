# Documentation index

## Current baseline and authority

OpenHDK v0.2.0 was released as source-only on 2026-10-06. The signed tag points
to `0175cac739ea7100e7e298a5c4527c3175fc0323`. This index describes that
baseline and the documentation reviewed after publication. It does not add
features or replace a contract.

[SPECIFICATION.md](SPECIFICATION.md) is the normative authority for implemented
behavior. Contract references and architecture explain it. When descriptions
conflict, correct the affected documents together. The roadmap and mixer
feature reference describe future work only. Licensing and dependency rules
continue to apply independently of feature plans.

## Document map

| Document | Role |
| --- | --- |
| [README](../README.md) | Project status, quick start, and current CLI capabilities. |
| [SPECIFICATION](SPECIFICATION.md) | Normative implemented playback contract. |
| [ARCHITECTURE](ARCHITECTURE.md) | Component boundaries and real-time rendering rules. |
| [PLAYBACK-SESSION-CONTRACT](PLAYBACK-SESSION-CONTRACT.md) | Frame clock, state transitions, dispatch, and completion. |
| [MIDI-CHANNEL-CONTROLLERS](MIDI-CHANNEL-CONTROLLERS.md) | Source controllers, runtime gain, resets, and diagnostics interpretation. |
| [MIXER-DYNAMICS-CONTRACT](MIXER-DYNAMICS-CONTRACT.md) | Implemented velocity curves, presets, and peak limiter. |
| [KARAOKE-LIBRARY-CONTRACT](KARAOKE-LIBRARY-CONTRACT.md) | Accepted 0.3.0 contract; catalog/discovery/preparation slices implemented, full services gated. |
| [CATALOG-METADATA-POLICY-CONTRACT](CATALOG-METADATA-POLICY-CONTRACT.md) | Accepted member/metadata and root-policy contract; models, scan/preparation and override/display transactions implemented. |
| [CATALOG-PERSISTENCE-CONTRACT](CATALOG-PERSISTENCE-CONTRACT.md) | Accepted schema-1 contract with pure codec, fresh-owner restore and fake-provider store protocol implemented; scoped Linux evidence accepted and live capture implemented; Windows and durable-first wiring pending. |
| [NATIVE-ADMISSION-EVIDENCE](NATIVE-ADMISSION-EVIDENCE.md) | Bounded admission receipts/verifier implemented; pinned WSL2 ext4 run and scoped factory acceptance recorded for `4f69246`. |
| [NATIVE-SERVICE-BINDING-PLAN](NATIVE-SERVICE-BINDING-PLAN.md) | Accepted lease-epoch/ancestry plan; private binding/guards and experimental Linux factory/fence implemented; scoped acceptance recorded for `4f69246`. |
| [DURABLE-LIBRARY-SERVICE-CONTRACT](DURABLE-LIBRARY-SERVICE-CONTRACT.md) | Accepted exclusive service/baseline contract; override primitive, fake tests and experimental Linux factory/fence implemented and scoped acceptance recorded; all five mutations implemented with scoped acceptance for `54f404a`; application wiring pending. |
| [LIBRARY-CLI-ORCHESTRATION-PLAN](LIBRARY-CLI-ORCHESTRATION-PLAN.md) | Accepted bounded management/preparation/playback plan; pure command/output models implemented, runtime session pending. |
| [DURABLE-ROOT-LIFECYCLE-PLAN](DURABLE-ROOT-LIFECYCLE-PLAN.md) | Accepted complete-owner staging/repair plan; shared guards and experimental operation-aware register/reattach integration implemented; experimental durable scan/logical relocate/remove implemented; 34-case bounded mutation receipts/verifier implemented; pinned native execution/scoped acceptance recorded for `54f404a`; app/CLI pending. |
| [NATIVE-LINUX-CHECKPOINT-EVIDENCE](NATIVE-LINUX-CHECKPOINT-EVIDENCE.md) | Scoped acceptance recorded for 6a6337d on WSL2 /dev/sdd ext4; evidence bundles retained. |
| [NATIVE-CHECKPOINT-PROVIDER-PLAN](NATIVE-CHECKPOINT-PROVIDER-PLAN.md) | Accepted API/evidence plan; Linux lease and experimental provider implemented; scoped Linux acceptance recorded; Windows synchronization BLOCKED. |
| [ROOT-REATTACHMENT-CONTRACT](ROOT-REATTACHMENT-CONTRACT.md) | Accepted attachment-generation/transaction rules; local SMF/KAR reattachment implemented, CLI/storage gated. |
| [LYRIC-TIMELINE-CONTRACT](LYRIC-TIMELINE-CONTRACT.md) | Accepted KAR/media timing contract; extraction/consumer and backend observation implemented; application/NCN24 gated. |
| [NCN24-FORMAT-EVIDENCE-SUPPLEMENT](NCN24-FORMAT-EVIDENCE-SUPPLEMENT.md) | Reviewed source comparison with offline synthetic fixture/checker tools; timing/mapping/high-byte corpus review Pending, NCN gate OPEN. |
| [NCN24 fixture tools](../tests/fixtures/ncn24/README.md) | Bounded 33-case original fixture checker/export and SMF/text cross-probe; synthetic verification only, no NCN acceptance. |
| [MEDIA-CLOCK-HANDOFF-CONTRACT](MEDIA-CLOCK-HANDOFF-CONTRACT.md) | Accepted backend-clock handoff; FluidSynth publication implemented, generation-bound observer implemented; application integration pending. |
| [POC-RUNBOOK](POC-RUNBOOK.md) | Windows build, diagnostics, playback, and console operations. |
| [Test README](../tests/README.md) | Test scope and failure diagnostics. |
| [V020-WINDOWS-ACCEPTANCE](V020-WINDOWS-ACCEPTANCE.md) | Reported listening results and final release gate evidence. |
| [RELEASE-CHECKLIST](RELEASE-CHECKLIST.md) | Reusable signed source-release procedure. |
| [CHANGELOG](../CHANGELOG.md) | Historical release scope; released sections remain unchanged. |
| [ROADMAP](ROADMAP.md) | Completed milestones, future ordering, and exit gates. |
| [SOUNDCRAFT-REFERENCE](SOUNDCRAFT-REFERENCE.md) | Pinned mixer/DSP source lessons for 0.5/0.6; planning only, no dependency or accepted detailed contract. |
| [MIXER-REFERENCE](MIXER-REFERENCE.md) | Pinned Buai Music Mixer snapshots, update lessons, and planning decisions. |
| [MIGRATION-BOUNDARY](MIGRATION-BOUNDARY.md) | Source-port provenance and excluded legacy artifacts. |
| [DEPENDENCY-POLICY](DEPENDENCY-POLICY.md) | Approved pins, deferred candidates, and redistribution rules. |
| [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES.md) | Upstream attribution and test fixture terms. |
| [Third-party README](../third_party/README.md) | Dependency acquisition overview. |
| [WRITING-STYLE](WRITING-STYLE.md) | STE-inspired English writing rules; no compliance claim. |
| [GLOSSARY](GLOSSARY.md) | Shared project terms. |
| [LICENSE](../LICENSE) and [NOTICE](../NOTICE) | GPL terms and project provenance. |

## Post-release review

All Markdown documents present in the v0.2.0 baseline, the new index,
LICENSE, and NOTICE were reviewed
against the v0.2.0 source baseline on 2026-10-06. The review compared CLI
constraints, diagnostic fields, dependency acquisition, and test counts with
source code, CMake, and both workflow definitions.

Corrections cover released status, final release evidence, six hardware-free
suites, dependency acquisition, diagnostic history, Windows console constraints,
and document precedence. Existing release changelog sections, dependency pins,
licenses, runtime code, and test code remain unchanged.

The Windows listening check applies to its recorded revision and asset pair.
It does not measure latency, limiter peaks, or compatibility with every file
and device. Numerical limiter claims come from deterministic tests.

## Next work gate

The next planned milestone is 0.3.0 karaoke library work. Implementation uses the accepted
[library contract](KARAOKE-LIBRARY-CONTRACT.md) and
[lyric timeline contract](LYRIC-TIMELINE-CONTRACT.md). They specify identity,
discovery, database ownership, malformed-input behavior, and synthetic fixture
requirements. Native storage/provider integration and NCN24 format evidence remain
explicit pre-implementation gates. Running-backend publication has an
[accepted handoff contract](MEDIA-CLOCK-HANDOFF-CONTRACT.md). The FluidSynth
adapter publishes it and a serialized observer binds the acknowledged
generation. Single-song preparation binds catalog identity and verifies source
revisions; application orchestration remains pending. Local reattachment follows the
[accepted contract](ROOT-REATTACHMENT-CONTRACT.md). The
[accepted metadata/root policy contract](CATALOG-METADATA-POLICY-CONTRACT.md)
is connected to root registration, canonical metadata extraction and policy
inheritance, atomic user override records and owning display resolution.
The [accepted persistence contract](CATALOG-PERSISTENCE-CONTRACT.md) has a pure
codec, fresh-owner restore and detached fake-provider protocol. Review native-provider slices and NCN evidence before their
adapters. The [accepted native provider plan](NATIVE-CHECKPOINT-PROVIDER-PLAN.md)
separates API review, native fault/interruption evidence and Windows synchronization.
Storage, NCN and application integration remain gated. Preserve the
canonical SMF and mixer contracts unless a compatibility change is accepted.

This review does not establish KAR/NCN support or select a UI framework. Later
multi-SoundFont, bus, effects, and output-routing work keeps the ordering in
[ROADMAP.md](ROADMAP.md).

The [2026-10-10 native admission bundle](evidence/native-admission/2026-10-10-wsl2-ext4/README.md)
records revision `4f6924670d4272736984342c8294967650c60edb`; the scoped decision is linked below.

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
Fresh pinned WSL2 ext4 execution and explicit scoped review are Pending. Earlier
`6a6337d` / `4f69246` / `54f404a` acceptances and bundles do not advance; Windows
checkpoint acknowledgment remains BLOCKED, and NCN/manual gates remain open.
