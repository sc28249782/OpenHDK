# NCN24 SUB logical EOF amendment proposal

**ID:** OHK-NCN-SUB-030
**Status:** Proposed; requires review and acceptance.
**Target:** OHK-LYR-030 section 4, NCN24 LYR framing only.
**Implementation:** None. The existing strict checker still rejects SUB.

## 1. Decision proposed for review

Treat the first `0x1A` byte in an explicitly selected NCN24 LYR member as logical
EOF. Ignore every byte after it for text/header decoding, scalar counts and
CUR correspondence. The ignored bytes can include CRLF, repeated SUB or other
values. Do not restrict the marker to a physical-file suffix.

This adopts the maintainer's requested DOS/Windows text-stream interpretation.
It is an explicit profile choice, not an encoding fallback or a claim that the
pinned Qt reader has been executed with this behavior. The filename preserves
the initial proposal topic; this revision proposes first-SUB EOF rather than
terminal-suffix recognition.

[Microsoft file translation constants](https://learn.microsoft.com/en-us/cpp/c-runtime-library/file-translation-constants?view=msvc-170)
state that the Microsoft C runtime interprets Ctrl+Z as EOF on input in text
mode. Binary mode suppresses text translations. This supports the historical
convention; it does not establish Qt behavior or a universal NCN specification.
Do not attribute the convention to an inability of DOS/Windows filesystems to
store exact byte lengths.

The [pinned legacy edge analysis](NCN-LEGACY-EDGE-ANALYSIS.md) records that the
HandyKaraoke LYR reader selects TIS-620, skips four lines and calls `readAll`.
It has no explicit SUB handling. Exact Qt/Windows output and widget counting
remain unresolved. No private inventories, song names, paths, hashes or
observations are published here. Source inspection is not runtime proof.

## 2. Proposed processing order

1. Acquire and bound the complete raw LYR member under existing source checks.
   Keep all bytes for source revision/digest, containment and raw-byte accounting.
2. Find the first raw byte equal to `0x1A`. If present, define the logical text
   span as all bytes before it. Exclude the marker and every following byte.
   If absent, the logical text span is the complete member.
3. Parse four terminated header lines within that logical span. Keep the
   existing explicit TIS-620 and C0 validation for text before EOF. Missing
   header terminators fail InvalidText. Bytes after EOF cannot complete headers.
   Retain original header bytes and provenance from the admitted logical span.
4. Decode the remaining logical body as strict TIS-620. Normalize CRLF/CR/LF
   to LF under existing rules. Preserve spaces, empty lines and any terminal
   body newline before EOF. Do not trim the body.
5. Require one CUR position per resulting body scalar under the existing target.
   The EOF marker and ignored remainder consume no cursor positions and produce
   no text cues or LineBreak operations.

This is NCN24 member framing. Do not add SUB removal to shared text decoders,
KAR extraction, `MetadataText`, UTF-8 or general TIS-620 validation.
There is no retry after InvalidText and no second encoding selection.

A SUB inside a header acts as EOF and will normally leave four terminated
headers incomplete. The failure is header termination, not an interior-SUB
text error. A SUB in the body ends lyrics even if ordinary text follows it.
That ignored text must not be used to infer cues or repair count mismatches.
Text errors before EOF still fail. Text-like bytes, undefined TIS-620 bytes,
NUL or other C0 controls after EOF are not decoded.

## 3. Evidence, offsets and bounds

Do not change or rewrite the raw file. Source identity/digest/revision covers
the complete member, including ignored bytes. Two files with identical logical
text and different ignored bytes still have different raw source revisions.

A future normalization record must identify whether logical EOF is physical
or SUB, the marker's original byte offset when present, the logical text length
and the ignored-byte count. Define that count as bytes strictly after the marker;
excluded framing length is marker plus ignored bytes. These fields explain
normalization and are not identity authority or editable metadata.

Use a borrowed span or equivalent bounded view for the prefix. Finding EOF
needs no decoded copy or allocation. Enforce raw source size caps before prefix
selection. Ignored bytes still consume the operation's raw-source ledger.
An oversized file cannot become admissible because its marker appears early.
Existing decoded-text/cue budgets count logical content. No second allowance
or numeric limit is introduced.

Report header/text offsets in the original member coordinate system. Never
return partially normalized lyrics on failure. This rule intentionally hides
post-EOF text from display; preserve its existence and size in provenance.

## 4. Independently authored review vectors

Let H be the seven-byte header `54 0A 41 0A 43 0A 0A`.
`58` and `59` are original ASCII X and Y fixtures. These are proposed results,
not production normalizer or legacy playback receipts.

| Body bytes after H | Proposed result | Logical normalized body | EOF offset; ignored bytes |
| --- | --- | --- | --- |
| empty | NoLyrics with empty CUR | empty | Physical EOF; 0 |
| `58` | Text, one scalar | `58` | Physical EOF; 0 |
| `1A` | NoLyrics with empty CUR | empty | 7; 0 |
| `1A 0D 0A` | NoLyrics with empty CUR | empty | 7; 2 |
| `58 1A` | Text, one scalar | `58` | 8; 0 |
| `58 1A 0D 0A` | Text, one scalar | `58` | 8; 2 |
| `58 0D 0A 1A` | Text plus LF, two scalars | `58 0A` | 10; 0 |
| `58 0A 1A 0D 0A` | Text plus LF, two scalars | `58 0A` | 9; 2 |
| `58 1A 59` | Text, one scalar | `58` | 8; 1 |
| `58 1A 1A` | Text, one scalar | `58` | 8; 1 |
| `58 1A 0A` | Text, one scalar | `58` | 8; 1 |
| `58 1A 0D` | Text, one scalar | `58` | 8; 1 |
| `58 1A 00 FF` | Text, one scalar | `58` | 8; 2 |
| `58 00 1A` | InvalidText, NUL at 8 | No result | EOF does not rescue prefix |
| `58 FF 1A` | InvalidText, undefined TIS-620 at 8 | No result | EOF does not rescue prefix |

A marker before completion of the fourth header is InvalidText for header
termination, including if missing terminators occur after the marker.
Add boundary vectors for SUB in each header, exact source-size caps, oversized
raw members with early EOF, decoded/staging limits, Thai scalars before EOF,
empty logical body with nonempty CUR, and logical count mismatch.
If the logical body has one scalar but CUR has two entries, reject the bundle;
ignored text is not used to satisfy the count.

## 5. Contract amendment and test migration

On explicit acceptance, amend OHK-LYR-030 section 4 with first-SUB logical EOF.
Cross-reference the NCN24 framing exception from section 3. Keep shared C0
validation unchanged. Record proposal acceptance separately from implementation
and closure of the NCN evidence gate.

Update offline checker, inventory and fixture expectations in a separate PR.
Label changed receipt schema/interpretation. Preserve old submitted inventories;
rerun samples for logical-body counts. Do not backfill old receipts.
The existing 33-case manifest and PR #93's strict SUB rejections remain unchanged
in this docs-only proposal. After acceptance, migrate those old expectations
and add prefix-error/header-termination cases. Keep CUR odd/decreasing rejection
vectors. Do not document EOF admission as implemented while tests still assert
the previous strict target.

Future tests must cover unchanged raw evidence, original offsets, EOF provenance,
ignored-byte counts, logical scalar/CUR counts, newline preservation, source and
staging caps, allocation failure and no partial publication. Test reader output,
player rendering and tempo behavior separately.

## 6. Unchanged CUR and open gates

This proposal does not accept odd CUR tails, including a single `FF` byte.
It does not pad words, sort decreasing positions, clamp ticks, filter FF00..FFFF,
duplicate cursors or truncate decoded lyrics to fit CUR. Checked PPQN/24 conversion,
full tempo-map integration and timeline containment keep their current rules.

EOF framing is one profile decision. Representative mapping, sentinel/odd-tail/
decreasing policy, full MIDI/tempo analysis and explicit normalizer admission
remain required. Accepting this document alone does not close the NCN gate or
advertise NCN playback.

Windows checkpoint acknowledgment remains BLOCKED. CLI pinned execution,
playback-controller work and manual validation remain separate. No runtime,
CTest-count, checkbox, dependency, historical evidence bundle or scoped storage
acceptance changes are made.
