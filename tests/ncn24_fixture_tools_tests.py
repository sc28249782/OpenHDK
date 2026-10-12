#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent expected-value mutations and bounded synthetic-harness checks."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('ncn_fixture_checker', Path(__file__).with_name('check-ncn24-fixtures.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)
MANIFEST = Path(__file__).parent / 'fixtures/ncn24/vectors.json'


class NcnFixtureTools(unittest.TestCase):
    def setUp(self):
        self.data = MANIFEST.read_bytes()
        self.manifest = checker.parse_manifest(self.data)

    def row(self, name):
        return next(r for r in self.manifest['cases'] if r['id'] == name)

    def rejected(self):
        with self.assertRaises(ValueError):
            checker.check(self.manifest, len(self.data))

    def test_full_original_vectors(self):
        r = checker.check(self.manifest, len(self.data))
        self.assertEqual((r['cases'], r['primary_vectors'], r['decoded_bytes']), (69, 4, 932))
        self.assertEqual(r['status'], 'fixture-verified')
        self.assertEqual(r['ncn_acceptance'], 'Pending')
        self.assertEqual(r['representative_observations'], 'Pending')
        self.assertLessEqual(r['limits']['reserved_bytes'], r['limits']['staged_bytes'])

    def test_independent_tempo_map_expectation(self):
        midi = checker.midi_fixture(bytes.fromhex(self.manifest['midis']['Mtempo']['hex']))
        self.assertEqual(midi['tempo'], [(0, 500000), (0, 500000), (480, 1000000)])
        self.assertEqual([checker.media_time(midi, t) for t in (480, 960, 1440)], [500000, 1500000, 2500000])
        self.row('tempo-change')['expected']['media_us'][1] = 1000000
        self.rejected()  # A first-tempo-only implementation must not validate.

    def test_old_manifest_schema_is_not_silently_reinterpreted(self):
        for schema in (1, 2):
            self.manifest['schema'] = schema
            self.rejected()
            with self.assertRaises(ValueError):
                checker.parse_manifest(json.dumps(self.manifest).encode())

    def test_cursor_endian_tamper(self):
        self.row('quarter')['cur_hex'] = '0018'
        self.rejected()

    def test_wiki_timing_tamper(self):
        self.manifest['timing'][2]['wiki_units'] = [24, 1]
        self.rejected()

    def test_expected_tick_tamper(self):
        self.row('floor-collision')['expected']['ticks'] = [0, 0, 0]
        self.rejected()

    def test_high_byte_skip_tamper(self):
        self.row('high-ff-max')['expected'] = dict(status='NoLyrics', scalars=[], member_offsets=[], positions=[], ticks=[], media_us=[])
        self.rejected()

    def test_scalar_and_offset_tamper(self):
        for name, field, values in [('thai-combining', 'scalars', [3585]), ('crlf', 'member_offsets', [7, 8, 9])]:
            original = copy.deepcopy(self.row(name)['expected'][field])
            self.row(name)['expected'][field] = values
            self.rejected()
            self.row(name)['expected'][field] = original

    def test_error_offset_tamper(self):
        self.row('decreasing')['expected']['byte_offset'] = 0
        self.rejected()

    def test_missing_duplicate_unknown_cases(self):
        for transform in (lambda rows: rows.pop(), lambda rows: rows.__setitem__(0, copy.deepcopy(rows[1])),
                          lambda rows: rows[0].__setitem__('id', '../private')):
            saved = copy.deepcopy(self.manifest['cases'])
            transform(self.manifest['cases']); self.rejected()
            self.manifest['cases'] = saved

    def test_false_scope_and_acceptance(self):
        for field, value in [('scope', 'representative'), ('ncn_acceptance', 'Accepted')]:
            saved = self.manifest[field]
            self.manifest[field] = value; self.rejected()
            self.manifest[field] = saved

    def test_duplicate_json_and_boolean_numeric(self):
        with self.assertRaises(ValueError):
            checker.parse_manifest(self.data.replace(b'"schema": 3', b'"schema": 3, "schema": 3'))
        self.row('quarter')['expected']['ticks'][0] = True
        self.rejected()

    def test_hex_and_manifest_bounds(self):
        for value in ('0', 'ff zz', 'FF', '00' * (checker.MEMBER_CAP + 1)):
            with self.assertRaises(ValueError): checker.unhex(value)
        with self.assertRaises(ValueError): checker.parse_manifest(b' ' * (checker.MANIFEST_CAP + 1))
        with self.assertRaises(ValueError): checker.validate_json({'x': -1})
        with self.assertRaises(ValueError): checker.validate_json({'x': 2**64})

    def test_decoded_and_shared_staging_bounds(self):
        with patch.object(checker, 'DECODED_CAP', 931): self.rejected()
        with patch.object(checker, 'STAGED_CAP', 100): self.rejected()
        with patch.object(checker, 'POSITION_CAP', 2): self.rejected()

    def test_smf_restriction_and_lengths(self):
        data = bytearray.fromhex(self.manifest['midis']['M480']['hex'])
        for bad in (bytes(data[:-1]), bytes(data) + b'x', bytes(data[:12]) + b'\x80\x00' + bytes(data[14:])):
            with self.assertRaises(ValueError): checker.midi_fixture(bad)
        data[-2] = 1  # Unsupported fixture meta; never accept arbitrary MIDI events.
        with self.assertRaises(ValueError): checker.midi_fixture(bytes(data))

    def test_vlq_and_missing_eot(self):
        prefix = bytes.fromhex('4d546864000000060000000101e04d54726b')
        for track in (b'\x81\x81\x81\x81', b'\x00\xff\x51\x03\x07\xa1\x20'):
            with self.assertRaises(ValueError):
                checker.midi_fixture(prefix + len(track).to_bytes(4, 'big') + track)

    def test_export_exact_bytes_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'synthetic'
            checker.check(self.manifest, len(self.data))
            checker.export_fixtures(self.manifest, out)
            self.assertEqual(len(list(out.rglob('example.*'))), 207)
            self.assertEqual((out / 'thai-combining/Lyrics/example.lyr').read_bytes(), b'T\nA\nC\n\n\xa1\xd4\xe8')
            self.assertEqual((out / 'high-ff-max/Cursor/example.cur').read_bytes(), b'\xff\xff')
            with self.assertRaises(FileExistsError): checker.export_fixtures(self.manifest, out)
            self.assertEqual(len(list(out.rglob('example.*'))), 207)

    def test_cli_failure_has_no_partial_or_private_output(self):
        with tempfile.TemporaryDirectory() as directory:
            private = Path(directory) / 'private-username'
            self.row('high-ff-middle')['expected']['ticks'][-1] = 0
            private.write_text(json.dumps(self.manifest))
            proc = subprocess.run([sys.executable, str(Path(checker.__file__)), '--manifest', str(private)],
                                  capture_output=True, timeout=5, check=False)
            self.assertEqual(proc.returncode, 1)
            self.assertEqual(proc.stdout, b'')
            self.assertEqual(json.loads(proc.stderr)['status'], 'fixture-check-failed')
            self.assertNotIn(b'private-username', proc.stderr)

    def test_manifest_regular_file_and_cap(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'manifest'
            path.write_bytes(b'x' * (checker.MANIFEST_CAP + 1))
            with self.assertRaises(ValueError): checker.read_bounded(path)
            if hasattr(os, 'mkfifo'):
                fifo = Path(directory) / 'fifo'
                os.mkfifo(fifo)
                with self.assertRaises(ValueError): checker.read_bounded(fifo)

    def test_body_scalar_bound(self):
        with self.assertRaises(ValueError):
            checker.body_observation(b'T\nA\nC\n\n' + b'A' * (checker.POSITION_CAP + 1))

    def test_cli_positive_receipts_and_caps(self):
        proc = subprocess.run([sys.executable, str(Path(checker.__file__))], capture_output=True, timeout=5, check=False)
        self.assertEqual(proc.returncode, 0)
        result = json.loads(proc.stdout)
        self.assertEqual(result['cases'], 69)
        self.assertEqual(result['ncn_acceptance'], 'Pending')
        self.assertLessEqual(len(proc.stdout), checker.OUTPUT_CAP)
        self.assertNotIn(str(MANIFEST).encode(), proc.stdout)
        self.assertNotIn('กิ่'.encode(), proc.stdout)


if __name__ == '__main__':
    unittest.main()
