# NCN24 optional terminal FF cursor framing proposal

**ID:** OHK-NCN-CUR-030
**Status:** Accepted by maintainer merge confirmation for PR #96 on 2026-10-11.
**Implementation:** Planning contract and schema-3 offline tools; no application normalizer.
**Target:** OHK-LYR-030 section 4, NCN24 CUR member framing only.
**NCN evidence gate:** OPEN. No application parser or normalizer is added.

## 1. Accepted framing decision

Accept two explicit CUR framing forms in the NCN24 profile:

- An even-length member contains complete unsigned 16-bit little-endian words.
  Physical EOF ends the member. No terminator is required.
- An odd-length member is admissible at the framing layer only if its final
  byte is `FF`. Exclude exactly that byte from the cursor-word span. The
  preceding even-length span contains complete little-endian words.

The second form treats a terminal singleton `FF` as a framing marker. It does
not scan for the first `FF`, remove complete words, or infer padding. A
one-byte `FF` member has zero cursor words under this proposal. Empty logical
lyrics plus zero words is NoLyrics; nonempty lyrics plus zero words still fails.

This is an accepted profile choice. Framing success does not establish valid
timing, cursor/lyric correspondence or compatibility with a collection.
The current contract and schema-3 tools apply this framing. Historical schema-2
receipts retain their unframed CUR interpretation.
The accepted first-SUB LYR rule remains separate and unchanged.

## 2. Source evidence and its limits

Source inspection on 2026-10-11 used the following references. It did not run
the converter, Qt, HandyKaraoke or BASS. No upstream code or dependency is ported.

| Reference | Observation | Limit |
| --- | --- | --- |
| [ncn-to-kar.pl, pinned commit 25abf77dfdd4f4a27774ecdb5bc9b1c1b0467508](https://github.com/zw/ncn-to-kar/blob/25abf77dfdd4f4a27774ecdb5bc9b1c1b0467508/ncn-to-kar.pl#L155-L167) | After consuming timing entries for lyrics, the converter reads one byte. It warns if EOF occurs without a terminator, or if the byte is not FF or more bytes remain. | This supports an exact-end singleton marker convention. It does not prove that every CUR requires it or that count-driven parsing is safe for OpenHDK. |
| [The same converter's word/timing loop](https://github.com/zw/ncn-to-kar/blob/25abf77dfdd4f4a27774ecdb5bc9b1c1b0467508/ncn-to-kar.pl#L127-L151) | Reads two bytes, uses little-endian unpacking and scales by MIDI resolution/24. It warns and clamps decreasing converted timestamps to the preceding timestamp. | Repair behavior is not adopted. Floating conversion and fallback behavior do not establish OpenHDK's checked integer rules. |
| [Pinned converter README](https://github.com/zw/ncn-to-kar/blob/25abf77dfdd4f4a27774ecdb5bc9b1c1b0467508/README.md) | Describes conversion to KAR after transcoding lyrics to UTF-8. | This is a migration tool, not an NCN specification or runtime proof of TIS-620 decoding. |
| [HandyKaraoke Utils.cpp, commit 7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Utils.cpp#L18-L49) | Both CUR overloads test atEnd before attempting two byte reads, combine the values and append the converted tick. Neither checks a terminal FF, odd length or read status. | This does not prove marker removal. Failed-second-read output in the pinned Qt build remains unmeasured. |
| [un4seen NCN discussion, reply 6](https://www.un4seen.com/forum/?topic=11559.0) | A user-posted reader loops while position is less than length minus one, reads a pair, and converts by resolution/24. It leaves a singleton final byte unread. | It does not validate the singleton value. Other replies use different rules, including filtering high-byte FF. Forum examples are not an official BASS NCN specification. |

The converter's source header declares GPL-3.0-or-later. Its credited C
template has an unspecified license. This document uses source references;
it copies no implementation and approves no dependency. OpenHDK continues to
exclude BASS, BASS FX, BASSMIDI, BASSmix, BASS_VST and their SDK artifacts.

The converter also prepends a line marker and groups combining marks for KAR
output. Those display conventions are not evidence that OpenHDK's current
scalar/line mapping is correct. Keep framing and mapping decisions separate.
Private inventories, member bytes, hashes, filenames and corpus statistics are
not published in this proposal. A future public evidence record needs separate
disclosure authorization. Source inspection alone does not close the NCN gate.

## 3. Accepted processing order

1. Acquire the complete raw CUR member under the existing containment, identity,
   size and shared operation-budget checks. Retain every raw byte.
2. Let N be the raw length. If N is even, use all N bytes as cursor words.
3. If N is odd and the last byte is FF, use bytes [0, N-1) as cursor words.
   Record marker offset N-1. Exclude exactly one byte, even if preceding bytes
   are also FF. No byte follows this marker.
4. If N is odd and the last byte is not FF, fail InvalidCursor for an incomplete
   word at original byte offset N-1. Never pad, retry or search backward.
5. Decode the selected span as unsigned little-endian 16-bit words. Preserve
   their order. Apply the existing nondecreasing, checked PPQN/24 conversion,
   full tempo-map, primary last-tick and logical lyric-scalar count rules.

Frame from physical length and the final byte before considering lyric counts.
Do not consume only enough words to fit lyrics and call the remainder a trailer.
Do not infer a marker from a plausible time, successful legacy playback or a
matching count. Even-length trailing `00 FF`, `FF FF` and FF runs stay data
under this proposal. They can still fail timing, ordering or count validation.

Reject the whole bundle on any failure. No partially prepared lyrics or
success receipt is returned. A marker cannot repair decreasing words, invalid
LYR text, a count mismatch or a cursor beyond the primary timeline.

## 4. Provenance, source identity and accounting

Record these proposed framing fields separately from live identity authority:

| Field | Physical EOF | Terminal singleton FF |
| --- | --- | --- |
| kind | Physical | TerminalFF |
| marker_byte_offset | null | N-1 |
| logical_cursor_bytes | N | N-1 |
| framing_bytes | 0 | 1 |

The arithmetic is `raw_bytes = logical_cursor_bytes + framing_bytes`.
Logical cursor length is always even. A marker offset is an original-member
offset, not a cursor index. Marker selection does not alter offsets of preceding
words; index i remains byte offset 2*i.

Digest/source revision covers the complete raw member, including the marker.
Identical word spans with and without a marker have different raw revisions.
Do not rewrite source files, remove a marker on disk or replace raw bytes with
the logical span in identity checks.

Use a borrowed span. No copied prefix or second allocation allowance is needed.
Check the complete raw size before framing. The marker consumes raw-source
budget, although it creates no word, text cue or LineBreak. An oversized raw
member remains oversized even when excluding one byte would fit a logical cap.

## 5. Independently authored review vectors

These literal examples are original fixtures, not private song excerpts.
Assume valid four-header LYR framing and a logical body with the stated scalar
count. Use PPQN 192, default 500000 microseconds per quarter note and primary
last tick 600000. Thus tick = 8*u. Candidate means all stated target checks
pass; it does not mean representative compatibility has been established.

| Case | Raw CUR hex | Body scalars | Framing; complete words | Proposed target result |
| --- | --- | ---: | --- | --- |
| empty | empty | 0 | Physical; none | NoLyrics |
| marker-only | FF | 0 | TerminalFF at 0; none | NoLyrics |
| marker-only-text | FF | 1 | TerminalFF at 0; none | Count mismatch |
| plain-quarter | 18 00 | 1 | Physical; 24 | Candidate; tick 192, media 500000 |
| marked-quarter | 18 00 FF | 1 | TerminalFF at 2; 24 | Candidate; same tick/media, different raw revision |
| low-FF-data | FF 00 | 1 | Physical; 255 | Candidate; tick 2040, media 5312500 |
| high-FF-data | 00 FF | 1 | Physical; 65280 | Candidate; tick 522240, media 1360000000 |
| full-FFFF-data | FF FF | 1 | Physical; 65535 | Candidate; tick 524280, media 1365312500 |
| full-word-plus-marker | FF FF FF | 1 | TerminalFF at 2; 65535 | Candidate; only final byte excluded |
| ordinary-singleton | 18 | 1 | Incomplete word at 0 | InvalidCursor |
| non-FF-tail | 18 00 00 | 1 | Incomplete word at 2 | InvalidCursor |
| interior-FF | FF 00 18 00 | 2 | Physical; 255, 24 | Decreasing at index 1, offset 2 |
| decreasing-marked | 30 00 18 00 FF | 2 | TerminalFF at 4; 48, 24 | Decreasing at index 1, offset 2 |
| equal-marked | 18 00 18 00 FF | 2 | TerminalFF at 4; 24, 24 | Candidate; preserve equal-time order |
| extra-complete-word | 18 00 30 00 FF | 1 | TerminalFF at 4; 24, 48 | Count mismatch; no trailer inference |
| even-FF-run | 18 00 FF FF FF FF | 1 | Physical; 24, 65535, 65535 | Count mismatch; no padding removal |
| odd-FF-run | 18 00 FF FF FF | 1 | TerminalFF at 4; 24, 65535 | Count mismatch; exclude one byte only |

Add a last-tick boundary variant: `18 00 FF` with primary last tick 191 fails
timeline containment despite valid framing. Add raw-size cap and cap-plus-one
vectors, including an over-cap marked member whose logical span would fit.
Test marker-only with a nonempty body, empty-body extra words, unchanged raw
hashes, original error offsets and allocation failure without partial results.

## 6. Migration after explicit acceptance

PR #96 was docs-only and accepted this decision. The separate migration now
updates OHK-LYR-030 and offline tools. The former 51-case schema-2 manifest is
historical; schema 3 contains those cases plus 18 original CUR framing vectors.
Previously submitted inventories remain byte-unchanged.

The migration requirements are:

1. Amend section 4 to describe optional terminal singleton framing. Preserve
   the unchanged rules from section 7 below.
2. Add literal vectors from section 5 and independent arithmetic checks. Update
   odd-FF expectations without weakening non-FF-tail rejection.
3. Version fixture/checker and inventory output explicitly as schema 3 for the
   new CUR interpretation. Keep observation-record schema 1 unless its own
   fields need an amendment. Its verifier must distinguish historical schema-1
   strict text, schema-2 first-SUB text with unframed CUR, and schema-3 framed CUR.
4. Keep raw odd-length/anomaly observations alongside framing provenance. Do
   not hide raw tails, remove complete high-FF words or relabel old receipts.
5. Preserve submitted inventories byte-for-byte. Rerun lawful samples into new
   files and bind new observations to the exact new inventory hash.

No synthetic checker result proves lyric/cursor mapping. Collect representative
line/scalar and tempo observations before normalizer admission. A bounded tail
inventory proves bytes/parity only; it does not prove payload alignment,
trailer meaning, full word validity or legacy rendering equivalence.

## 7. Decisions not included

This proposal does not accept complete-word sentinels, FF padding runs,
text-like trailers, first-FF EOF, high-byte filtering or arbitrary odd tails.
It does not sort or clamp decreases, duplicate cursors, truncate lyrics, infer
graphemes or change one-position-per-logical-scalar mapping. Checked integer
PPQN/24 conversion and compiled full-tempo-map integration remain unchanged.

NCN evidence and normalizer admission remain OPEN. CLI pinned evidence,
playback-controller work and manual validation are separate. Windows checkpoint
acknowledgment remains BLOCKED. The original proposal changed no runtime or tooling. The separate schema-3
migration changes offline tests, fixtures and interpretation labels only. It
changes no application runtime, dependency, suite count, checkbox, historical
evidence bundle or revision-bound storage acceptance.


## 8. Schema 3 tooling migration record

The manifest has 69 cases: 51 prior cases plus the 17 section-5 framing vectors
and a marked beyond-primary variant. Its four MIDI vectors remain unchanged.
Literal framing expectations are checked separately from candidate expectations.
Checker receipts and inventory add selected framing provenance or an explicit
OddLength framing rejection. Raw words, trailing-byte and high-FF statistics
remain visible even when framing is rejected; inventory is not bundle admission.

Fixture/checker and inventory output use schema 3. Old fixture schemas 1 and 2
are rejected by the current checker. The observation record remains schema 1.
Its verifier accepts inventory schemas 1, 2 and 3 with separate text/cursor
interpretation labels and framing arithmetic checks. It verifies supplied
structure/hash binding, not whether a marker actually exists in raw bytes.
CountMismatch harness offsets now name the end of the logical word span.
No submitted receipt is relabeled. Rerun private samples into new files.
