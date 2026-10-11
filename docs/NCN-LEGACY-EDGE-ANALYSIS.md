# NCN legacy edge analysis

Status: source inspection and original synthetic regression tests;
NCN evidence gate OPEN. No contract amendment or normalizer admission.

## Basis

This analysis examines three edge hypotheses: a SUB byte in lyric text, an
incomplete final CUR word, and decreasing complete CUR words. No private
inventory, member hashes, song identifiers or playback records are published.
Original fixtures below do not establish representative compatibility.

Inspect the exact HandyKaraoke revision
`7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8`:

- [Utils.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Utils.cpp#L18-L85): both CUR overloads and LYR overloads.
- [LyricsWidget.cpp](https://github.com/sc28249782/HandyKaraoke/blob/7b9a6e2e8a69c0854298a8387f1ffbf2f16d15d8/Widgets/LyricsWidget.cpp#L75-L322): setup, sequential update and seek.

These are source references. OpenHDK did not build or run Qt, HandyKaraoke or
BASS for this analysis. No legacy implementation code or dependency is imported.
The original synthetic tests use existing OpenHDK evidence helpers.

## What the source establishes

| Question | Source observation | Unresolved behavior |
| --- | --- | --- |
| LYR `1a` | Reader selects TIS-620, reads four lines and calls `readAll`. It has no explicit SUB strip or strict C0 validation in these functions. | Exact Qt/Windows text decoding and glyph handling; whether this byte has a format meaning. |
| Odd final CUR byte | Loop checks `atEnd` before the pair, initializes both byte variables, attempts two reads and appends without checking stream status. It has no odd-length rejection. | Actual failed-second-read output and stream status in the pinned build; no zero-padding behavior is asserted here. |
| Decreasing values | Reader appends in input order with `floor(u*resolution/24)`. It does not sort or reject decreases. | Whether the decrease is intentional format data, tolerated damage or another convention. |
| Sequential rendering | Widget returns if the current tick is below the next entry. It otherwise advances its cursor index through rendering branches. It does not globally validate sorted input. | Callback cadence and exact rendered mapping around the decreasing pair. |
| Seeking | Seek walks a prefix while its entries are less than the requested tick. | Nonmonotonic entries can affect the prefix; uninterrupted playback success does not prove seek equivalence. |
| Blank lines | Setup adds a space and duplicates a cursor for eligible empty lines. | UI adjustments are not proof of one raw CUR value per normalized scalar. |

The trailing single `ff` is not a complete `ffff` word. Do not merge this
question with the historical FF00..FFFF filter hypothesis.
At PPQN 192, original synthetic values 48, 47 and 49 map to ticks 384,
376 and 392 under musical-unit conversion. The decrease remains 8 ticks.
No media duration is inferred without the full tempo map.

## Original regression vectors

[legacy-edge-vectors.json](../tests/fixtures/ncn24/legacy-edge-vectors.json)
contains four small authored cases: SUB with a trailing newline, odd FF tail,
decreasing complete words, and combined anomalies. Their expected strict
rejections are literal data. They are not copied song members or outputs from
a legacy player. The test checks diagnostic precedence without treating it as
a new production error API.

Run:

```bash
python3 tests/ncn_legacy_edge_tests.py
```

Six tests verify the strict rejection, absence of guessed SUB termination,
distinction between an incomplete FF tail and a complete FF word, unchanged
input ordering, tick conversion and coexistence of inventory anomalies.
Existing 33-case fixtures and their count remain unchanged.
These tests do not emulate Qt or establish legacy acceptance.

## Decision still required

Keep the current contract until an explicit review selects its amendment.
Collect reader output, selected cursor observations, a full primary timeline
and line/scalar mapping first. Record terminal control-byte and odd-byte
handling as explicit profile decisions if supported by evidence.
Do not silently strip SUB, pad an incomplete word, sort cursor positions,
clamp decreasing ticks or truncate text to make counts agree.
A narrow strict profile can remain valid while excluding legacy edge cases;
its compatibility scope must be stated accurately.

CLI pinned evidence, playback-controller integration, Windows checkpoint
acknowledgment and manual validation remain separate tracks. Historical storage
acceptances, evidence bundles and release checkboxes do not advance.

## Proposed NCN24 SUB logical EOF

The [terminal SUB amendment proposal](NCN24-TERMINAL-SUB-PROPOSAL.md) defines first-SUB
logical EOF for LYR, excluding the marker and all following bytes from text
decoding and Cursor counts while retaining the bounded raw member. It requires
review and acceptance. Current strict fixtures still reject SUB; no parser,
contract rule, CUR policy or acceptance gate changes in this proposal.
NCN evidence remains OPEN; runtime, counts, checkboxes and historical evidence
remain unchanged.
