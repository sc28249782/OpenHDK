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
  commit-before-publish mutation wiring still prevents completing this checkbox.
  The [accepted durable service contract](DURABLE-LIBRARY-SERVICE-CONTRACT.md)
  defines exclusive owner/store admission and override-first staging. The service
  primitive and fake-provider lifecycle/commit tests are implemented; construction
  is test-only. Native binding/containment and other durable mutations remain
  separate slices. This does not complete the storage checkbox.
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
and native factory remain pending, followed by fresh pinned integration evidence.
The accepted detached Linux revision and Windows BLOCKED gate remain unchanged.
