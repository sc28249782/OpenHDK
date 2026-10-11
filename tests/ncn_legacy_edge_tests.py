#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original synthetic discrepancies; no Qt or representative playback execution."""
import importlib.util
import json
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
    def test_literal_cases_keep_strict_rejections(self):
        self.assertEqual(len(DATA['cases']), 4)
        self.assertEqual(DATA['ncn_acceptance'], 'Pending')
        midi = {'ppqn': DATA['ppqn'], 'last_tick': DATA['last_tick']}
        for row in DATA['cases']:
            with self.subTest(case=row['id']):
                lyr, cur = bytes.fromhex(row['lyr_hex']), bytes.fromhex(row['cur_hex'])
                original = (lyr, cur)
                self.assertEqual(checker.candidate_observation(midi, lyr, cur), row['expected'])
                self.assertEqual((lyr, cur), original)

    def test_sub_is_rejected_without_terminator_guess(self):
        for suffix in (b'\x1a', b'\x1a\r\n', b'\x1aX'):
            self.assertEqual(collector.lyr_counts(b'T\nA\nC\n\nX' + suffix),
                             {'status':'InvalidTextHypothesis','byte_offset':8})

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
        self.assertEqual(collector.lyr_counts(bytes.fromhex(row['lyr_hex']))['status'], 'InvalidTextHypothesis')
        cur = collector.cur_counts(bytes.fromhex(row['cur_hex']), [])
        self.assertEqual((cur['decreasing_pairs'], cur['trailing_bytes']), (1, 1))

if __name__ == '__main__':
    unittest.main()
