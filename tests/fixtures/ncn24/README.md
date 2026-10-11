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

Expected result: `fixture-verified`, 33 cases, four primary vectors and both
`representative_observations` and `ncn_acceptance` equal to `Pending`.
Receipts contain case IDs and member hashes, not lyrics or input/output paths.
An expected rejection is a successful fixture comparison, not an accepted song.
The complete matrix is required; a missing, duplicate or unknown case fails.

Run the negative and boundary tests:

```sh
python3 tests/ncn24_fixture_tools_tests.py
```

Expected result: 20 tests pass. They include changed timing, endian, high-byte,
scalar and byte-offset expectations; incomplete cases; malformed JSON/hex/SMF;
limits; export refusal and no partial/private output on checker failure.

## Export synthetic bundles for later observations

Choose a new output directory whose parent already exists:

```sh
python3 tests/check-ncn24-fixtures.py --export /tmp/openhdk-ncn24-fixtures
```

The checker validates all expectations before writing. Export creates 33 separate
case roots, each with `Song/example.mid`, `Lyrics/example.lyr` and
`Cursor/example.cur`. The 99 files include intentional rejection cases. Existing
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

Diagnostic precedence here is LYR decoding/header, CUR length, order/primary
bounds, then scalar correspondence. `CountMismatch` points to the end of CUR.
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
