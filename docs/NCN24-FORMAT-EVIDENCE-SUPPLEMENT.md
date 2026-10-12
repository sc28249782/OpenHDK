# NCN24 format evidence supplement

Supplement ID: `OHK-NCN-EVIDENCE-030`
Related contract: [OHK-LYR-030](LYRIC-TIMELINE-CONTRACT.md), sections 4–7
Status: Reviewed proposal; offline synthetic fixture/checker tooling implemented.
NCN evidence gate: **OPEN**. Representative bundle observations and explicit
review remain Pending. No NCN parser or normalizer is added by this supplement.

## 1. Scope and source provenance

This supplement records disagreements that must be resolved before implementing
NCN24. It does not change the accepted planning target or declare generic NCN
compatibility. The inspected OpenHDK baseline is
`395b453d8ab79c77046e7a47b0de779ef7ed487c`.

| Source | Pin and observation scope |
| --- | --- |
| [DisNCord Wiki](https://wiki.restless.systems/index.php?title=NCN_(Karaoke_Format)&oldid=317) | Revision 317, dated 2022-11-29; retrieved 2026-10-11. Secondary format description. |
| [HandyKaraoke Utils.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Utils.cpp) | Pinned source, both CUR overloads and LYR readers. Source inspection; no legacy execution claimed. |
| [HandyKaraoke LyricsWidget.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Widgets/LyricsWidget.cpp) | Same pin; `setLyrics`, `setLyricsTemp`, cursor positioning and character widths. UI behavior is distinct from raw format rules. |
| [Sukoom2001 article](https://sukoom2001.wordpress.com/2005/02/15/รูปแบบไฟล์-cur-ของ-nick-karaoke/) | Post dated 2005-02-15 republishes a Nick Karaoke explanation; original authorship is not independently authenticated. Timing examples appear in comments dated 2010-10-02 and 2010-10-15. |
| [Qt 5.15 QString documentation](https://doc.qt.io/archives/qt-5.15/qstring.html) | UTF-16 representation explains why legacy string length is not a general Unicode-scalar or grapheme guarantee. API semantics, not an NCN corpus result. |

The original supplement did not count an unspecified BASSMIDI forum discussion
as corroboration. The later [CUR framing amendment](NCN24-CUR-FRAMING-PROPOSAL.md)
records the exact NCN thread and reply scope as community source examples,
not an official BASS format specification.
No upstream source, BASS dependency or Qt dependency is imported.

| Question | Current evidence | NCN24 target / unresolved point |
| --- | --- | --- |
| Bundle members | Sources describe MID, LYR and CUR. | Explicit root layout and containment remain governed by OHK-LYR-030. |
| LYR header | Legacy reader consumes four lines with TIS-620. | Strict termination, retained bytes and normalization need independent vectors and bundle checks. |
| CUR representation | Pinned reader combines low/high bytes as unsigned little-endian. | Non-FF odd-tail/order validation remains strict; the separate accepted CUR amendment admits optional terminal singleton FF framing. |
| Timing | Pinned reader uses `floor(u*q/24)`. Wiki differs. | Retain the musical-unit target pending discriminating observations. |
| Character mapping | Legacy widget has line state and blank-line repair. | One scalar, including LF, per position is a target, not a proven universal rule. |
| High byte FF | Historical snippet filters it; pinned reader does not. | No silent filtering in the current target. Resolve corpus behavior before implementation. |
| Runtime support | Current source policy is SMF/KAR only. | NCN parser, normalizer and catalog preparation remain unimplemented. |

## 2. Timing hypotheses and discriminating observations

Let `q` be PPQN, `t` a MIDI tick, `u` an unsigned CUR value and `B` constant BPM.
The pinned reader implements the musical-unit candidate:

`tick = floor(u * q / 24)`.

The Wiki gives `C = (t / q) / B * 24 * 60`. Algebraically, at constant tempo,
this equals `24 * elapsed_seconds`. That is an inference about the expression,
not proof that NCN is a 24-Hz format. The 2010 comment gives
`CurTime = elapsed_seconds * Tempo * 24 / 60`, consistent with musical units
at constant tempo. Neither formula alone validates a changing-tempo file.

For one quarter note (`t=q`):

| BPM | Elapsed seconds | Wiki expression | Musical-unit candidate |
| --- | --- | --- | --- |
| 60 | 1 | 24 | 24 |
| 90 | 2/3 | 16 | 24 |
| 120 | 1/2 | 12 | 24 |
| 180 | 1/3 | 8 | 24 |

A BPM-60 observation alone cannot separate the candidates. Compare events at
known musical positions at other tempos, with independently observed legacy
cursor behavior. Do not change OpenHDK's divisor merely to match a Wiki formula.

With PPQN 480, tempo 500,000 microseconds/quarter through tick 480, then
1,000,000, positions `u=24,48,72` map to ticks `480,960,1440` and media times
`500000,1500000,2500000`. At tick 960, multiplying total elapsed time by the
current BPM 60 gives 36 cursor units, rather than musical position 48. Integrate
the complete primary tempo map; do not substitute current BPM times elapsed time.

Unsigned 16-bit storage has 65,536 values, from 0 through 65,535. The maximum
value does not establish that every song is scaled to a fixed duration. Check
range, end-of-primary rejection and time interpretation separately.

## 3. Character, line and high-byte hypotheses

The 2005 snippet skips words whose **high byte** is FF, anywhere its predicate
matches. This covers `0xFF00` through `0xFFFF`, not only `0xFFFF`. The pinned
reader retains those words. A low byte FF is a different case. Do not treat a
comment about trailing data as evidence that all matching words are padding.

The 2010 sample includes a per-line entry iteration before characters.
The pinned widget counts line length plus one and inserts duplicated cursors
for empty lines. These are competing UI/mapping observations; they do not prove
that the serialized format contains one normalized LF cursor after each line.
Matching total counts alone does not establish matching cursor placement.

For valid TIS-620 Thai BMP characters, source bytes, Unicode scalars and UTF-16
units commonly have equal counts. Thai combining tests can distinguish grapheme
coalescing, but cannot by themselves establish byte-versus-scalar semantics.
Test leading/trailing/blank lines, spaces and CRLF alongside exact Thai bytes.
Keep composed and decomposed spelling distinct. Do not normalize text, duplicate
positions, truncate a mismatch or infer missing timing to make a sample pass.

## 4. Independently authored byte vectors

These synthetic vectors are authored independently of a production NCN encoder
or normalizer. Expected results describe the existing NCN24 target. They do
**not** establish compatibility with a real NCN collection. Hex is complete;
spaces in tables separate bytes only.

### Primary MIDI vectors

Each is format 0, one track, with a complete End-of-Track event. M480/M3/Mhigh
use the SMF default tempo. Mtempo has explicit tempo changes.

| ID | Complete SMF hex | Last tick / media microseconds |
| --- | --- | --- |
| M480 | `4d546864000000060000000101e04d54726b000000058b20ff2f00` | 1440 / 1500000 |
| M3 | `4d546864000000060000000100034d54726b0000000403ff2f00` | 3 / 500000 |
| Mhigh | `4d546864000000060000000101e04d54726b00000006cfff6cff2f00` | 1310700 / 1365312500 |
| Mtempo | `4d546864000000060000000101e04d54726b0000001400ff510307a1208360ff51030f42408740ff2f00` | 1440 / 2500000 |

### LYR/CUR vectors

Prefix every body below with header **H**: `54 0a 41 0a 43 0a 0a`
(`T`, `A`, `C`, empty reserved line; four LF terminators). Body starts at
member byte offset 7. Use M480 unless another primary is specified. Display
notations are explanatory; the hex defines source bytes.

| Body hex / interpretation | CUR hex | Target expectation / comparison |
| --- | --- | --- |
| Empty | Empty | NoLyrics. |
| `41` / A | `18 00` | Tick 480; 500000 microseconds. |
| `41 0a 42` / A LF B | `18 00 30 00 48 00` | Text, LineBreak, Text at ticks 480/960/1440. |
| `41 0d 0a 42` / A CRLF B | Same three positions | Normalize to the preceding three scalars, despite four raw body bytes. |
| `41 0d 42` / A CR B | Same three positions | Same normalized actions. |
| `41 0a` / terminal LF | `18 00 30 00` | Two positions; compare with A without terminal LF. |
| `41 0a 0a 42` / blank line | `00 00 18 00 30 00 48 00` | Four positions; preserve both breaks, no cursor repair. |
| `41 20 42` / space | `18 00 30 00 48 00` | Three positions; preserve space. |
| `41 09 42` / tab | Same three positions | Preserve tab; no display width guarantee. |
| `41` / single-line A | `18 00 30 00` | Count mismatch under strict target; compare per-line entry-marker hypothesis on real bundles. |
| `a1 d4 e8` / กิ่ | `18 00 30 00 48 00` | Three scalars U+0E01/U+0E34/U+0E48, not one grapheme position. |
| `a1 d3` / กำ | `18 00 30 00` | Two scalars. |
| `a1 ed d2` / กํา | `18 00 30 00 48 00` | Three scalars; do not equate with the previous spelling. |
| `41 42` / AB | `18 00 18 00` | Equal positions retain source order. |
| `41 42 43` / ABC, M3 | `07 00 08 00 08 00` | Ticks 0/1/1; microseconds 0/166666/166666. |
| `41 42 43` / ABC, Mtempo | `18 00 30 00 48 00` | Tempo-map times 500000/1500000/2500000. |
| `41 42` / AB | `18 00 17 00` | Decreasing position fails; offending CUR index 1, byte offset 2. |
| `41` / A | `18` | Odd byte length fails; dangling CUR byte offset 0. |
| `00` or undefined TIS-620 `db` | `18 00` | InvalidText at body byte 0/member byte 7. |
| `41` / A | `49 00` | Beyond M480 last tick fails. |

Also test fewer than four terminated header lines, every header newline form,
nonempty body with empty CUR and empty body with nonempty CUR. Errors must name
the member and offset; no partial timeline is returned. Error categories and
precise offsets require reviewed normalizer tests before implementation claims.

Use Mhigh and body `41` for each following CUR word separately. This prevents
end-of-primary rejection from masking high-byte behavior:

| CUR hex | Unsigned value | Tick | Pinned reader vs historical filter |
| --- | --- | --- | --- |
| `ff 00` | 255 | 5100 | Both retain; low byte FF. |
| `ff fe` | 65279 | 1305580 | Both retain. |
| `00 ff` | 65280 | 1305600 | Pinned reader retains; historical filter skips. |
| `ff ff` | 65535 | 1310700 | Pinned reader retains; historical filter skips. |

Current NCN24 target treats each as an unsigned position, subject to ordering,
counts and primary bounds. Also compare FF-high words in the middle and at the
tail of lawful bundles. Sentinel behavior remains a corpus question.

## 5. Bounded comparison procedure and privacy

Before running a future harness, review explicit member, aggregate staging,
record/output and timeout limits. Charge coexisting input/decoded/expected
buffers under one allowance. Record every limit and reject excess without
partial results. Logical byte accounting is not a total process-memory bound.
This docs-only proposal introduces no runtime limit or new storage admission.

For every lawful representative bundle, record:

1. Opaque sample ID, permission basis, member lengths/hashes, selected reader
   revision and execution environment. Keep private members outside Git.
2. PPQN, complete tempo map and primary last tick; raw CUR values/index/offset,
   high-byte distribution, LYR header boundaries and normalized body counts.
3. Selected body offsets/scalars/actions and candidate MIDI ticks/media times.
   Include non-60 BPM, tempo changes, Thai combinations and line transitions.
4. Independently captured legacy cursor observations at those positions. Record
   whether rendering, seeking or line repair changes the observation. A source
   snippet, matching count or synthetic vector is not a legacy playback result.
5. Per-case expected/observed status, command/child exit receipts, skipped cases
   with reasons, and before-cleanup bounded captures where publication is lawful.

Do not redistribute songs merely because they can be read. Use original
synthetic fixtures for committed bytes. Publish only authorized observations;
omit lyrics, absolute paths, usernames and raw private members. If redaction
is needed, retain original/published hash mapping and replacement counts. Hashes
prove consistency of supplied bytes, not independent source/run authentication.
Independent tools must report actual observations separately from injected cases.

## 6. Offline fixture tooling

[Original fixture tools](../tests/fixtures/ncn24/README.md) now materialize the
four MIDI vectors and 69 explicit LYR/CUR cases. The bounded Python checker
compares independently authored expected scalars, source offsets, positions,
ticks and rational media times. It includes both M3 `7,8,8` -> `0,1,1` and
`1,2,3` -> `0,0,0`. A separate C++ probe cross-checks exported primary/Thai
bytes with the existing OpenHDK SMF/timeline/text APIs.

These tools validate synthetic target expectations. They are not an application
NCN parser/normalizer or a private-song/legacy-playback analyzer. Per-line marker
and high-byte filtering compatibility remains unresolved. All successful outputs
retain `ncn_acceptance=Pending`; representative observations remain Pending.
Tool bounds, diagnostic precedence and export effects are explicit in the tool
README. Production profile limits/error APIs remain unchanged.

The [representative observation runbook](NCN-REPRESENTATIVE-OBSERVATION-RUNBOOK.md)
now supplies a bounded read-only member inventory and a supplied-record verifier.
They do not run a legacy player or validate a primary tempo map. Tool tests use
synthetic members/mock records; actual representative execution remains Pending.

## 7. Review decisions and work order

| Gate item | Status after this proposal |
| --- | --- |
| Source disagreement and constant-tempo algebra | Recorded; offline fixture comparison implemented. |
| Independent vector execution against future NCN normalizer | Pending; no normalizer exists. |
| Representative timing, line/Thai and high-byte observations | Pending. |
| Count/line-position and sentinel policy acceptance | Pending; amend OHK-LYR-030 first if evidence conflicts. |
| NCN24 implementation admission | OPEN; no support claim. |

Review the fixture/checker tooling, then obtain lawful representative
observations. Review whether the strict profile is useful
and accurate; revise its mapping or introduce an explicitly named profile if
needed. Do not add a silent tolerant fallback. Close the existing evidence gate
only with the required observations and explicit acceptance, then implement the
normalizer and catalog/preparation/clock integration in separate slices.

Pinned management/preparation CLI execution and playback-controller work remain
separate pending tracks. This supplement does not advance the accepted native
provider/factory/mutation revisions, close Windows checkpoint acknowledgment,
change roadmap checkboxes or replace manual device/lyric validation.

## Legacy edge hypotheses

The [legacy edge analysis](NCN-LEGACY-EDGE-ANALYSIS.md) examines pinned reader handling
of SUB text, incomplete CUR words and decreasing cursor positions.
Four original edge vectors and 32 offline tests apply first-SUB EOF and optional
terminal FF framing while preserving non-FF odd-tail/decreasing rejection and
checking EOF/framing/limit/schema boundaries.
No measured mapping, authenticated playback or normalizer acceptance is claimed.
NCN evidence remains OPEN; no contract, CTest-count or checkbox change.

## Accepted NCN24 SUB logical EOF tooling

The [first-SUB EOF amendment](NCN24-TERMINAL-SUB-PROPOSAL.md) was accepted with PR #94.
The planning contract and offline fixture/checker/inventory tools now select
logical text before the first `1A`. They retain complete raw members for hashes,
source caps and accounting. Shared text decoders remain unchanged. The separate
CUR framing amendment is described below.
The earlier SUB-only migration used schema 2. Current fixture/receipts and
inventory use schema 3, including the separate accepted CUR amendment.
The observation verifier
labels schema 1 as historical strict-C0 interpretation and schema 2 as first-SUB
EOF; it does not reinterpret or backfill old receipts.
No application NCN parser/normalizer exists. NCN evidence remains OPEN, with
runtime counts, release checkboxes, historical bundles and storage acceptance
revisions unchanged.

## Accepted NCN24 optional CUR terminator tooling

The [OHK-NCN-CUR-030 amendment](NCN24-CUR-FRAMING-PROPOSAL.md) was accepted with PR #96.
The contract and schema-3 offline tools now select complete little-endian words
at physical EOF, or exclude exactly one terminal FF byte from odd-length CUR.
Non-FF odd tails still reject. Complete FF words, padding/trailer hypotheses,
ordering, mapping and timing rules remain unchanged. Raw member hashes, caps
and shared accounting include the marker. The manifest has 69 cases (51 prior
plus 18 CUR framing vectors); current checker input rejects schemas 1 and 2.
Inventory keeps raw trailing-byte statistics beside framing provenance.
The observation record remains schema 1; its verifier labels inventory schemas
1/2 as Unframed CUR and schema 3 as OptionalTerminalFF, with arithmetic checks.
Preserve submitted receipts and rerun samples into new files. No application
NCN normalizer exists; the evidence gate remains OPEN. Runtime/suite counts,
checkboxes, historical bundles and storage acceptances do not change. Windows
checkpoint acknowledgment remains BLOCKED.
