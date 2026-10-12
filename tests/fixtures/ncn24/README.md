# Original NCN24 synthetic fixture evidence tools

Status: implemented offline fixture tooling; **NCN acceptance Pending**.
Related proposal: [OHK-NCN-EVIDENCE-030](../../../docs/NCN24-FORMAT-EVIDENCE-SUPPLEMENT.md).

`vectors.json` contains original, explicitly authored hex and expected values.
No song collection, legacy encoder output or production NCN normalizer generated
these bytes. Fixture bytes and tooling use the repository's GPL-3.0-or-later
license. The synthetic title/artist/key and Thai spellings are test data.

## Run the checker

Run from the repository root:

```sh
python3 tests/check-ncn24-fixtures.py
```

Expected result: `fixture-verified`, 69 cases, four primary vectors and both
`representative_observations` and `ncn_acceptance` equal to `Pending`.
Receipts contain case IDs and member hashes, not lyrics or input/output paths.
An expected rejection is a successful fixture comparison, not an accepted song.
The complete matrix is required; a missing, duplicate or unknown case fails.

Run the negative and boundary tests:

```sh
python3 tests/ncn24_fixture_tools_tests.py
```

Expected result: 21 tests pass. They include changed timing, endian, high-byte,
scalar and byte-offset expectations; incomplete cases; malformed JSON/hex/SMF;
limits; export refusal and no partial/private output on checker failure.

## Export synthetic bundles for later observations

Choose a new output directory whose parent already exists:

```sh
python3 tests/check-ncn24-fixtures.py --export /tmp/openhdk-ncn24-fixtures
```

The checker validates all expectations before writing. Export creates 69 separate
case roots, each with `Song/example.mid`, `Lyrics/example.lyr` and
`Cursor/example.cur`. The 207 files include intentional rejection cases. Existing
targets, including symlinks, are never overwritten. A filesystem failure can
leave a partial new export directory; inspect it and select a new directory.
The tool does not remove or modify an existing directory. No absolute path is
included in its response.

For an independent cross-check with the existing OpenHDK SMF and text APIs,
compile the fixture probe:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -I. tests/ncn24_fixture_smf_probe.cpp -o /tmp/ncn24-fixture-probe
```

Run the probe against the exported files:

```sh
/tmp/ncn24-fixture-probe /tmp/openhdk-ncn24-fixtures
```

Expected result: four SMF and three TIS-620 vectors verified. The probe reads
only selected synthetic files. It does not parse CUR or build NCN timelines.
Linux CI additionally runs the probe with ASan/UBSan. These offline checks do
not add a CTest suite or change the 32 core / 35 audio CTest counts.

## Tool bounds and diagnostic semantics

| Resource | Limit |
| --- | --- |
| Raw UTF-8 JSON manifest | 128 KiB, regular file only, read with an explicit cap |
| One decoded member | 4 KiB |
| Aggregate decoded source bytes across all cases | 64 KiB |
| Positions in one case | 256 |
| Restricted SMF events / VLQ bytes | 128 / 4 |
| Serialized receipt output | 64 KiB |
| Shared logical staging allowance | 512 KiB |
| Exported source bytes | 256 KiB |

The shared staging reservation includes the raw manifest, parsed JSON strings,
keys and numeric payload, decoded member allowance, retained/serialized receipt
allowances and conversion/work buffers. Export streams one case at a time.
These logical payload/resource bounds do not measure total Python/C++ process
memory, container overhead, allocation overhead or filesystem cache usage.
The receipt reports configured limits and the conservative reserved total.

The independent SMF reader deliberately supports only format 0, one track,
PPQN, tempo metadata and End-of-Track. Other events fail as unsupported fixture
input. It is not a general MIDI parser or a private-song analysis tool.
The candidate scalar model uses strict Python TIS-620 with the contract's C0
policy, then normalizes CRLF/CR/LF. It computes checked bounded unsigned positions
and exact rational tempo-map times. It never edits the fixture or inserts cursors.

Diagnostic precedence here is LYR decoding/header, CUR framing, order/primary
bounds, then scalar correspondence. `CountMismatch` points to the end of the
logical word span in original member coordinates (the marker offset if present).
Offsets are zero-based member byte offsets; normalized LF from CRLF points to CR.
These names/offset choices are harness diagnostics, not a new production parser
error contract. No partial candidate is returned on rejection. A future
normalizer needs its own reviewed error/API contract and these independent
expected values as an oracle.

## Remaining evidence gate

A verified fixture says that independent bytes, expected arithmetic and current
SMF/text APIs agree for the specified target. It does not establish that real
NCN files use that target. The per-line entry-marker and high-byte FF alternatives
remain recorded disagreements; this checker does not simulate or authenticate
a legacy UI. Exported synthetic cases can assist a separately documented legacy
experiment, but are not representative corpus observations.

Next collect lawful representative observations with reader version, complete
tempo map, cursor/line positions, command/exit receipts and privacy/redaction
records. Do not distribute private songs. Review mapping/sentinel rules and
amend OHK-LYR-030 if needed before implementing a production normalizer.
NCN gate remains OPEN; native storage acceptances and Windows checkpoint gate
are unchanged. CLI evidence/playback work remains a separate track.

For lawful real members, use the separate [representative observation runbook](../../../docs/NCN-REPRESENTATIVE-OBSERVATION-RUNBOOK.md).
Its inventory is read-only and counts hypotheses; its verifier checks supplied
record structure/binding. Neither runs playback, validates the full tempo map
or accepts NCN. Do not submit synthetic/mock data as representative evidence.

## Historical schema 2 EOF migration

The earlier SUB-only manifest had 51 cases and schema 2. Its literal expectations apply the
accepted first-SUB logical EOF framing before header/body decoding. Existing
33 vectors remain, with 18 new EOF/header cases. Complete raw hashes and source
limits are unchanged. Checker receipts add logical EOF kind, original marker
offset, logical text byte length and ignored-byte count. Schema-1 manifests
are rejected by the current checker rather than silently reinterpreted.

Inventory schema 2 used the same framing and emitted `lyr.logical_eof` on both
CountsObserved and header/text failures. `logical_body_bytes` describes the
logical body only. The observation verifier still accepts schema-1 inventories
as `StrictC0` and schema-2 inventories as `FirstSubEof`; record schema stays 1.
Those labels are interpretation, not compatibility or authentication claims.
Never edit old supplied receipts. Rerun the collector for new logical counts.

Run `python3 tests/ncn_legacy_edge_tests.py` for 32 migrated edge/boundary tests.
No runtime normalizer, legacy execution or NCN acceptance is implied.


## Schema 3 optional CUR terminator migration

The accepted [CUR amendment](../../../docs/NCN24-CUR-FRAMING-PROPOSAL.md)
adds 18 CUR framing vectors to the preceding 51 cases. Schema 3 has 69 cases,
four unchanged MIDI primaries and 932 decoded bytes. Literal `cur_framing`
expectations are validated independently of candidate expectations. Receipts
include LYR EOF and CUR framing provenance even when candidate checks reject.
Old manifest schemas 1 and 2 fail explicitly in the current checker.

Inventory schema 3 adds `cur.framing`. Selected framing has `status=Selected`,
`kind=Physical|TerminalFF`, a nullable `marker_byte_offset`,
`logical_cursor_bytes` and `framing_bytes`. Non-FF odd tails report only
`status=Rejected`, `code=OddLength` and the original dangling `byte_offset`.
The collector still reports raw complete-word and trailing-byte statistics.
A successful inventory exit does not imply valid framing or a compatible bundle.
Member length/hash and raw-source limits include all bytes.

The observation record remains schema 1. Its verifier accepts inventory schemas
1, 2 and 3 without relabeling supplied bytes. Text interpretation is StrictC0
for schema 1 and FirstSubEof for schemas 2/3. Cursor interpretation is Unframed
for schemas 1/2 and OptionalTerminalFF for schema 3. Arithmetic and supplied
hash binding do not authenticate raw marker bytes or playback.

The 84 offline Python tests include non-FF odd tails, complete FF words,
marker-only, raw caps before framing, hash/ledger differences, logical count
errors, ordering/last-tick/floor behavior and provenance tampering. Source bytes
are never rewritten. Keep old inventories byte-unchanged and rerun into new
files. Representative mapping/sentinel/tempo review and NCN admission stay OPEN.
