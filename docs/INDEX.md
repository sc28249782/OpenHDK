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
| [DURABLE-LIBRARY-SERVICE-CONTRACT](DURABLE-LIBRARY-SERVICE-CONTRACT.md) | Accepted exclusive service/baseline contract; override primitive, fake tests and experimental Linux factory/fence implemented and scoped acceptance recorded; other mutations pending. |
| [DURABLE-ROOT-LIFECYCLE-PLAN](DURABLE-ROOT-LIFECYCLE-PLAN.md) | Accepted complete-owner staging/repair plan; shared guards and experimental operation-aware register/reattach integration implemented; experimental durable scan/logical relocate/remove implemented; 34-case bounded mutation receipts/verifier implemented; pinned native execution/acceptance pending. |
| [NATIVE-LINUX-CHECKPOINT-EVIDENCE](NATIVE-LINUX-CHECKPOINT-EVIDENCE.md) | Scoped acceptance recorded for 6a6337d on WSL2 /dev/sdd ext4; evidence bundles retained. |
| [NATIVE-CHECKPOINT-PROVIDER-PLAN](NATIVE-CHECKPOINT-PROVIDER-PLAN.md) | Accepted API/evidence plan; Linux lease and experimental provider implemented; scoped Linux acceptance recorded; Windows synchronization BLOCKED. |
| [ROOT-REATTACHMENT-CONTRACT](ROOT-REATTACHMENT-CONTRACT.md) | Accepted attachment-generation/transaction rules; local SMF/KAR reattachment implemented, CLI/storage gated. |
| [LYRIC-TIMELINE-CONTRACT](LYRIC-TIMELINE-CONTRACT.md) | Accepted KAR/media timing contract; extraction/consumer and backend observation implemented; application/NCN24 gated. |
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
