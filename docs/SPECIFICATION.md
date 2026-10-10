# OpenHDK System Specification

**Specification ID:** OHK-SPEC
**Status:** Normative released baseline
**Baseline:** OpenHDK `0.2.0`
**Scope:** deterministic SMF playback, FluidSynth/miniaudio, and mixer/dynamics

This document specifies the behavior that OpenHDK currently guarantees. It is
not a specification for a complete karaoke product. A feature listed as
deferred or out of scope is not available merely because a related legacy
application provided it.

The key words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY**
are normative.

This specification is the normative authority for the implemented behavior in
its stated scope. `ARCHITECTURE.md`, `PLAYBACK-SESSION-CONTRACT.md`,
`MIDI-CHANNEL-CONTROLLERS.md`, and `MIXER-DYNAMICS-CONTRACT.md` provide design
context and detailed explanations. If their descriptions conflict with this specification, this specification
takes precedence and the conflicting documents MUST be corrected together.
`ROADMAP.md` describes planned work and does not extend the implemented scope.

## 1. Product identity and scope

OpenHDK is an independent GPL-3.0-or-later modernization project for Windows
karaoke playback. It is not a renamed HandyKaraoke release and MUST NOT import
or depend on BASS, BASS FX, BASSMIDI, BASSmix, BASS_VST, or related BASS
artifacts.

The baseline implements these layers:

1. bounded canonical Standard MIDI File (SMF) parsing;
2. deterministic event decoding and timeline compilation;
3. frame-derived playback-session timing;
4. ordered MIDI-command dispatch;
5. FluidSynth SoundFont synthesis and miniaudio device or headless PCM output;
6. read-only MIDI channel diagnostics;
7. launch-time and runtime-safe per-channel mixing controls;
8. playback velocity curves and named mute/solo presets; and
9. linked stereo hard-peak limiting.

KAR and NCN parsing, lyric display, song database/library management,
multi-SoundFont libraries and mappings, instrument grouping and bus routing,
effects beyond the implemented mixer/dynamics controls, Qt UI, physical MIDI I/O,
VST/VST3 hosting, HNK/HNK3 compatibility, and packaged end-user karaoke
workflows are outside this baseline.

## 2. Architectural boundaries

```text
SMF bytes
  -> SmfParser
  -> SmfTrackEventDecoder
  -> SmfTimelineCompiler
  -> PlaybackSession
  -> SmfMidiEventDispatcher
  -> MidiCommandSink
  -> FluidSynthBackend
  -> miniaudio device callback or headless PCM render
```

Each stage MUST have one responsibility. Parsing, timeline ordering, session
timing, MIDI dispatch, synthesis, and device output MUST NOT be conflated.
`AudioBackend` is the boundary between application control code and an audio
implementation. Backend-specific types MUST NOT leak through its public
contract.

The legacy FluidSynth file-player path is retained only for isolated backend
compatibility tests. It MUST NOT drive the same synthesizer concurrently with
compiled-timeline playback.

## 3. SMF input contract

OpenHDK MUST accept only canonical, bounded SMF formats 0 and 1 with PPQN time
division. It MUST reject SMPTE time division, malformed or truncated chunks,
invalid header lengths, invalid track structure, missing End-of-Track events,
overflowing lengths or time values, and trailing data after the canonical file
end.

Input parsing MUST own the accepted bytes or otherwise retain valid storage for
the lifetime of parsed data. It MUST report a structured error code, byte
offset, and safe diagnostic message; malformed input MUST NOT cause undefined
behavior or an unbounded allocation.

The decoder MUST preserve deterministic source order. It recognizes channel
events, tempo meta-events, ordinary meta-events, SysEx, and End-of-Track. The
dispatcher currently emits only note-on/off, controller, program-change, and
pitch-bend commands. A note-on with velocity zero MUST be emitted as note-off.
Polyphonic key pressure and channel pressure are decoded but deliberately
ignored by the current dispatch contract.

## 4. Timeline and playback-session contract

Timeline compilation MUST order events by absolute tick, then source track
index, then source event index. Tempo conversion MUST use checked arithmetic
and carry fractional microseconds so the result does not depend on how a
timeline is segmented. Tempo value zero is invalid.

`PlaybackSession` accepts an already compiled immutable timeline and a nonzero
sample rate. It MUST measure media time from rendered frame count, never from
wall-clock time. Fractional frame-time remainder MUST carry across render calls
so equivalent total frame counts produce equivalent media time regardless of
audio block size.

During Playing, each render block covers an inclusive time interval and returns
each due event exactly once in compiled order. A Paused block returns no events
and does not advance media time. Completion is explicit: after the last event,
the renderer decides when a synthesis release tail is complete and then marks
the session finished. The current FluidSynth adapter completes immediately
after the final event block; it does not invent a release-tail duration.

## 5. Audio rendering and device lifecycle

Before compiled playback replaces a session, `FluidSynthBackend` MUST stop an
initialized miniaudio device. It MUST publish a prepared playback session
before starting the device. If preparation, session start, or device restart
fails after a successful device stop, the backend MUST make a best-effort
device restart before returning its recoverable error.

When a legacy FluidSynth player exists but is no longer playing, compiled
playback MUST release that player before it prepares a new session. If the
legacy player is still playing, compiled playback MUST fail rather than allow
two playback authorities to control one synth.

The callback renders PCM segments up to each due event's frame offset,
dispatches the event, and then renders the following segment. It MUST use
integer frame accounting for event placement. It MUST NOT parse files,
allocate, lock, log, perform file I/O, or perform UI work.

Headless operation (`--no-device`) MUST render PCM without creating an output
device. Audio device enumeration and selected-device validation MUST be
non-invasive: listing devices does not begin playback.

## 6. MIDI volume and runtime mixer contract

Master output volume is normalized from the CLI range 0 through 100. The
backend MUST reject non-finite values and values outside the normalized range.
Mute forces master output to zero without changing the configured volume.

Each MIDI channel has a source CC7 value and a channel gain. Launch-time
`--channel-volume <1-16>:<0-100>` supplies a gain table; repeated entries for a
channel use the last supplied value. The output CC7 is derived from source CC7
and the configured gain. Therefore a channel trim MUST preserve SMF CC7
automation instead of replacing it. CC121 (Reset All Controllers) restores the
GM-default source CC7 before the gain is applied.

Runtime channel gain, mute, solo, reset, and revision state MUST cross from a
control path into the callback through lock-free atomic state. The callback
MUST NOT call FluidSynth from a control thread or wait for a control lock.
Torn observations across channels MAY be corrected on the next callback block;
they MUST NOT cause a permanent stale mixer state.

When mute or solo makes a channel's effective gain zero, the renderer sends
CC120 (All Sound Off) to stop voices already sounding on that channel.
Unmuting or removing solo does not reconstruct those killed voices; it affects
subsequent note-on events.

The Windows interactive runtime mixer console provides channel commands
`mute`, `unmute`, `solo`, `unsolo`, and `gain` with one-based channel numbers
1 through 16. `reset` and `quit` take no channel number.
Invalid commands or values MUST fail safely without changing mixer state.

### 6.1 v0.2 mixer and dynamics contract

This is the implemented mixer and dynamics contract for the v0.2.0 source
baseline. Release publication requires the separate release checklist.

The v0.2 additions operate on callback-visible mixer state or rendered PCM;
they MUST NOT change the immutable parsed SMF, compiled timeline, or recorded
source controller observations.

A velocity curve applies only to a dispatched note-on with a source velocity
from 1 through 127. A source velocity of zero remains note-off. The baseline
curves are `linear`, `soft`, and `hard`. For a positive source velocity `v`,
`linear` returns `v`; `soft` returns `max(1, round(v*v/127))`; and `hard`
returns `min(127, 127-round((127-v)*(127-v)/127))`. `round` means
non-negative integer rounding to nearest, with an exact half rounded upward.
The selected curve MUST be applied immediately before MIDI note-on dispatch;
it MUST NOT rewrite a source event or alter note-off, controller, program, or
pitch-bend messages.

The playback CLI selects a curve with
`--velocity-curve <linear|soft|hard>`. The default MUST be `linear`. Names are
case-sensitive; a repeated option uses the last valid value. Unknown or
incomplete values MUST fail with the usage error. Backend configuration MUST
reject unknown curve values before creating audio resources. Selection is
fixed for the initialized backend. The legacy file-player API MUST reject
non-linear curves because it bypasses the compiled-timeline dispatcher.

A named mixer preset stores exactly the 16-channel runtime mute and solo
flags. It MUST NOT store source controller state, launch-time channel trims,
runtime gains, master volume, SoundFont state, or playback position. Preset
names are ASCII, case-sensitive identifiers of 1 through 32 characters: the
first character is a letter; remaining characters are letters, digits,
hyphen, or underscore. Saving an existing name replaces its stored flags.
Recalling a preset MUST publish all its flags as one new mixer revision. A
missing, invalid, or deleted preset MUST fail without changing mixer state.
Preset persistence beyond the running process is outside v0.2.

The Windows mixer console provides `preset-save <name>`,
`preset-recall <name>`, `preset-delete <name>`, and `preset-list`. Named
operations MUST take exactly one name; list MUST take no arguments and MUST
report names in ASCII lexicographic order. Backend operations require an
initialized backend. Recall MUST reject a retained legacy player because
runtime channel mixing requires compiled-timeline playback. Save, delete, and
list are control-path operations and do not change synth state. Presets belong
to one backend object, survive mixer reset and backend shutdown/reinitialize,
and end when that object is destroyed. No file persistence or fixed preset
count is introduced in v0.2.

The v0.2 master limiter is an enabled linked stereo hard-peak limiter after
all master and channel mixing and before either device output or headless PCM
output. For each frame, non-finite input samples MUST become zero. Let `p` be
the greater absolute value of the resulting left and right samples. With a
ceiling of 0.98, the frame gain is 1 when `p <= 0.98`, otherwise `0.98/p`.
The same gain MUST multiply both stereo samples. This limiter has no lookahead,
release state, allocation, lock, I/O, logging, or control-thread interaction.
Later limiter algorithms or adjustable parameters require a compatibility
change to this specification.

### 6.2 Accepted v0.3.0 library and lyric contracts

`KARAOKE-LIBRARY-CONTRACT.md` and `LYRIC-TIMELINE-CONTRACT.md` define the
accepted target implementation boundary (PR #34). They cover catalog identity and transactions,
deterministic file discovery, immutable lyric timelines, explicit encoding,
FF 05 preference and identified FF 01 fallback, and an NCN24 evidence gate.
The target KAR cue contract preserves text and display breaks in source order
inside one event-timed cue, while retaining decoded payload and raw bytes.
Leading markers precede their following text; embedded newlines remain between
text runs. No new clock positions or inferred durations are introduced.
The development tree implements the logical catalog and bounded SMF/KAR
discovery/content-token boundary listed in library contract section 9, plus the
pure bounded lyric text decoder and KAR selection/immutable cue extractor
described in lyric contract section 8, plus the pure media-clock consumer in
section 9. The consumer returns once-only whole-cue batches and an emitted
prefix as current display state; reset/stop clears that state. These helpers
are control/observation-path only. Single-song SMF/KAR preparation now
rejects absent/foreign snapshot lineage before filesystem access, resolves
SongId from an acquired catalog snapshot, rechecks source revision
and containment, and owns canonical MIDI and explicit-policy KAR timelines.
No audio operation is performed by preparation; application/CLI lyric
orchestration remains pending.
Complete library and lyric services
MUST NOT be described as implemented or released behavior. The v0.2.0 parser,
playback, MIDI-controller, mixer, and limiter contracts remain unchanged.

[ROOT-REATTACHMENT-CONTRACT.md](ROOT-REATTACHMENT-CONTRACT.md) defines the
accepted per-root attachment generation and transactional reattachment rules.
The development tree implements local SMF/KAR reattachment: a changed binding
invalidates its catalog entries and clears source tokens in one publication.
Preparation rejects stale attachment generations at Resolve before source I/O.
Validated same-path requests leave state unchanged; already-prepared buffers
remain owned. This adds no released behavior or CLI operation.

[CATALOG-METADATA-POLICY-CONTRACT.md](CATALOG-METADATA-POLICY-CONTRACT.md)
is an accepted planning contract. Policy/metadata values and bounded text are
connected to root registration, scan and preparation. Library Ready includes
selected lyric validation under the root policy; NoLyrics remains successful.
Preparation inherits the acquired root policy or uses a complete one-call
selection override after lineage/attachment checks, while retaining fresh
source metadata separately from catalog metadata. Invalid sources cannot be
rescued by a one-call override. User title/artist records are replaced atomically
on the serialized control path; empty text fails, absence clears, and identical
records publish no revision. Errors MUST retain the old snapshot. Rescans,
relocation and reattachment preserve overrides; removal discards them.
Display resolves user title, source title, then filename fallback, with explicit
origin. Artist is user override or absent. Prepared display MUST use fresh
effective source metadata and the acquired overrides, remaining immutable after
later catalog changes. These changes do not alter standalone SMF diagnostics or playback CLI
behavior and are not part of released v0.2.0.

Persistent storage selection and the NCN format evidence supplement require
reviewed designs before their adapters are implemented.
[CATALOG-PERSISTENCE-CONTRACT.md](CATALOG-PERSISTENCE-CONTRACT.md) accepts a
bounded binary checkpoint, ID/counter-preserving fresh restore, unattached roots
and an explicit commit/recovery protocol. A pure detached schema-1 codec is
implemented with canonical ordering, complete validation and shared coexistence
bounds. It MUST NOT import Ready/source authority or live identity/lineage.
A validated fresh-owner restore factory is implemented. It MUST preserve stored
IDs/counters/policies/overrides under a new lineage, invalidate every song and
leave roots unattached. Hints MUST NOT trigger source access. Explicit first
attachment MUST increment generation/revision and require a complete scan.
Exhausted counters MUST NOT wrap; old owners/prepared values remain unchanged.
A detached checkpoint coordinator is implemented with fake-provider tests. It
MUST reject stale/foreign store tickets, retain history, acknowledge only confirmed
publication, and fault on uncertainty until validated reopen/reconciliation.
Store tickets do not prove live catalog snapshot provenance. No autosave or migration is supplied. An experimental native override service
is implemented below; its affected evidence remains pending.
The [accepted native provider plan](NATIVE-CHECKPOINT-PROVIDER-PLAN.md) describes
API review and native evidence obligations. A Linux ownership lease is implemented
with bounded validated paths, single-owner admission, stable no-truncate locking,
and identity rechecks. It MUST NOT publish checkpoints or claim native durability.
Production admission requires the retained directory mount label to be ext4;
this eligibility check does not establish native acceptance. Other platforms
remain unsupported by this primitive. An experimental Linux provider implements
bounded artifact I/O, no-replace create, same-directory update, artifact/parent
synchronization and validated reconciliation. Unknown publication failures and
post-rename sync failures MUST remain Uncertain. Cleanup MUST NOT remove the
primary or another artifact. The coordinator MUST include provider-retained
logical bytes in its existing staged allowance. Scoped WSL2 ext4 acceptance is
[recorded](NATIVE-LINUX-CHECKPOINT-EVIDENCE.md); broader storage support remains gated.
The evidence target records bounded selected-call and artifact receipts before
cleanup. The maintainer reviewed the pinned WSL2 ext4 matrix run and explicitly
accepted provider/test revision `6a6337df03fb69326cc5096eb2ef34b6e32bac11` on the
recorded `/dev/sdd` ext4 environment. See the
[scoped decision](NATIVE-LINUX-CHECKPOINT-EVIDENCE.md#explicit-scoped-acceptance--2026-10-09).
This closes that Linux evidence gate only; representative coverage retains its
trusted-directory, selected-call and process-interruption limits. Windows
synchronization remains BLOCKED; other platforms and live durable-first wiring
remain separate gates. Evidence bundles and tested revision are unchanged.

Its Windows synchronization question remains open.
The native provider and durable-first mutation gates require separate evidence;
this contract does not approve a dependency or change released v0.2.0 behavior.

Running-backend media-position publication has an accepted bounded contract in
`MEDIA-CLOCK-HANDOFF-CONTRACT.md`. Its pure atomic cell and generation counter
are implemented and the FluidSynth adapter publishes committed compiled
positions, acknowledged generations, finish, failure, and explicit stop state.
A serialized `LyricClockObserver` applies the accepted observer table to an
acknowledged generation. Application/CLI orchestration
remains pending; this helper does not prove SongId or source revision. No database
dependency, new CLI option, Qt UI, or
legacy database migration is authorized by these documents. Acceptance tests
and any later support claim MUST close the corresponding gates.

## 7. Command-line contract

The application provides these modes:

| Mode | Required behavior |
| --- | --- |
| `--list-devices` | List available playback devices and exit without playback. |
| `--midi-diagnostics <file.mid>` | Parse the file and report read-only per-channel observations without requiring a SoundFont or audio device. |
| `--midi <file.mid> --soundfont <file.sf2>` | Parse, compile, initialize, and play the SMF timeline. |

Playback accepts optional `--device <index>`, `--volume <0-100>`, repeated
`--channel-volume <1-16>:<0-100>`, `--mute`, `--no-device`,
`--interactive-mixer`, and `--velocity-curve <linear|soft|hard>` arguments.
`--interactive-mixer` enables the runtime
Windows device-output mixer console described in section 6 during playback.
It MUST reject `--no-device` and non-Windows console builds with a usage error.
`--device` and `--no-device` are mutually exclusive. Unknown, incomplete, or
invalid CLI input MUST report usage and return a nonzero usage error. A
backend, SMF, SoundFont, or audio-device failure MUST report a safe diagnostic
and return nonzero.

`--midi-diagnostics` reports SMF format and track count, then each channel's
note-on count, observed programs, CC7 and CC11 history, optional CC39/CC43
14-bit pairs, effective CC7 after CC121, and controller-reset observations.
Diagnostics are observational; they MUST NOT modify playback state or create a
synthesizer/device.

## 8. Error, resource, and concurrency rules

Recoverable failures MUST be represented through `AudioBackendStatus` or the
SMF parser/compiler error model. Public code MUST NOT depend on raw FluidSynth,
miniaudio, filesystem, or exception messages as its error contract.

All backend resources MUST have deterministic shutdown order. Stopping or
replacing a playback session MUST clear callback-visible session activity before
its dependent synthesizer/device resources are released. A failed operation
MUST leave the backend reusable or return a structured error that requires a
documented reinitialization step.

Control-path mutation is permitted only when its callback-visible handoff is
defined. The audio callback is a real-time boundary and takes precedence over
convenience code.

## 9. Verification and acceptance

Every change that affects this specification MUST update the relevant test and
documentation in the same change set. At minimum, the Windows CMake/Ninja
build and CTest suite MUST pass. The baseline suite covers parser fixtures,
diagnostics CLI success and error cases, runtime mixer console behavior,
FluidSynth headless playback, CLI help, and bootstrap audio behavior. v0.2
work MUST additionally cover every baseline velocity curve, atomic preset
recall semantics, linked-stereo peak limiting, and non-finite PCM handling in
hardware-free tests.

Manual validation with a user-supplied MIDI and SoundFont remains required for
audible balance changes and audio-device behavior. Such assets MUST NOT be
committed unless their redistribution rights are verified.

The development live checkpoint capture adapter acquires a const projection and
snapshot context from one serialized SongDiscovery owner. It copies matching
root policy/generation/hints, allocator high water and user overrides without
filesystem access. It does not persist Ready authority or make mutations durable.
Limits and conservative Windows UTF-8 hint preflight are specified in
[OHK-STORE-030](CATALOG-PERSISTENCE-CONTRACT.md#current-live-checkpoint-capture-boundary).
Experimental Linux durable service admission is implemented; fresh native
evidence remains pending. Released v0.2.0 behavior is unchanged.
The [accepted durable service contract](DURABLE-LIBRARY-SERVICE-CONTRACT.md)
defines exclusive owner/store admission, an acknowledged baseline and override-first
staging. The service primitive has fake-provider lifecycle and commit-before-memory
publication tests. Fake construction uses the test provider category under
seams; experimental Linux factories own their concrete provider and admission. Existing in-memory
mutation, provider acceptance and playback behavior are unchanged.

## 10. Deferred work and change control

[SOUNDCRAFT-REFERENCE.md](SOUNDCRAFT-REFERENCE.md) supplies pinned source-study
inputs for future mixer/DSP contracts. It adds no implemented behavior, accepted
processor/graph API or dependency and does not change 0.3.0 gates. Existing
synth gain, limiter, playback and committed-clock semantics remain unchanged.

The following require separate designs and acceptance before they may be
claimed as supported: multi-SoundFont libraries and mappings, instrument
classification, mixer groups and bus routing, versioned whole-system Mixer
Profiles, drum-tuning compatibility policies, output routing, karaoke lyrics
and timelines, song library/database, UI, physical or external MIDI, external
effects/plugins, cross-platform guarantees, and any permissive parser
compatibility mode.

Velocity curves, named mixer presets, and the linked stereo hard-peak limiter
are implemented for the v0.2.0 source baseline. Windows device listening and
automated tests passed on the source revision recorded in
`V020-WINDOWS-ACCEPTANCE.md`. These additions are not part of the released
v0.1.0 baseline. The published v0.2.0 tag points to
`0175cac739ea7100e7e298a5c4527c3175fc0323`. Final build/test, CI, signature,
and source-only publication evidence is recorded in
`V020-WINDOWS-ACCEPTANCE.md`. Future releases MUST pass the gates in
`RELEASE-CHECKLIST.md` before signed tagging and publication.

Changes to parser strictness, event ordering, time conversion, session
completion, callback rules, channel-volume semantics, or CLI argument behavior
are compatibility changes. They MUST update this document, regression tests,
and release notes together.

The accepted [native service binding plan](NATIVE-SERVICE-BINDING-PLAN.md)
now has private lease-epoch and retained-handle ancestry primitives. They reject
stale/foreign bindings, root overlap and different observed mount IDs; they
retain at most 32 active roots under a conservative 45-descriptor cap. They
perform no save or memory publication. Discovery and checkpoint schema limits
remain separate. An experimental native factory and private coordinator fence are implemented.
Fresh integration evidence and explicit acceptance remain pending. Native service acceptance requires fresh integration evidence.

The [experimental factory boundary](NATIVE-SERVICE-BINDING-PLAN.md#11-experimental-factory-and-final-admission-fence)
records explicit Create/Open, private final and NoChange admission checks, and
shared binding/guard accounting. Future durable root lifecycle and application
wiring remain separate slices; new native receipts/run/acceptance remain pending.

## Native admission receipt instrumentation

The binding and experimental native service targets now emit bounded per-case
[admission receipts](NATIVE-ADMISSION-EVIDENCE.md) for lease epochs, identity/mount checks,
ancestor termination and the final changed/NoChange fence. The existing collector
verifies both logs with Python 3 and records its verification exit code. Current
counts remain 28 core / 31 audio-enabled; binding checks are 74 and native service
checks are 33. A fresh pinned ext4 run is recorded in the evidence section below; explicit
scoped native factory acceptance remains pending. Historical evidence and the `6a6337d` provider acceptance are
unchanged. Windows checkpoint acknowledgment remains blocked.

## Recorded WSL2 ext4 admission run

The maintainer supplied a [fresh pinned admission run](evidence/native-admission/2026-10-10-wsl2-ext4/README.md) at
`4f6924670d4272736984342c8294967650c60edb` on WSL2 `/dev/sdd` ext4.
All 28 compile/run exits, admission verifier and overall exit are zero. The
binding/service logs use bypass zero and contain 13 traces, 364 identity
observations and three fence cases. The published logs redact the local username
with original/published hashes and replacement counts. This is supplied execution
evidence, not independent source/run authentication. Explicit scoped factory
acceptance remains pending; mount-namespace alias evidence remains Skipped.
Historical `6a6337d` acceptance, Windows blockage and roadmap checkboxes are unchanged.
