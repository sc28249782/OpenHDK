#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original synthetic discrepancies; no Qt or representative playback execution."""
import importlib.util
import json
import copy
import hashlib
import tempfile
from unittest.mock import patch
from pathlib import Path
import unittest
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).parent

def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

checker = load('edge_checker', 'check-ncn24-fixtures.py')
collector = load('edge_inventory', 'collect-ncn-member-inventory.py')
DATA = json.loads((ROOT / 'fixtures/ncn24/legacy-edge-vectors.json').read_text())

class EdgeTests(unittest.TestCase):
    def test_literal_cases_migrate_sub_keep_cur_rejections(self):
        self.assertEqual(len(DATA['cases']), 4)
        self.assertEqual(DATA['ncn_acceptance'], 'Pending')
        midi = {'ppqn': DATA['ppqn'], 'last_tick': DATA['last_tick'], 'tempo': [(0, 500000)]}
        for row in DATA['cases']:
            with self.subTest(case=row['id']):
                lyr, cur = bytes.fromhex(row['lyr_hex']), bytes.fromhex(row['cur_hex'])
                original = (lyr, cur)
                self.assertEqual(checker.candidate_observation(midi, lyr, cur), row['expected'])
                self.assertEqual((lyr, cur), original)

    def test_sub_is_logical_eof_with_arbitrary_following_bytes(self):
        for suffix in (b'\x1a', b'\x1a\r\n', b'\x1aX', b'\x1a\x00\xff'):
            result = collector.lyr_counts(b'T\nA\nC\n\nX' + suffix)
            self.assertEqual(result['normalized_scalars'], 1)
            self.assertEqual(result['logical_eof']['marker_byte_offset'], 8)
            self.assertEqual(result['logical_eof']['ignored_bytes'], len(suffix) - 1)

    def test_odd_ff_is_not_a_high_ff_word(self):
        result = collector.cur_counts(bytes.fromhex('1800ff'), [0])
        self.assertEqual((result['words'], result['trailing_bytes'], result['high_ff_words']), (1, 1, 0))
        self.assertEqual(result['selected'][0]['unsigned_value'], 24)
        self.assertEqual(result['high_ff_policy'], 'Unresolved')

    def test_complete_ff_word_is_distinct(self):
        result = collector.cur_counts(bytes.fromhex('1800ffff'), [1])
        self.assertEqual((result['words'], result['trailing_bytes'], result['high_ff_words']), (2, 0, 1))
        self.assertEqual(result['selected'][0]['unsigned_value'], 65535)

    def test_decrease_survives_tick_conversion(self):
        result = collector.cur_counts(bytes.fromhex('30002f003100'), [0, 1, 2])
        self.assertEqual(result['decreasing_indices'], [1])
        values = [r['unsigned_value'] for r in result['selected']]
        self.assertEqual(values, [48, 47, 49])
        self.assertEqual([u * 192 // 24 for u in values], [384, 376, 392])

    def test_combined_inventory_keeps_both_anomalies(self):
        row = DATA['cases'][-1]
        self.assertEqual(collector.lyr_counts(bytes.fromhex(row['lyr_hex']))['normalized_scalars'], 3)
        cur = collector.cur_counts(bytes.fromhex(row['cur_hex']), [])
        self.assertEqual((cur['decreasing_pairs'], cur['trailing_bytes']), (1, 1))

class SubBoundaryTests(unittest.TestCase):
    H = b'T\nA\nC\n\n'

    def test_header_eof_cannot_use_ignored_newlines(self):
        for prefix in (b'T', b'T\nA', b'T\nA\nC', b'T\nA\nC\nR'):
            data = prefix + b'\x1a\n\n\n\n'
            result = collector.lyr_counts(data)
            self.assertEqual((result['status'], result['byte_offset']),
                             ('MissingHeaderTerminator', len(prefix)))
            with self.assertRaises(checker.FixtureError) as error:
                checker.body_observation(data)
            self.assertEqual(error.exception.result['code'], 'HeaderTermination')

    def test_prefix_errors_retain_original_offsets(self):
        for bad in (b'\x00', b'\xff'):
            data = self.H + b'X' + bad + b'\x1a'
            self.assertEqual(collector.lyr_counts(data)['byte_offset'], 8)
            with self.assertRaises(checker.FixtureError) as error:
                checker.body_observation(data)
            self.assertEqual(error.exception.result['byte_offset'], 8)

    def test_shared_decoder_still_rejects_sub(self):
        with self.assertRaises(checker.FixtureError): checker.text_scalar(26, 4)
        with self.assertRaises(ValueError): collector.scalar(26)

    def test_newline_before_eof_is_body_after_eof_is_not(self):
        for before in (b'\r\n', b'\r', b'\n'):
            data = self.H + b'X' + before + b'\x1a\r\n'
            scalars, offsets = checker.body_observation(data)
            self.assertEqual((scalars, offsets), ([88, 10], [7, 8]))
            counts = collector.lyr_counts(data)
            self.assertEqual((counts['normalized_scalars'], counts['normalized_lf']), (2, 1))
            self.assertTrue(counts['terminal_lf'])

    def test_ignored_bytes_change_raw_hash_and_ledger(self):
        base = json.loads((ROOT / 'fixtures/ncn24/vectors.json').read_text())
        mid = bytes.fromhex(base['midis']['M480']['hex'])
        one = {'MID':mid, 'LYR': self.H + b'X\x1a', 'CUR':b'\x18\x00'}
        two = dict(one, LYR=one['LYR'] + b'\x00\xffXYZ')
        a, b = (collector.inspect('sample-001', members, [0]) for members in (one, two))
        self.assertEqual(a['lyr']['normalized_scalars'], b['lyr']['normalized_scalars'])
        self.assertNotEqual(a['members']['LYR']['sha256'], b['members']['LYR']['sha256'])
        self.assertEqual(b['limits']['reserved_bytes'] - a['limits']['reserved_bytes'], 5)
        self.assertEqual(b['lyr']['logical_eof']['ignored_bytes'], 5)
        self.assertEqual(a['schema'], 2)
        with patch.object(collector, 'STAGED_CAP', b['limits']['reserved_bytes'] - 1):
            with self.assertRaises(ValueError): collector.inspect('sample-001', two, [0])

    def test_raw_cap_applies_before_early_eof(self):
        cap = collector.MEMBER_CAPS['LYR']
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'original-synthetic.lyr'
            data = self.H + b'\x1a' + b'X' * (cap - len(self.H) - 1)
            path.write_bytes(data)
            self.assertEqual(collector.read_member(path, cap), data)
            path.write_bytes(data + b'X')
            with self.assertRaises(ValueError): collector.read_member(path, cap)
        with self.assertRaises(ValueError): checker.unhex('1a' + '58' * checker.MEMBER_CAP)

    def test_logical_count_not_repaired(self):
        midi = {'ppqn':480, 'last_tick':1440, 'tempo':[(0,500000)]}
        for lyr, cur in ((self.H+b'X\x1aY', b'\x18\x00\x30\x00'),
                         (self.H+b'\x1aX', b'\x18\x00')):
            self.assertEqual(checker.candidate_observation(midi, lyr, cur)['code'], 'CountMismatch')

    def test_thai_scalars_before_eof(self):
        data = self.H + bytes.fromhex('a1d4e8') + b'\x1a\xff'
        self.assertEqual(checker.body_observation(data), ([3585,3636,3656], [7,8,9]))
        counts = collector.lyr_counts(data)
        self.assertEqual((counts['normalized_scalars'], counts['thai_mark_scalars']), (3,2))

    def test_physical_eof_and_empty_marker(self):
        for data, kind, size in ((self.H, 'Physical', 7), (self.H+b'\x1a', 'Sub', 7)):
            eof = collector.lyr_counts(data)['logical_eof']
            self.assertEqual((eof['kind'], eof['logical_text_bytes'], eof['ignored_bytes']), (kind,size,0))
            self.assertEqual(eof['marker_byte_offset'], None if kind == 'Physical' else 7)

    def test_allocation_failure_no_success_output(self):
        # CLI catches MemoryError; simulate before inspect without exposing member text.
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for ext in ('mid','lyr','cur'):
                path = Path(directory) / ('example.' + ext); path.write_bytes(b'X'); paths.append(path)
            from contextlib import redirect_stderr
            import io
            output = io.StringIO()
            with patch.object(collector, 'inspect', side_effect=MemoryError), redirect_stderr(output):
                rc = collector.main(['--mid',str(paths[0]),'--lyr',str(paths[1]),'--cur',str(paths[2]),
                                     '--sample-id','sample-001','--permission-basis','original-synthetic'])
            self.assertEqual(rc, 1)
            self.assertEqual(json.loads(output.getvalue())['error_family'], 'MemoryError')
            self.assertNotIn(str(directory), output.getvalue())

    def test_verifier_keeps_schema_one_separate(self):
        verifier = load('sub_record_verifier', 'verify-ncn-observation-record.py')
        manifest = json.loads((ROOT / 'fixtures/ncn24/vectors.json').read_text())
        members = {'MID':bytes.fromhex(manifest['midis']['M480']['hex']),
                   'LYR': self.H + b'X', 'CUR':b'\x18\x00'}
        inv = collector.inspect('sample-001', members, [0])
        record = (ROOT / 'fixtures/ncn24/observation-template.json').read_bytes()
        old = copy.deepcopy(inv); old['schema'] = 1; del old['lyr']['logical_eof']
        for value, interpretation in ((inv,'FirstSubEof'), (old,'StrictC0')):
            result = verifier.verify(json.dumps(value).encode(), record)
            self.assertEqual(result['inventory_text_interpretation'], interpretation)
        self.assertNotIn('logical_eof', old['lyr'])

    def test_verifier_rejects_inconsistent_eof_fields(self):
        verifier = load('sub_field_verifier', 'verify-ncn-observation-record.py')
        manifest = json.loads((ROOT / 'fixtures/ncn24/vectors.json').read_text())
        inv = collector.inspect('sample-001', {'MID':bytes.fromhex(manifest['midis']['M480']['hex']),
            'LYR':self.H+b'X\x1aABC','CUR':b'\x18\x00'}, [0])
        record = (ROOT / 'fixtures/ncn24/observation-template.json').read_bytes()
        for key, value in (('kind','Physical'), ('marker_byte_offset',7),
                           ('logical_text_bytes',True), ('ignored_bytes',2)):
            bad = copy.deepcopy(inv); bad['lyr']['logical_eof'][key] = value
            with self.assertRaises(ValueError): verifier.verify(json.dumps(bad).encode(), record)
        for schema in (True, 0, 3):
            bad = copy.deepcopy(inv); bad['schema'] = schema
            with self.assertRaises(ValueError): verifier.verify(json.dumps(bad).encode(), record)

if __name__ == '__main__':
    unittest.main()
