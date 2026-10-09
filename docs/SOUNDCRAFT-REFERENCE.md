# SoundCraft design reference

## Purpose, status and evidence boundary

**Status:** Planning reference, reviewed on 2026-10-09. This document does not
accept a detailed mixer/DSP contract or add implemented OpenHDK behavior.
**Upstream:** [storytold/soundcraft](https://github.com/storytold/soundcraft).
**Pinned snapshot:** `c51e5d5ec52f11c6264b72e26661fa712feb5345`.
**Snapshot version:** 0.3.0 in Cargo.toml; README describes pre-alpha status.

[SPECIFICATION.md](SPECIFICATION.md) remains the normative authority.
[ROADMAP.md](ROADMAP.md) assigns milestone order. This reference informs future
0.5.0 mixer/bus and 0.6.0 DSP designs. It does not delay or change 0.3.0's
persistence, NCN evidence, application and manual-validation gates.

The review inspected pinned source and notices. It did not build SoundCraft,
run its tests, measure its CPU/latency, audit every processor or validate its
advertised platform/standards coverage. Distinguish inspected source from
upstream claims and OpenHDK proposals. A source comment or trait contract is
not independent runtime proof.

## Pinned source map

| Source | Inspected observation | Use in OpenHDK planning |
| --- | --- | --- |
| [README](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/README.md), [Cargo.toml](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/Cargo.toml) | Layered Rust workspace, realtime/offline mix role, version and pre-alpha status. Feature coverage is an upstream claim. | Study separation of model, rendering, devices and UI; retain the OpenHDK C++/FluidSynth/miniaudio boundary. |
| [DSP interface](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/crates/dsp/src/lib.rs) | Plugin trait has prepare/reset/process, parameters, latency, tail and gain-reduction queries. ParamInfo describes bounds, units, taper and choices. | Propose a backend-independent processor lifecycle and parameter contract. No C++ API is selected here. |
| [Mix engine](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/crates/mix/src/lib.rs) | Strips, inserts, sends, buses, delay alignment and main mix; offline render uses MixEngine too. | Study processing order and routing semantics; redesign resource preparation for the OpenHDK callback contract. |
| [Pan](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/crates/dsp/src/pan.rs) | Mono-to-stereo laws include -3/-4.5/-6/0 dB; finite range handling and endpoint gains. | Specify mono pan separately from stereo balance/dual pan and MIDI CC10. Do not select one law for every source shape. |
| [Biquad](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/crates/dsp/src/biquad.rs) | Source identifies RBJ Audio EQ Cookbook formulas. | Start numerical study with independent formula-based vectors; choose EQ topology in a later contract. |
| [Dynamics](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/crates/dsp/src/plugins/dynamics.rs) | Shared stereo detector, feed-forward compressor with lookahead and sidechain high-pass. | Study detector, smoothing, knee and latency behavior; preserve current safety limiter semantics. |
| [Reverb](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/crates/dsp/src/plugins/reverb.rs) | Room/plate models share an eight-line feedback delay network with Hadamard mixing and damping. | Evaluate bounded send/return effects after bus contracts; no sound-quality or CPU claim from source inspection. |
| [Meter](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/crates/dsp/src/meter.rs) | Peak/hold/clip, RMS, loudness and oversampled true-peak code. | Stage metering from peak to advanced master analysis; standards claims require independent validation. |

## Real-time lessons and required redesign

The DSP trait states that process does not allocate or panic. The mixer has a
wider boundary: MixEngine::render calls ensure_synced, which can call sync.
The sync documentation explicitly says it allocates. Render also collects a
work list into a Vec and can resize scratch storage. The snapshot's handling
of hosted plugin creation/retirement is useful study material, but it does not
prove that the whole mixer meets OpenHDK's callback rules.

OpenHDK's future contract must cover the complete render/adoption path:

- Prepare graph order, buffers, processors, delay lines and bounded event
  storage outside the callback. Validate channels, maximum block size and
  routing before publication. Define oversize-block failure instead of silently
  shortening output.
- Separate immutable topology/configuration from mutable DSP history. Give
  active filter/envelope/delay state a single rendering owner.
- Admit a fully prepared graph at a defined block boundary. Specify generation,
  failure and retirement rules before permitting live replacement.
- Retire old resources outside the callback. The last shared_ptr release and
  destructor may deallocate; pointer exchange alone is not a retirement proof.
- Bound work, storage and handoff capacity. Keep allocation, locks, parsing,
  file I/O, logging and UI work outside the callback. Do not adopt upstream
  parallel-worker scheduling without a separate bounded-execution design.

Start with a fixed prepared graph and zero-latency processors if that makes
these rules testable. Hot graph replacement and full delay compensation may
remain deferred until their own acceptance tests exist.

## Mixer and synthesizer boundaries

Buai Music Mixer provides karaoke-domain and routing UX requirements in
[MIXER-REFERENCE.md](MIXER-REFERENCE.md). SoundCraft supplies a separate mixer/DSP
implementation reference. Neither project defines OpenHDK's public model.

Before per-instrument PCM effects, 0.4.0 must define how synth output becomes
independently addressable PCM strips. A final stereo mix cannot provide
independent instrument inserts merely by naming sixteen MIDI channels. Review
backend output capabilities and resource lifecycle without choosing an adapter
strategy in this document.

For 0.5.0, define a bounded directed graph with explicit cycle rejection,
channel layouts and deterministic processing order. Specify insert placement,
pre/post-fader sends, return gain and solo/mute propagation. A send/return
reverb is a candidate, not a fixed bus count or guaranteed CPU saving.

Keep MIDI destinations separate from PCM buses and audio endpoints. External
MIDI bypasses the PCM mixer and limiter. Keep Mixer Profiles separate from
v0.2.0 flags-only presets; new fields need accepted feature contracts first.

Specify master gain once, before master DSP and the final safety limiter.
The current backend synth gain already precedes the limiter. A future PCM gain
stage needs a migration rule to prevent applying gain twice. The linked stereo
0.98 limiter remains the implemented final safety stage. This reference does
not replace it with a compressor or claim true-peak limiting.

## Processor, parameter, latency and meter contracts

A future processor contract should specify prepare failure, reset, buffer shape,
maximum channels/frames, aliasing, bypass and parameter application. Preparation
may allocate off the callback; process must obey the complete callback boundary.
No virtual C++ interface or automatic mapping of every helper is accepted here.

Study descriptors with stable IDs, units, bounds, defaults, taper and choices.
SoundCraft ParamInfo::clamp maps NaN to default, clamps infinities to bounds and
rounds toggle/choice values. OpenHDK must decide request validation separately:
unknown IDs, non-finite values and invalid profiles need explicit transactional
error behavior. Validated targets and DSP smoothing are different concerns.
Do not silently copy clamp/default policy into CLI or profile loading.

Distinguish mono pan, stereo balance, dual pan and source MIDI pan. Define
endpoint gains, center attenuation and the energy/voltage criterion before
choosing a default. Upstream's labels and formulas are study inputs, not an
OpenHDK pan-law contract.

Represent processing latency in frames separately from effect tail duration.
Define finite, capped and potentially unbounded tails, bypass latency and
release/stop behavior. Prepare compensation storage outside rendering. Adding
latency does not turn committed media position into device presentation time;
lyric/output alignment needs a separate accepted timing design.

Use the same prepared processing core for realtime and offline rendering where
possible. Specify initial state, sample rate, block limits, tail draining and
comparison tolerance. Shared code does not guarantee bit-exact results across
all processors, platforms or block partitions.

Place peak/hold/clip meters explicitly before or after limiting. Pre-limiter
measurement reveals overload; post-limiter measurement describes output.
Define reset, units, decay and observation handoff. Add RMS later. Loudness and
true-peak claims require primary-standard references, independent test signals,
channel-weighting/layout rules and stated tolerances. Upstream meter names are
not certification evidence.

## Work sequence and acceptance inputs

| Stage | Reviewable result before implementation | Required evidence |
| --- | --- | --- |
| Reference now | Pinned lessons, provenance and deferred decisions in this document. | Source-path checks and documentation consistency; no runtime support claim. |
| 0.4.0 | Synth output/layer/strip boundary and resource lifecycle. | Independent output isolation, missing-resource and lifecycle cases. |
| 0.5.0 | Mixer graph, gain/pan/solo/send semantics, prepared graph ownership; minimal processor latency model. | Cycle/layout rejection, impulse routes, endpoint/center pan vectors, no partial publication and no callback allocation/deallocation/locks. |
| 0.6.0 | Parameter/lifecycle/bypass/tail contracts; EQ, compressor, then reverb/chorus slices. | Independent frequency/impulse/gain vectors, linked stereo ratios, malformed parameters, bounds and specified block-partition tolerance. |
| 0.7.0 | PCM output layouts/capability/fallback rules. | Mapping and endpoint failures; multiple-device clocks remain a separate design. |
| 0.8.0 | Meter presentation and UI model independent of processors. | Bounded meter handoff and UI behavior without callback/UI coupling. |

Tests should use synthetic signals and independent numerical expectations.
Allocation/retirement checks must cover graph changes and failures, not only a
steady-state process call. A processor's published latency must match measured
impulse delay. Tail and bypass tests must check the accepted policy. Maintain
hardware-free coverage and add scoped Windows listening/device validation for
audio-affecting releases. Benchmark only with recorded configuration and limits.

Command/CLI/UI unification is a later design input. Agent/MCP control needs
command schemas, validation, authorization and serialization contracts. It does
not enter 0.3.0 or authorize a network control service now. SoundCraft's SMF,
synth, cpal, egui and CLAP/VST3/AU hosting do not replace existing components or
select a framework/dependency for OpenHDK.

## License, assets and provenance

At the pinned snapshot, [LICENSE-MIT](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/LICENSE-MIT) and
[LICENSE-APACHE](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/LICENSE-APACHE) offer MIT OR Apache-2.0 for covered code.
[NOTICE](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/NOTICE) and [ATTRIBUTION](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/ATTRIBUTION.md) describe
notices and separately licensed material. Do not infer that every dependency or
asset has the workspace code license.

The ArtCraft name/wordmark/logos in docs/brand have separate restricted terms in
[LICENSE-brand.txt](https://github.com/storytold/soundcraft/blob/c51e5d5ec52f11c6264b72e26661fa712feb5345/docs/brand/LICENSE-brand.txt). Do not import those
marks into OpenHDK or imply upstream endorsement. This reference imports no
code, asset, Rust package or dependency and makes no license-compliance claim
for a future port.

Before a source port or source-derived C++ translation, record the selected
license, original notices, repository/full SHA/path, modifications and tests
under [MIGRATION-BOUNDARY.md](MIGRATION-BOUNDARY.md). Translation does not remove
provenance obligations. Review notices and dependency policy for that specific
change. Independently implemented algorithm studies should identify primary
formula/standard sources and validation without claiming clean-room work merely
because the programming language differs.
