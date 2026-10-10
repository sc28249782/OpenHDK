# OpenHDK architecture

## Implemented deterministic SMF path

~~~text
bounded canonical SMF bytes
        ↓
SmfParser
        ↓
SmfTrackEventDecoder
        ↓
SmfTimelineCompiler
        ↓
    SmfTimeline
      ↙     ↘
MidiChannelDiagnostics    PlaybackSession
(read-only aggregate)     (frame-derived timing and due-event spans)
                                ↓
                    SmfMidiEventDispatcher
                                ↓
                    abstract MidiCommandSink
~~~

The parser supports SMF formats 0 and 1. The decoder retains supported channel
events plus tempo, Meta, SysEx, and End-of-Track records; the dispatcher sends
only its supported channel subset to the sink. Timeline ordering is by tick,
file track index, and source index. `PlaybackSession` returns due events but
does not render PCM. `FluidSynthBackend::playCompiledTimeline()` binds those
due-event spans to a private FluidSynth sink and miniaudio render blocks.

The current policy is strict canonical SMF input: bounded MThd/MTrk structure,
required End-of-Track, and no trailing track or file data. A compatibility mode
for legacy padding would require an explicit future design, including exact
acceptance and error-offset rules; it is not implied by the current parser.

`MidiChannelDiagnostics` is an output-independent aggregate over the compiled
timeline. It uses fixed-size per-channel and per-program storage, counts only
note-on messages with nonzero velocity, and records the set of observed
programs. For CC7, CC39, CC11, and CC43, it retains first, pre-first-note,
final, minimum, maximum, and value-change observations in deterministic
timeline order. It reports a 14-bit final value only after both the controller
MSB and LSB have appeared on the same channel, and counts CC121 reset events.
It also reports `effective-cc7`, the source CC7 state OpenHDK uses before
launch-time or runtime channel gain: each CC121 makes that state 100 at the reset's
timeline position. This is an OpenHDK playback-contract view, distinct from
the raw CC7 events in the SMF.
The CLI owns text formatting and file I/O. Its standalone `--midi-diagnostics`
path exits before constructing the audio backend, so inspection has no
SoundFont or audio device dependency.

## Current audio proof of concept

`AudioBackend` separates the current FluidSynth + miniaudio adapter from
the SMF path above. Its explicit compiled-timeline entry prepares a
`PlaybackSession` outside the callback, renders PCM segments between due-event
frame offsets in the callback, and dispatches events at those offsets. The CLI
reads, parses, and compiles SMF before activating the backend, then uses this
compiled-timeline entry and supports a launch-time per-channel gain table. The
FluidSynth MIDI sink keeps
each channel's source CC7 value, applies its configured gain to that value, and
then sends the resulting CC7 value to FluidSynth. Therefore a trim preserves
the volume automation in an SMF instead of replacing it. Reset All Controllers
(CC121) restores the GM default CC7 value before that same gain is applied.
This table is prepared before playback; changing channel gains while a device
callback is running uses `MidiRuntimeMixer`. The control path publishes
lock-free atomic gain, mute, and solo state; the callback observes that state
only at a render-block boundary and issues the corresponding CC7 changes from
its private FluidSynth sink. This preserves source CC7 automation and never
calls FluidSynth from the control thread. Its API uses zero-based channel
indices 0–15; the CLI's existing `--channel-volume` remains one-based 1–16.
`resetRuntimeMixer()` clears all runtime gains, mutes, and solos.
On Windows, `--interactive-mixer` is a main-thread console polling surface for
those same operations. It does not create a second callback or a worker thread;
the callback remains the only path that calls FluidSynth for runtime changes.
The legacy file-player path is not permitted to run alongside compiled-timeline
playback on the same synth.

The implemented v0.2 path inserts a note-on velocity transform immediately before
MIDI dispatch, not in the parser or timeline. Named presets are control-path
snapshots of runtime mute/solo flags and publish through the same revision
boundary as other mixer state. A linked stereo peak limiter sits after the
runtime mixer has produced PCM and before both device and headless output.
The FluidSynth adapter applies the stateless limiter in
`renderSynthFrames`, so every successful segment, including legacy playback,
uses the same output boundary. The dispatcher now applies the configured
velocity curve immediately before positive note-on dispatch. Selection is
fixed during backend initialization; the default linear curve preserves prior
behavior. Non-linear curves are rejected by the legacy file-player API.
The backend owns a control-thread preset registry. Console recall publishes
flags through the packed atomic mixer; only the callback touches the synth.
Registry operations allocate on the control path. Presets survive reset and
reinitialization within the same backend object, with no file persistence.
These layers must retain the callback rules above; they do not authorize a
FluidSynth-specific public mixer model.

The POC has no implicit or bundled SoundFont fallback. A missing or unloadable
configured SoundFont returns the existing structured recoverable error.

## Proposed library and lyric layers

The accepted v0.3.0 contracts in [KARAOKE-LIBRARY-CONTRACT.md](KARAOKE-LIBRARY-CONTRACT.md)
and [LYRIC-TIMELINE-CONTRACT.md](LYRIC-TIMELINE-CONTRACT.md) now have a logical
catalog and a control-path discovery wrapper. Native no-follow source reads,
canonical SMF validation, SHA-256 content tokens, and a final consistency check
feed one catalog publication. Immutable snapshots retain identities and tokens;
scans never replace buffers owned by active playback.

A pure allocating control-path text decoder adds explicit UTF-8/TIS-620
validation and byte-offset failures. It preserves newline/marker bytes; it does
not build cues or execute display actions. Aggregate lyric budgets and event
provenance are enforced by the extractor described below.
A pure KAR extractor now retains immutable raw/decoded payloads and builds
source-ordered text/break operations within one cue per selected nonempty,
non-metadata event. A newline divides display runs, not event timing. It uses
compiler timestamps/provenance without MIDI dispatch. Published const timelines
own their storage; Text operations store byte offsets rather than dangling views.
Selected-payload, cue, title and staging limits apply before publication.

A pure consumer observes integer PlaybackSession media positions and returns
owning immutable due batches. Its emitted cue prefix represents current display
state; equal-time polling emits no repeat cues, and reset/stop clears the prefix.
It does not construct UI state or publish clocks from the running backend.

`SongDiscovery::prepare` resolves one Ready SongId from an acquired snapshot,
checks containment and its SHA-256/byte-count token, and builds immutable MIDI
and KAR timelines under inherited or complete per-call selection policy.
A final reread detects preparation-time
changes. PreparedSong retains catalog identity and its original snapshot;
rescans never replace its buffers. Preparation does not start or replace audio.
Source metadata and user override/display transactions are connected to scan
and preparation. Durable storage, NCN preparation and application lyric
orchestration remain unimplemented. The
[root reattachment contract](ROOT-REATTACHMENT-CONTRACT.md) separates root
attachment generations from catalog and playback revisions. The development
tree stages the new path and invalidated catalog together, verifies native
directory identity, then commits with non-throwing swaps. Preparation rejects
stale-root snapshots while already-prepared buffers remain owned. The
[media-clock handoff contract](MEDIA-CLOCK-HANDOFF-CONTRACT.md) defines a fixed
atomic latest-value cell, playback generations, and successful-block commit
rules. The backend publishes committed block positions, terminal state, and
generation acknowledgments through the implemented cell and counter.
`LyricClockObserver` binds the pure consumer to successful start
acknowledgements and clears it on generation mismatch or terminal errors.
The controller owns the observer for one backend lifetime and clears it before
stop/replacement/shutdown. Application orchestration remains pending. A final scan check is
not an atomic filesystem snapshot; future playback preparation must revalidate source tokens.
The accepted [metadata/root policy contract](CATALOG-METADATA-POLICY-CONTRACT.md)
separates source fields, user overrides and filename fallback. It specifies
immutable root policies, bounded per-scan extraction summaries and prepared
metadata under the effective policy. SongDiscovery now extracts before Ready;
NoLyrics succeeds. CatalogSong has one primary member and shared const compact
metadata. PreparedSong retains fresh effective metadata and the acquired root
context separately. SourceMetadataExtraction reuses canonical KAR results,
with one existing staged payload allowance and checked coexistence charges.
User title/artist replacement stages one immutable shared record and publishes
both fields with one catalog revision. CatalogDisplay resolves owning title and
artist values with explicit origin, separate from source metadata. PreparedSong
materializes display using effective extraction plus acquired snapshot overrides.
Shared records are credited once under the existing payload allowance; validation
and display copies are reserved before allocation. These control-path operations
perform no audio action or filesystem write.
Catalog methods are serialized and non-reentrant. Readers share acquired
immutable snapshots. Lyric extraction and the pure consumer reuse compiled/session media time
rather than an independent wall clock.

The accepted [catalog persistence contract](CATALOG-PERSISTENCE-CONTRACT.md)
keeps a bounded checkpoint codec and native store provider on the control path.
The checkpoint projection retains IDs, allocator history, root policy/generation
and user overrides; it imports no Ready authority, timeline or runtime lineage.
Restore would create a fresh owner with unattached roots. A directory hint would
require explicit reattachment and a complete scan. Store sequence and catalog
revision are separate. Explicit checkpoint success applies only to the captured
revision; durable mutation wiring must commit storage before memory publication.
CommitUncertain blocks further writes until explicit validated reconciliation.
The pure schema-1 codec and detached projection are implemented with complete
validation and one operation-wide coexistence ledger. A validated restore
factory creates a fresh SongDiscovery lineage with retained IDs/history/overrides
and Invalid songs. Roots are inactive; saved hints perform no path I/O. First
explicit attachment increments generation/revision before complete scanning.
A detached store coordinator validates history and commit tokens, stages candidate
and prior copies through an abstract provider, and faults on uncertain publication.
Fake-provider tests exercise the protocol; store tickets bind only a store object.
The [accepted native provider plan](NATIVE-CHECKPOINT-PROVIDER-PLAN.md) keeps
lease/handle ownership and platform publication outside the coordinator. It
includes a Linux no-follow/flock ownership lease with a bounded process registry.
That primitive publishes no checkpoint. The plan requires native evidence and a
reviewed Windows synchronization sequence before
acknowledged storage support. An experimental Linux provider implements artifact slots, bounded I/O,
renameat2 no-replace creation, renameat update, file/parent fsync and explicit
reconciliation. The coordinator includes retained provider bytes in its existing
ledger. Scoped WSL2 ext4 acceptance is recorded; live checkpoint capture is implemented.
Capture acquires one serialized owner snapshot/counters/root mapping into a
const owning projection/context, without I/O. It preserves historical captures
after owner changes. Experimental native admission is implemented; other mutation staging remains
pending. It approves no database package
and adds no callback-visible file, lock, decode, or recovery operation.
The [accepted durable service boundary](DURABLE-LIBRARY-SERVICE-CONTRACT.md)
requires exclusive owner/store lifetimes and an acknowledged baseline. Its first
implemented primitive is complete user override replacement: private staging, confirmed
store commit, then noexcept memory publication. The experimental Linux
factory now owns native binding and root-containment guards. Fake-provider
construction remains test-only. Native service evidence and other mutation
services remain pending.

The evidence target records bounded selected-call and artifact receipts before
cleanup. The maintainer reviewed the pinned WSL2 ext4 matrix run and explicitly
accepted provider/test revision `6a6337df03fb69326cc5096eb2ef34b6e32bac11` on the
recorded `/dev/sdd` ext4 environment. See the
[scoped decision](NATIVE-LINUX-CHECKPOINT-EVIDENCE.md#explicit-scoped-acceptance--2026-10-09).
This closes that Linux evidence gate only; representative coverage retains its
trusted-directory, selected-call and process-interruption limits. Windows
synchronization remains BLOCKED; other platforms and live durable-first wiring
remain separate gates. Evidence bundles and tested revision are unchanged.

The audio callback owns no filesystem, database, text-decoding, or UI work.
Backend progress publication uses its accepted real-time-safe handoff contract.
Persistent storage needs a selected/reviewed adapter. NCN24 normalization needs
its evidence supplement before implementation. The public library model must
not expose Qt, SQL, BASS, or FluidSynth types.

## Future mixer model boundaries

The [mixer reference](MIXER-REFERENCE.md) records planning lessons from Buai
Music Mixer. Future MIDI destinations must be typed separately from PCM buses
and audio endpoints; an external MIDI route bypasses the PCM mixer/limiter.
A versioned Mixer Profile is a future whole-configuration model, separate from
implemented flags-only mixer presets. Master gain and limiting need distinct
stages when buses are added. Drum-tuning filters require an explicit playback
policy that preserves source events and defines destination scope. These are
planning boundaries, not implemented features or accepted detailed contracts.

The pinned [SoundCraft reference](SOUNDCRAFT-REFERENCE.md) adds mixer/DSP study
inputs for 0.5/0.6. Its render/sync allocation behavior is not an OpenHDK callback
pattern. Future graph designs must prepare and retire resources off-callback,
separate topology from mutable DSP state, and specify latency independently of
committed media position. Processor, pan and meter policies require accepted
contracts before implementation. FluidSynth/miniaudio remain the adapter path.

## Deferred layers

Multi-SoundFont layers and mappings, instrument grouping, bus routing, effects,
multi-channel output routing, Qt migration, karaoke library work, KAR/NCN
parsing and lyric timelines, physical MIDI hardware and RtMidi/libremidi,
HNK/HNK3 compatibility, and VST2/VST3 are deferred. They are not part of the
implemented SMF path or the audio proof of concept. No BASS-family component
belongs in any layer.

Audio callbacks must not perform file I/O, allocation, parsing, or UI work.

## Native binding primitive

[OHK-BIND-030](NATIVE-SERVICE-BINDING-PLAN.md) is accepted. Its private lease
epoch capability and retained root/ancestor guard primitives are implemented.
The final coordinator admission fence and experimental Linux factory are now
implemented in durable service slice 2. Fresh integration evidence and its scoped decision are recorded below. Canonical text alone is insufficient
for containment. The primitive admits only same-mount active roots, with
32 active roots and bounded handle/path budgets. Scoped native factory acceptance is recorded below.

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
