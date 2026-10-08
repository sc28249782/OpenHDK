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
| [CATALOG-PERSISTENCE-CONTRACT](CATALOG-PERSISTENCE-CONTRACT.md) | Accepted schema-1 contract with pure codec, fresh-owner restore and fake-provider store protocol implemented; native-provider evidence and live wiring pending. |
| [NATIVE-LINUX-CHECKPOINT-EVIDENCE](NATIVE-LINUX-CHECKPOINT-EVIDENCE.md) | Native ext4 acceptance Pending; regression scope and native-run instructions. |
| [NATIVE-CHECKPOINT-PROVIDER-PLAN](NATIVE-CHECKPOINT-PROVIDER-PLAN.md) | Accepted API/evidence plan; Linux lease and experimental provider implemented; native acceptance and Windows synchronization pending. |
| [ROOT-REATTACHMENT-CONTRACT](ROOT-REATTACHMENT-CONTRACT.md) | Accepted attachment-generation/transaction rules; local SMF/KAR reattachment implemented, CLI/storage gated. |
| [LYRIC-TIMELINE-CONTRACT](LYRIC-TIMELINE-CONTRACT.md) | Accepted KAR/media timing contract; extraction/consumer and backend observation implemented; application/NCN24 gated. |
| [MEDIA-CLOCK-HANDOFF-CONTRACT](MEDIA-CLOCK-HANDOFF-CONTRACT.md) | Accepted backend-clock handoff; FluidSynth publication implemented, generation-bound observer implemented; application integration pending. |
| [POC-RUNBOOK](POC-RUNBOOK.md) | Windows build, diagnostics, playback, and console operations. |
| [Test README](../tests/README.md) | Test scope and failure diagnostics. |
| [V020-WINDOWS-ACCEPTANCE](V020-WINDOWS-ACCEPTANCE.md) | Reported listening results and final release gate evidence. |
| [RELEASE-CHECKLIST](RELEASE-CHECKLIST.md) | Reusable signed source-release procedure. |
| [CHANGELOG](../CHANGELOG.md) | Historical release scope; released sections remain unchanged. |
| [ROADMAP](ROADMAP.md) | Completed milestones, future ordering, and exit gates. |
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
