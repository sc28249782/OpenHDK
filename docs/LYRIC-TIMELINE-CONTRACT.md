# Lyric timeline and NCN profile proposal

**Contract ID:** OHK-LYR-030
**Status:** Accepted planning contract (PR #34); implementation proceeds in reviewed slices.
**Target:** 0.3.0; no KAR/NCN support is claimed for released v0.2.0.

`SPECIFICATION.md` remains normative for implemented behavior. The obligations
below apply to the target implementation accepted in PR #34. Catalog and
file ownership are specified in
[KARAOKE-LIBRARY-CONTRACT.md](KARAOKE-LIBRARY-CONTRACT.md).

## 1. Immutable lyric timeline

A cue contains a compiled tick, media time in integer microseconds, source
track/event index or NCN cursor index, an ordered display-operation sequence,
decoded UTF-8 payload text, and retained raw source bytes. A cue is an event
fragment, not an inferred syllable, note, or grapheme. Preserve spaces and punctuation; do not insert
spaces, guess syllable durations, or distribute one fragment across notes.

Build cues on the control path. KAR cues reuse each selected event's exact
`SmfTimeline` tick/time and source order. Do not calculate time again from a
single BPM. At equal timestamps preserve tick, track index, and source event
index. Within a cue, text and break operations MUST preserve source order.
Empty payloads produce no cue. Metadata is retained separately and never
becomes timed lyric text.

The display sequence consists of `Text`, `LineBreak`, and `ParagraphBreak`
operations. Each `Text` operation contains a nonempty contiguous run of decoded
text between recognized controls. Leading markers produce break operations
before the following text; an embedded newline produces a break at its original
position between text runs. Do not move embedded breaks before all text.
Adjacent literal characters form one `Text` run; do not emit empty `Text`
operations. Operations can refer to immutable decoded storage or own text,
but all retained storage counts toward the existing staging budget.

One selected nonempty, non-metadata KAR event produces one cue, including
an action-only event. All of its operations share that event's tick, media time, and provenance.
Splitting text into display runs MUST NOT split the cue, assign additional
cue times, or infer durations. Retain the full decoded payload independently
of display operations, including markers and original CR/LF bytes, alongside
the raw source bytes. Only the display sequence interprets the selected
profile's controls. The consumer emits the whole ordered cue once when due;
its operations do not have separate clock positions.

A consumer advances from the PlaybackSession media clock, never wall time.
At position `t`, all cues at or before `t` are due in stable order. It MUST
emit each cue once per forward traversal. Equal-position polls emit nothing
new; a paused clock emits nothing new. At completion, retain the final lyric
state until explicit stop/reset. Stop/reset clears the consumer cursor and
display state; a new session starts from the beginning. A lyric consumer MUST
NOT alter PlaybackSession completion or invent an audio release tail.

A pure consumer can receive media positions without a device. Publishing those
positions from the running backend requires a separate bounded, lock-free
handoff design; no UI work, allocation, logging, or lyric decoding is permitted
in the audio callback. Seeking/backward traversal is outside this increment;
reject backward positions unless the consumer was explicitly reset.

## 2. KAR selection: FF 05 first, FF 01 only when identified

These are OpenHDK compatibility policies, not a claim that all karaoke dialects
share one convention. A `.kar` extension alone does not identify lyric tracks.
Use decoded Meta types; never search raw file bytes for `FF 01` or `FF 05`.

Without an explicit track selection:

1. Find tracks with nonempty Lyric Meta (`FF 05`) payloads. If exactly one
   exists, use its FF 05 events. If several exist, return AmbiguousLyricTrack.
2. Only when no such FF 05 track exists, find FF 01 text tracks whose Track
   Name (`FF 03`) is `Words` or `Lyrics`, after trimming ASCII space/tab and
   folding ASCII case. A track qualifies if any Track Name event has that
   name. Exclude leading-`@` metadata and empty payloads when finding candidates.
3. Exactly one eligible track selects that text profile. Several eligible
   tracks return AmbiguousLyricTrack; none produce a successful empty lyric
   timeline with state NoLyrics.

An explicit zero-based track index restricts selection to that track first.
Within it, nonempty FF 05 takes precedence. Otherwise it must satisfy the named
FF 01 rule or returns NoLyrics. An out-of-range index is InvalidLyricTrack.
Do not merge FF 01 and FF 05, join competing language tracks, or fall back after
a selected track fails text decoding. Music can be valid without lyrics;
NoLyrics is not an InvalidSmf error.

In the named FF 01 profile, each payload starting with ASCII `@` is metadata,
not sung text. Retain the raw tag and event position. Recognized `@T` values
may supply title metadata, but MUST NOT be heuristically split into title and
artist. Title uses the first nonempty `@T` in selected track order; retain
additional values without claiming their role. This proposal does not decode
`@L` as a charset instruction or accept a marker-only unnamed track.

## 3. Text encoding and display actions

Selection supplies an explicit `UTF-8` or `TIS-620` decoder. UTF-8 is the
initial default for KAR; ASCII is a subset. Never use the host locale, guess
encoding from Thai-looking bytes, or retry another encoding after failure.
Raw bytes remain available independently of decoded display text.

UTF-8 MUST reject invalid, overlong, surrogate, and out-of-range sequences.
TIS-620 MUST use a reviewed fixed mapping and reject undefined bytes. Decoder
errors identify the selected member/event and payload byte offset. No silent
replacement characters, normalization, transliteration, BOM stripping, or
platform-codepage conversion is permitted. NUL and other C0 controls except
CR/LF/tab fail InvalidText. Tab is retained as text; no tab width is implied.

| Selected profile | Control policy |
| --- | --- |
| FF 05 lyric | CR, LF, or CRLF within one payload becomes one LineBreak per logical newline. Slash, backslash, and `@` remain literal text. |
| Named FF 01 text | Leading `/` becomes LineBreak; leading `\` becomes ParagraphBreak. Consume all consecutive leading markers in order, then retain the remainder as text. CR/LF rules match FF 05. Markers elsewhere remain literal. |

The following display vectors use escape notation for source controls.
They describe operations inside one cue unless two events are explicitly shown:

| Profile and payload | Ordered display operations |
| --- | --- |
| FF 05 `A\r\nB` | Text(`A`), LineBreak, Text(`B`) |
| FF 05 `\r\n` | LineBreak; no empty Text operation |
| FF 05 `/A\\B@` | Text(`/A\\B@`); markers remain literal |
| Named FF 01 `/\\/Hello` | LineBreak, ParagraphBreak, LineBreak, Text(`Hello`) |
| Named FF 01 `/A\r\nB` | LineBreak, Text(`A`), LineBreak, Text(`B`) |
| Named FF 01 `A/B\\C` | Text(`A/B\\C`); non-leading markers remain literal |
| Named FF 01 `/\\` | LineBreak, ParagraphBreak; action-only cue |
| FF 05 event `A\r`, then event `\nB` | First cue: Text(`A`), LineBreak; second cue: LineBreak, Text(`B`) |

CRLF is combined only within a payload, not across event boundaries. A
ParagraphBreak is a semantic request to start a new paragraph/display section;
this contract does not require a particular screen layout. Marker-only payloads
produce an action-only cue. UTF-8 combining characters remain code points;
grapheme highlighting and typography require later UI contracts. The same
encoding policy applies to extracted metadata. Generic FF 01 comments,
copyright, and track-name events never become lyric cues by default.

## 4. NCN24 bundle boundary

NCN24 is a proposed, intentionally narrow OpenHDK profile based on the pinned
legacy reader in section 7. It is not an official specification for every NCN
collection. Selecting it is explicit; a loose MIDI file never activates it.

A registered NCN root has `Song`, `Lyrics`, and `Cursor` directories. Resolve
`Song/<relative stem>.mid` to `Lyrics/<relative stem>.lyr` and
`Cursor/<relative stem>.cur`. Preserve subdirectories and the whole filename
stem. Match directory names, stem components, and extensions using ASCII case
folding; reject multiple matches. Non-ASCII characters compare exactly. All
members must be regular files inside the root under the library containment
rules. Missing, ambiguous, or unreadable members fail the whole bundle.
NCN24 accepts `.mid` as its primary member; `.midi` is not a profile alias.

The proposed LYR profile uses explicit TIS-620. Its first four terminated text
lines are title, artist, key text, and an uninterpreted reserved header line.
The remaining body is lyrics. Accept CRLF, LF, and CR line endings, normalized
to LF in the body. Keep empty lines, spaces, and a terminal body newline; do
not trim them. Missing header terminators fail InvalidText. Key text is
metadata, not a transposition command. Retain header bytes and provenance.

The proposed CUR profile is a headerless stream of unsigned 16-bit
little-endian positions. Odd byte length fails InvalidCursor; no padded last
value is allowed. Values must be nondecreasing; equal values are valid.
For a cursor value `u` and primary MIDI PPQN `q`, normalize to the integer tick
`floor(u*q/24)` using checked multiplication/division. Interpret the 24 divisor
as this profile's cursor units per quarter note. This is a compatibility choice
inferred from the pinned reader, not a universal NCN timing guarantee.

Use the primary MIDI's compiled tempo map, including fractional carry, to
convert normalized ticks to media microseconds. Do not use file length, first
BPM, or a wall-clock timer. Reject positions beyond the primary timeline's
last tick. Floor conversion may collapse adjacent positions at small PPQN;
retain cursor order and never reorder equal-time cues.

After newline normalization, require one cursor position per Unicode scalar
in the body, including spaces and each LF. LF produces LineBreak; other
scalars produce text cues. Thai combining scalars consume separate positions;
no grapheme inference is performed. Cursor/body count mismatch fails the
bundle, with no inferred timing, truncation, duplicated cursor, or partial
lyric result. Empty body plus empty CUR is NoLyrics; a nonempty body with empty
CUR is invalid. This rule is deliberately stricter than a tolerant legacy UI.

## 5. NCN evidence gate before implementation

The layout, TIS-620 use, and integer cursor formula are observations from one
reader. The proposed count/newline rules need independent evidence. Before
implementing the NCN normalizer, publish a reviewed profile supplement with:

- independently authored minimal LYR/CUR/MID byte fixtures and expected cues;
- evidence for four header lines, CRLF/terminal-newline handling, code-point
  counts (including Thai combining characters), and cursor/body correspondence;
- expected little-endian values, PPQN-divisible and non-divisible examples,
  tempo changes, duplicate positions, decreasing values, and odd-byte errors;
- explicit limits and malformed-input offsets; and
- maintainer-reported comparison on a lawfully available representative bundle
  or another independently verifiable primary format source.

Private files remain external. A source sample is not a redistribution license.
If evidence disagrees, revise this proposal before writing the parser. Until
this gate closes, discovery may report NCN24 as UnsupportedProfile, but MUST
NOT advertise NCN lyric playback. A release claiming NCN24 support must close
this gate; it cannot silently substitute KAR-only scope.

## 6. Required independent test vectors

| Case | Required result |
| --- | --- |
| FF 05 and eligible FF 01 present | Use FF 05 only. |
| Two FF 05 tracks, no selection | AmbiguousLyricTrack; no mixed cues. |
| Named Words track; generic comment elsewhere | Use selected text, ignore comment. |
| `@TTitle`, `/Hello`, `\World` in text profile | Metadata, LineBreak + text, ParagraphBreak + text. |
| Slash/backslash in FF 05 | Literal characters. |
| CRLF in one payload; split CR/LF payloads | One LineBreak; two LineBreaks respectively. |
| PPQN 3, default tempo, KAR events at ticks 1/2 | Exact compiler times 166666/333333 microseconds. |
| Tempo change and equal-time events | Preserve compiler times and source order. |
| Poll equal time, pause, stop/reset | No repeat cue; no advance; cleared cursor/state. |
| Invalid UTF-8, undefined TIS-620, NUL | InvalidText with event/member and byte position. |
| NCN u=24/48, q=480, default tempo | Ticks 480/960, times 500000/1000000 microseconds. |
| NCN u=7/8, q=3, default tempo | Ticks 0/1, times 0/166666 microseconds. |
| NCN odd/decreasing/count mismatch/missing member | Structured failure, no partial preparation. |

Test every limit in the library contract. Source bytes, MIDI event order,
controllers, diagnostics, and mixer behavior must remain unchanged. Public
synthetic fixtures MUST have independent expected values rather than copying
the parser's formula as the only oracle. Add state/encoding/selection tests
before connecting the library to audio or any display surface.

## 7. Evidence and provenance

The MIDI Association describes SMF lyric recommendations and separate language
and display extensions. Those sources establish that text conventions require
an explicit profile; this proposal does not claim full RP-017/RP-026 conformance:

- [SMF specifications](https://midi.org/standard-midi-files)
- [SMF Lyric Meta Event Definition](https://midi.org/smf-lyric-meta-event-definition)
- [SMF Language and Display Extensions](https://midi.org/smf-language-and-display-extensions)

Legacy observations were reviewed on 2026-10-06 at HandyKaraoke commit
`7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8`:

- [SongDatabase.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/SongDatabase.cpp): NCN directory/member layout and LYR metadata reading.
- [Utils.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Utils.cpp): TIS-620, four skipped header lines, little-endian CUR values, and PPQN/24 conversion.
- [MidiFile.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Midi/MidiFile.cpp): FF 05 preference and named FF 01 fallback.

These are behavioral references, not copied implementation or universal format
proof. No Qt/BASS code or media is imported. Any future source port requires
[MIGRATION-BOUNDARY.md](MIGRATION-BOUNDARY.md) provenance and preserved notices.

## 8. Current implementation boundary and text mapping

The development tree provides `lyrics/LyricTextDecoder.hpp`, a pure allocating
control-path helper. It returns owned UTF-8 on success or an error code and
payload byte offset with no partial text. Invalid UTF-8 reports the first
invalid byte; a missing continuation reports the payload end. Future extraction
must attach member, track, and source-event provenance to this local offset.
Allocation exceptions propagate. Input spans remain unchanged and are not
retained by this helper; future cues must retain raw source bytes separately.

The fixed TIS-620 mapping is reviewed against
[CPython v3.13.0 TIS-620 mapping](https://github.com/python/cpython/blob/v3.13.0/Tools/unicode/python-mappings/TIS-620.TXT).
It is expressed independently as ranges, without importing that implementation:

| Source bytes | Unicode scalars |
| --- | --- |
| `00..9F` | `U+0000..U+009F`, subject to the C0 policy in section 3 |
| `A1..DA` | `U+0E01..U+0E3A` |
| `DF` | `U+0E3F` |
| `E0..FB` | `U+0E40..U+0E5B` |
| `A0`, `DB..DE`, `FC..FF` | Undefined; InvalidText |

DEL and C1 scalars are retained literally in both encodings; they have no
implied display action. This is an explicit mapping profile, not a CP874 or
ISO-8859-11 alias. The decoder preserves CR/LF/tab, BOM, spaces, punctuation,
and combining characters without interpretation or normalization.

Per-call input defaults to the library's 4 MiB lyric-source bound and decoded
output to its 64 MiB staging ceiling. Positive lower limits are accepted for
testing; higher limits fail configuration. Output capacity is checked before
each scalar is appended, including TIS-to-UTF-8 expansion. These are per-call
byte bounds, not aggregate cue/metadata accounting or allocator-overhead caps;
the future extractor must enforce the full staging budget across its results.

`lyrics/KarLyricExtractor.hpp` now implements pure selection and immutable
KAR cue construction from an already validated `SmfTimeline`. It uses FF05
precedence, named FF01 fallback, and explicit zero-based track restriction.
NoLyrics is a successful empty timeline. It retains selected raw/decoded
payloads, source positions, ordered display operations, separate FF01 metadata,
and the first nonempty @T title without splitting or trimming it. Errors return
no partial timeline and attach the event position and payload offset when
available. The supplied timeline identifies the member; library preparation
must add its locator to diagnostics. Parsing/compilation and their nested
errors stay with the existing SMF APIs, not this already-compiled input API.

Published results own their storage through `shared_ptr<const KarLyricTimeline>`;
cue/metadata access returns const spans. Text operations store byte ranges into
the owned decoded payload, not pointers invalidated by string/vector moves.
Break offsets identify their source marker/newline in decoded bytes. These
operation offsets are distinct from decoder error offsets in raw payload bytes.

The extractor enforces aggregate selected-payload bytes (including metadata),
cue count, extracted-title bytes, and retained staging bounds. Positive lower
limits support tests; higher limits fail configuration. Logical staging charges
owned raw and decoded bytes, the additional title copy, and `sizeof` each cue,
metadata record, and display operation before append. This deliberately counts
fixed records as well as variable payload. Vector capacity, allocator overhead,
temporary track-selection flags, and the per-event decoder result are not a
physical process-memory cap. Metadata records are bounded by staged charges;
there is no separate arbitrary tag count. Allocation exceptions propagate
without publishing a partial result. The caller must budget other simultaneously
retained catalog/lyric payload against the library's total staging ceiling.

The pure media-clock consumer described in section 9 is implemented. Catalog
metadata integration and audio/CLI lyric integration remain unimplemented. The NCN evidence gate remains open. Released
v0.2.0 has no KAR/NCN lyric service.

## 9. Current pure media-clock consumer boundary

`lyrics/LyricMediaConsumer.hpp` receives integer media positions from the
PlaybackSession clock on one serialized control/observation path. It does not
read wall time or access the backend. `prepare` binds a non-null immutable KAR
timeline and clears the traversal; a null input fails without changing an
existing traversal. NoLyrics timelines are valid inputs.

`advance(t)` returns an owning batch of all previously unobserved cues with
media time at or before `t`. Each whole cue retains its ordered operations;
cues with equal times preserve their compiled order. The batch owns the source
timeline and its const spans remain valid while the batch is retained, including
after consumer stop, replacement, or destruction. The consumer does not copy
cue payloads or construct a rendered screen.

Current display state is represented by `observed()`, an owning view of the
emitted cue prefix. A display adapter can replay that prefix's ordered operations.
Equal-position and paused-clock polls return empty new batches. Later positions
beyond the final cue retain the same prefix; completion is controlled only by
PlaybackSession. Consumer errors report Unprepared or BackwardPosition, the
requested position, and the previous position when available. Rejected calls
return an empty batch and leave current position and prefix unchanged.

`reset` clears the position and emitted prefix while retaining the bound
timeline, so a new forward traversal may start at zero. `stop` additionally
releases the binding; a new session requires `prepare`. Old batches remain
historical snapshots and do not represent current display state after reset or
stop. Adapters must use the cleared current state to clear their own display.
Retaining batches can keep old prepared buffers alive; their lifecycle belongs
to the observing control path.

The helper is not a concurrent backend publication protocol, a GUI, or an audio
callback adapter. Shared ownership and serialized method calls do not establish
real-time-safe cross-thread handoff. Running-backend media-position publication has a separate
[proposed bounded handoff design](MEDIA-CLOCK-HANDOFF-CONTRACT.md), which still
requires review and acceptance before implementation. Headless tests feed
actual PlaybackSession frame-derived positions, including pause, completion,
and stop/reprepare; they do not establish device/GUI lyric synchronization.
