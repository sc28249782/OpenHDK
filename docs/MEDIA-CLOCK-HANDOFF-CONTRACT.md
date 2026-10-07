# Running-backend media-clock handoff contract

**Contract ID:** OHK-CLOCK-030

**Status:** Accepted by PR #41. The pure publication cell is implemented in
the development tree; backend integration remains pending.

**Target:** OpenHDK 0.3.0. Released v0.2.0 has no backend clock observer or
karaoke lyric service.

## 1. Purpose and scope

This contract defines a bounded handoff from a running playback backend to a
serialized control-path observer. The observer can drive the existing pure
`LyricMediaConsumer` without reading callback-owned session state.

The published position is the media time of PCM frames successfully rendered
by OpenHDK. It is not a device presentation timestamp. It does not measure or
compensate for driver, device, buffer, speaker, or listener latency. An
observer MUST NOT interpolate it from wall time.

The handoff publishes only fixed-size values. The writer and reader MUST NOT
allocate, lock, perform I/O, log, parse text, or call UI code. This contract
does not add a pause API, a lyric renderer, or a queue of callback events.

## 2. Public value model

The public model MUST remain independent of FluidSynth, miniaudio, and KAR
types. One accepted snapshot contains:

| Field | Required meaning |
| --- | --- |
| publication revision | A nonzero even sequence value for one complete publication. |
| playback generation | A nonzero identity for one admitted compiled-playback attempt, or zero when no attempt is bound. |
| media microseconds | The last successfully committed frame-derived media position for that generation. |
| source | `None`, `CompiledTimeline`, or `LegacyPlayer`. |
| phase | `Unavailable`, `Preparing`, `Playing`, `Paused`, `Finished`, `Stopped`, or `Failed`. |
| failure | A fixed error code, or `None`; callback code MUST NOT publish a string. |

`Paused` reserves observation behavior for a separately approved backend pause
operation. This proposal does not authorize such an operation.

The initial snapshot is source `None`, phase `Unavailable`, generation zero,
media time zero, and failure `None`.

## 3. Playback generations

The backend MUST assign a new generation to every admitted compiled-playback
attempt. Generations start at one, increase for the lifetime of one backend
object, and MUST NOT be reused after failure, stop, shutdown, or
reinitialization. Runtime gain, mute, solo, and preset changes do not create a
generation.

The start result MUST expose the successful generation to the controller. The
controller MUST bind a prepared lyric timeline only to that acknowledged
generation. A snapshot alone is not evidence that a start call succeeded.

The backend MUST reject a new attempt when the generation counter cannot
advance without wrapping. The rejection MUST occur before changing current
playback or its published record. Generation zero remains reserved.

## 4. Position commit boundary

The backend MAY calculate a tentative block end before synthesis. It MUST
publish that position only after all due MIDI commands and all PCM segments in
the block have rendered successfully. A dispatch or synthesis failure MUST
retain the previous successfully committed position.

This rule matters because `PlaybackSession::render` selects events and advances
its internal frame clock before FluidSynth renders every segment. The backend
MUST retain the last committed position separately before it discards or
stops the failed session.

The backend MUST publish `Finished` with the final committed position before
it makes compiled playback inactive. The terminal snapshot remains readable
during idle rendering until an explicit stop, replacement attempt, or shutdown
publishes another lifecycle record.

Headless `renderStereo` uses the same commit boundary. Concurrent headless and
device rendering remains forbidden. The legacy FluidSynth player has no
equivalent compiled media clock; it MUST publish source `LegacyPlayer`, phase
`Unavailable`, and MUST NOT estimate a position from wall time.

## 5. Writer ownership and lifecycle

Only one writer may publish at a time. The device callback owns publication
while the device can call it. The control path may publish lifecycle records
only after device stop or uninitialization has established that no callback is
in flight. An active flag alone is not writer synchronization.

If callback quiescence fails, the control path MUST preserve the current
session and publication. It MUST NOT introduce a second writer. Headless
rendering uses one serialized control-path writer.

An admitted compiled attempt publishes `Preparing` at time zero. A successful
start publishes `Playing` at time zero and returns its generation only after
the device starts successfully, when a device is configured. A failure after
admission publishes `Failed` with that generation and its last committed time
after the callback is quiescent. Best-effort restoration of idle device
rendering MUST NOT erase the failure record.

An explicit stop publishes `Stopped` for the current generation with time
zero. Shutdown publishes the initial unbound value after the callback is
quiescent. The generation and publication counters still do not reset during
the backend object's lifetime.

## 6. Fixed atomic publication cell

The implementation MUST use fixed-size atomic fields for sequence,
generation, media time, source, phase, failure, and exhaustion. Every atomic
type used by the cell MUST be always lock-free on the supported target, checked
at compile time. The implementation MUST fail to build or expose the feature
as unavailable on an unsupported target; it MUST NOT hide a mutex fallback in
the callback.

The sequence counter begins at zero. One writer publishes without a retry
loop:

1. Check that the next odd/even sequence pair is representable.
2. Store the next odd sequence value.
3. Store-release every payload field, including unchanged fields.
4. Store-release the following even sequence value.

One reader makes exactly one attempt per poll:

1. Load-acquire the exhaustion latch.
2. Load-acquire the sequence; reject an odd value as transiently unstable.
3. Load-acquire every payload field.
4. Load-acquire the sequence and exhaustion latch again.
5. Accept only equal even sequence values with no exhaustion.

The read result is `Snapshot`, `Unstable`, or `Exhausted`. `Unstable` carries
no snapshot. The reader MUST NOT spin, retry, wait, or combine fields from its
last valid value. A caller may retain its last valid snapshot only while the
expected generation still matches.

Each payload field is atomic. A sequence check around ordinary non-atomic
fields would create a data race. Release/acquire operations on every payload
field provide the ordering needed to reject a value that overlaps a later
publication when the final sequence check sees that later publication's odd
or even sequence.

## 7. Counter exhaustion

The sequence counter MUST NOT wrap or reset while observers can retain values.
If the next odd/even pair is unavailable, the writer freezes the last complete
payload and sets a permanent lock-free exhaustion latch. It MUST NOT try to
publish a final error through the exhausted sequence.

After exhaustion, every read returns `Exhausted`. The controller clears lyric
state and reports a recoverable playback observation error. Compiled playback
must be quiesced through its normal error path. A new backend object is
required after existing observers release the old object.

## 8. Observer behavior

The controller owns one `LyricMediaConsumer` on its serialized observation
path. It applies an accepted snapshot as follows:

| Condition | Required action |
| --- | --- |
| `Preparing` | Retain a prepared binding; do not advance. |
| `Playing`, expected generation | Advance through committed media time. |
| `Paused`, expected generation | May catch up through the held committed time; do not extrapolate. |
| `Finished`, expected generation | Advance through the final committed time and retain the observed prefix. |
| `Failed`, `Stopped`, `Unavailable`, or generation mismatch | Stop the consumer and clear current display state. |
| `Unstable` | Skip this poll without advancing or extrapolating. |
| `Exhausted` | Stop and clear, then report the observation error. |

The controller MUST stop the old consumer before it requests playback stop,
replacement, or backend shutdown. This action does not depend on a successful
clock read. The playback generation identifies a backend attempt; it does not
prove `SongId`, source revision, or lyric identity. Library preparation remains
responsible for those bindings.

The cell retains only the newest value. A delayed observer can still emit all
due cues through that position because the pure consumer owns its timeline and
prefix. The observer is not guaranteed to see every intermediate lifecycle
phase when replacement occurs between polls. A generation mismatch prevents
cues from two attempts from being mixed.

## 9. Acceptance tests

Implementation requires hardware-free tests that cover:

- coherent snapshots under concurrent publication of two distinguishable
  payloads;
- a deterministic read during an odd sequence, which returns `Unstable` after
  one attempt;
- first block, equal polls, block partitioning, and final-block publication;
- a mid-block dispatch/render failure that retains the last committed time;
- finish, stop, failed start, restart, and generation non-reuse;
- the legacy-player unavailable record and a valid clock for NoLyrics;
- injected sequence and generation values at their exhaustion boundaries; and
- compile-time lock-free checks for every cell atomic.

The concurrency test MUST run under the existing strict compiler and
ASan/UBSan configuration. Passing it does not replace a race-detector proof;
the atomic algorithm and its single-writer ownership remain part of the review.

The Windows audio-enabled suite MUST exercise the same publication boundary
through real headless FluidSynth rendering. Later manual device/lyric
validation must identify the revision and external assets. Listening may
confirm ordering and lifecycle behavior; it MUST NOT claim measured device
latency or presentation-clock synchronization.

## 10. Current implementation boundary

`audio/MediaClockPublication.hpp` implements the fixed atomic publication cell,
bounded one-attempt read, permanent sequence-exhaustion latch, and monotonic
generation counter. A test-only seam injects counter boundaries and an action
between the odd sequence store and payload publication. The production publish
path has no hook, allocation, lock, retry, I/O, or logging.

The cell does not yet publish from `FluidSynthBackend`, acknowledge a generation
from compiled playback, or drive `LyricMediaConsumer`. The remaining lifecycle,
failed-block commit, and real headless-render tests in section 9 must pass when
that adapter is implemented.
