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
    def test_literal_cases_migrate_framing_keep_decrease(self):
        self.assertEqual(len(DATA['cases']), 4)
        self.assertEqual(DATA['ncn_acceptance'], 'Pending')
        midi = {'ppqn': DATA['ppqn'], 'last_tick': DATA['last_tick'], 'tempo': [(0, 500000)]}
        for row in DATA['cases']:
            with self.subTest(case=row['id']):
                lyr, cur = bytes.fromhex(row['lyr_hex']), bytes.fromhex(row['cur_hex'])
                original = (lyr, cur)
                self.assertEqual(checker.candidate_observation(midi, lyr, cur), row['expected'])
                self.assertEqual(checker.cursor_framing(cur), row['cur_framing'])
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
        self.assertEqual(a['schema'], 3)
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
        for schema in (True, 0, 4):
            bad = copy.deepcopy(inv); bad['schema'] = schema
            with self.assertRaises(ValueError): verifier.verify(json.dumps(bad).encode(), record)

class CurBoundaryTests(unittest.TestCase):
    H = b'T\nA\nC\n\n'
    MIDI = dict(ppqn=192, last_tick=600000, tempo=[(0, 500000)])

    def test_terminal_marker_is_optional_and_excludes_exactly_one_byte(self):
        for raw, kind, size, marker in ((b'', 'Physical', 0, None),
                (bytes.fromhex('1800'), 'Physical', 2, None),
                (bytes.fromhex('1800ff'), 'TerminalFF', 2, 2),
                (bytes.fromhex('ffffff'), 'TerminalFF', 2, 2)):
            expected = dict(status='Selected', kind=kind, marker_byte_offset=marker,
                            logical_cursor_bytes=size, framing_bytes=len(raw)-size)
            self.assertEqual(checker.cursor_framing(raw), expected)
            self.assertEqual(collector.cur_counts(raw, [])['framing'], expected)

    def test_complete_ff_words_remain_data(self):
        for hx, words in (('ff00', [255]), ('00ff', [65280]), ('ffff', [65535]),
                          ('1800ffffffff', [24, 65535, 65535])):
            result = checker.candidate_observation(self.MIDI, self.H+b'X'*len(words), bytes.fromhex(hx))
            self.assertEqual(result['positions'], words)
            self.assertEqual(result['ticks'], [v*8 for v in words])
        result = checker.candidate_observation(self.MIDI, self.H+b'XY', bytes.fromhex('ff001800'))
        self.assertEqual((result['code'], result['byte_offset']), ('Decreasing', 2))

    def test_non_ff_odd_tails_are_explicit_rejections(self):
        for hx, offset in (('18', 0), ('180000', 2), ('180018', 2)):
            raw = bytes.fromhex(hx)
            f = dict(status='Rejected', code='OddLength', byte_offset=offset)
            self.assertEqual(checker.cursor_framing(raw), f)
            observed = collector.cur_counts(raw, [])
            self.assertEqual(observed['framing'], f)
            self.assertEqual(observed['trailing_bytes'], 1)
            self.assertEqual(checker.candidate_observation(self.MIDI, self.H+b'X', raw)['code'], 'OddLength')

    def test_marker_cannot_repair_count_order_or_primary_bounds(self):
        for body, hx, midi, code, offset in (
                (b'X', '18003000ff', self.MIDI, 'CountMismatch', 4),
                (b'XY', '30001800ff', self.MIDI, 'Decreasing', 2),
                (b'X', '1800ff', dict(self.MIDI, last_tick=191), 'BeyondPrimary', 0)):
            result = checker.candidate_observation(midi, self.H+body, bytes.fromhex(hx))
            self.assertEqual((result['code'], result['byte_offset']), (code, offset))
            self.assertNotIn('positions', result)

    def test_empty_marker_has_zero_positions(self):
        self.assertEqual(checker.candidate_observation(self.MIDI, self.H, b'\xff')['status'], 'NoLyrics')
        self.assertEqual(checker.candidate_observation(self.MIDI, self.H+b'X', b'\xff')['code'], 'CountMismatch')
        with self.assertRaises(ValueError): collector.cur_counts(b'\xff', [0])

    def test_sub_and_cursor_framing_do_not_rescue_text(self):
        result = checker.candidate_observation(self.MIDI, self.H+b'X\x1a\xff', bytes.fromhex('1800ff'))
        self.assertEqual(result['scalars'], [88])
        result = checker.candidate_observation(self.MIDI, self.H+b'X\x00\x1a', bytes.fromhex('1800ff'))
        self.assertEqual((result['code'], result['byte_offset']), ('InvalidText', 8))

    def test_raw_caps_precede_marker_selection(self):
        # Five raw bytes cannot fit a four-byte cap despite a four-byte word span.
        with patch.object(checker, 'MEMBER_CAP', 4):
            with self.assertRaises(ValueError):
                checker.candidate_observation(self.MIDI, b'', bytes.fromhex('18003000ff'))
        manifest = json.loads((ROOT/'fixtures/ncn24/vectors.json').read_text())
        members = dict(MID=bytes.fromhex(manifest['midis']['M480']['hex']),
                       LYR=self.H+b'XY', CUR=bytes.fromhex('18003000ff'))
        with patch.dict(collector.MEMBER_CAPS, CUR=5):
            self.assertEqual(collector.inspect('sample-001', members, [])['cur']['words'], 2)
        with patch.dict(collector.MEMBER_CAPS, CUR=4):
            with self.assertRaises(ValueError): collector.inspect('sample-001', members, [])
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)/'synthetic.cur'; p.write_bytes(members['CUR'])
            self.assertEqual(collector.read_member(p, 5), members['CUR'])
            with self.assertRaises(ValueError): collector.read_member(p, 4)

    def test_marker_changes_raw_hash_and_shared_ledger(self):
        manifest = json.loads((ROOT/'fixtures/ncn24/vectors.json').read_text())
        members = dict(MID=bytes.fromhex(manifest['midis']['M480']['hex']),
                       LYR=self.H+b'X', CUR=bytes.fromhex('1800'))
        plain = collector.inspect('sample-001', members, [0])
        marked = collector.inspect('sample-001', dict(members, CUR=members['CUR']+b'\xff'), [0])
        self.assertEqual(plain['cur']['selected'], marked['cur']['selected'])
        self.assertNotEqual(plain['members']['CUR']['sha256'], marked['members']['CUR']['sha256'])
        self.assertEqual(marked['limits']['reserved_bytes']-plain['limits']['reserved_bytes'], 1)
        with patch.object(collector, 'STAGED_CAP', marked['limits']['reserved_bytes']-1):
            with self.assertRaises(ValueError):
                collector.inspect('sample-001', dict(members, CUR=members['CUR']+b'\xff'), [])

    def test_position_cap_and_equal_floor_order_survive_framing(self):
        with patch.object(checker, 'POSITION_CAP', 2):
            result = checker.candidate_observation(self.MIDI, self.H+b'XY', bytes.fromhex('18001800ff'))
            self.assertEqual(result['positions'], [24, 24])
            with self.assertRaises(ValueError):
                checker.candidate_observation(self.MIDI, self.H+b'XY', bytes.fromhex('180018001800ff'))
        result = checker.candidate_observation(dict(self.MIDI, ppqn=3), self.H+b'XYZ', bytes.fromhex('010002000300ff'))
        self.assertEqual((result['positions'], result['ticks']), ([1,2,3], [0,0,0]))

    def inventory(self, raw):
        manifest = json.loads((ROOT/'fixtures/ncn24/vectors.json').read_text())
        return collector.inspect('sample-001', dict(MID=bytes.fromhex(manifest['midis']['M480']['hex']),
            LYR=self.H+b'X', CUR=raw), [])

    def test_three_inventory_schemas_keep_distinct_interpretations(self):
        verifier = load('cur_history_verifier', 'verify-ncn-observation-record.py')
        record = (ROOT/'fixtures/ncn24/observation-template.json').read_bytes()
        new = self.inventory(bytes.fromhex('1800ff'))
        for schema in (1,2,3):
            inv = copy.deepcopy(new); inv['schema'] = schema
            if schema<3: del inv['cur']['framing']
            if schema==1: del inv['lyr']['logical_eof']
            result = verifier.verify(json.dumps(inv).encode(), record)
            self.assertEqual(result['inventory_text_interpretation'], 'StrictC0' if schema==1 else 'FirstSubEof')
            self.assertEqual(result['inventory_cursor_interpretation'], 'OptionalTerminalFF' if schema==3 else 'Unframed')
            self.assertEqual(result['ncn_acceptance'], 'Pending')

    def test_verifier_rejects_framing_arithmetic_and_boolean_tampering(self):
        verifier = load('cur_arithmetic_verifier', 'verify-ncn-observation-record.py')
        record = (ROOT/'fixtures/ncn24/observation-template.json').read_bytes()
        inv = self.inventory(bytes.fromhex('1800ff'))
        for key, value in (('kind','Physical'), ('marker_byte_offset',True),
                           ('logical_cursor_bytes',3), ('framing_bytes',0), ('framing_bytes',True)):
            bad = copy.deepcopy(inv); bad['cur']['framing'][key] = value
            with self.assertRaises(ValueError): verifier.verify(json.dumps(bad).encode(), record)
        for key, value in (('words',2), ('trailing_bytes',0)):
            bad = copy.deepcopy(inv); bad['cur'][key] = value
            with self.assertRaises(ValueError): verifier.verify(json.dumps(bad).encode(), record)

    def test_rejected_framing_is_inventory_not_a_successful_span(self):
        verifier = load('cur_rejection_verifier', 'verify-ncn-observation-record.py')
        record = (ROOT/'fixtures/ncn24/observation-template.json').read_bytes()
        inv = self.inventory(bytes.fromhex('180000'))
        self.assertEqual(verifier.verify(json.dumps(inv).encode(), record)['ncn_acceptance'], 'Pending')
        for key, value in (('status','Selected'), ('code','Candidate'), ('byte_offset',0)):
            bad = copy.deepcopy(inv); bad['cur']['framing'][key] = value
            with self.assertRaises(ValueError): verifier.verify(json.dumps(bad).encode(), record)

    def test_framing_allocation_failure_does_not_publish_partial_receipts(self):
        import io
        from contextlib import redirect_stdout, redirect_stderr
        original = checker.cursor_framing
        calls = 0
        def fail_late(raw):
            nonlocal calls
            calls += 1
            if calls == 5: raise MemoryError
            return original(raw)
        out, err = io.StringIO(), io.StringIO()
        with patch.object(checker, 'cursor_framing', side_effect=fail_late), redirect_stdout(out), redirect_stderr(err):
            rc = checker.main([])
        self.assertEqual((rc, calls, out.getvalue()), (1, 5, ''))
        self.assertEqual(json.loads(err.getvalue())['error_family'], 'MemoryError')

    def test_every_non_ff_singleton_value_is_rejected(self):
        for value in range(255):
            raw = bytes.fromhex('1800') + bytes((value,))
            self.assertEqual(checker.cursor_framing(raw),
                             dict(status='Rejected', code='OddLength', byte_offset=2))
        self.assertEqual(checker.cursor_framing(bytes.fromhex('1800ff'))['kind'], 'TerminalFF')

if __name__ == '__main__':
    unittest.main()
