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
| [KARAOKE-LIBRARY-CONTRACT](KARAOKE-LIBRARY-CONTRACT.md) | Accepted 0.3.0 contract; catalog/discovery slice implemented, storage/preparation gated. |
| [LYRIC-TIMELINE-CONTRACT](LYRIC-TIMELINE-CONTRACT.md) | Accepted KAR/media timing contract; decoder, cue extraction, and pure consumer implemented; backend/NCN24 gated. |
| [MEDIA-CLOCK-HANDOFF-CONTRACT](MEDIA-CLOCK-HANDOFF-CONTRACT.md) | Accepted backend-clock handoff; pure atomic cell implemented, adapter pending. |
| [POC-RUNBOOK](POC-RUNBOOK.md) | Windows build, diagnostics, playback, and console operations. |
| [Test README](../tests/README.md) | Test scope and failure diagnostics. |
| [V020-WINDOWS-ACCEPTANCE](V020-WINDOWS-ACCEPTANCE.md) | Reported listening results and final release gate evidence. |
| [RELEASE-CHECKLIST](RELEASE-CHECKLIST.md) | Reusable signed source-release procedure. |
| [CHANGELOG](../CHANGELOG.md) | Historical release scope; released sections remain unchanged. |
| [ROADMAP](ROADMAP.md) | Completed milestones, future ordering, and exit gates. |
| [MIXER-REFERENCE](MIXER-REFERENCE.md) | Pinned feature reference and planning decisions. |
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
requirements. Persistent storage selection and NCN24 format evidence remain
explicit pre-implementation gates. Running-backend publication has a
[accepted handoff contract](MEDIA-CLOCK-HANDOFF-CONTRACT.md). Its pure atomic
cell is implemented; backend integration remains pending. Preserve the
canonical SMF and mixer contracts unless a compatibility change is accepted.

This review does not establish KAR/NCN support or select a UI framework. Later
multi-SoundFont, bus, effects, and output-routing work keeps the ordering in
[ROADMAP.md](ROADMAP.md).
