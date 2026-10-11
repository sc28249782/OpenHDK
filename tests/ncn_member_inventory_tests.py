#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic-only inventory checks. No private corpus or playback was run."""
import copy
import hashlib
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
spec = importlib.util.spec_from_file_location('ncn_inventory', Path(__file__).with_name('collect-ncn-member-inventory.py'))
collector = importlib.util.module_from_spec(spec); spec.loader.exec_module(collector)
verifier_spec = importlib.util.spec_from_file_location('ncn_record_verifier', Path(__file__).with_name('verify-ncn-observation-record.py'))
verifier = importlib.util.module_from_spec(verifier_spec); verifier_spec.loader.exec_module(verifier)
DATA = json.loads((Path(__file__).parent / 'fixtures/ncn24/vectors.json').read_text())


def bundle(name):
    row = next(c for c in DATA['cases'] if c['id'] == name)
    return {'MID': bytes.fromhex(DATA['midis'][row['midi']]['hex']),
            'LYR': bytes.fromhex(row['lyr_hex']), 'CUR': bytes.fromhex(row['cur_hex'])}


class InventoryTests(unittest.TestCase):
    def test_original_33_cases_are_inventory_not_acceptance(self):
        for row in DATA['cases']:
            members = bundle(row['id'])
            result = collector.inspect('sample-001', members, [])
            self.assertEqual(result['ncn_acceptance'], 'Pending')
            self.assertEqual(result['legacy_observations'], 'Pending')
            self.assertFalse(result['count_equality_is_compatibility'])
            self.assertEqual(result['members']['CUR']['sha256'], hashlib.sha256(members['CUR']).hexdigest())
            self.assertLessEqual(result['limits']['reserved_bytes'], result['limits']['staged_bytes'])

    def test_thai_count_without_text(self):
        r = collector.inspect('sample-001', bundle('thai-combining'), [0, 1, 2])
        self.assertEqual((r['lyr']['normalized_scalars'], r['lyr']['thai_mark_scalars']), (3, 2))
        self.assertEqual([p['unsigned_value'] for p in r['cur']['selected']], [24, 48, 72])
        self.assertEqual(r['primary']['ppqn'], 480)
        self.assertEqual(r['primary']['tempo_map'], 'Pending')
        self.assertNotIn('กิ่', json.dumps(r, ensure_ascii=False))

    def test_spelling_not_normalized(self):
        counts = [collector.inspect('sample-001', bundle(name), [])['lyr']['normalized_scalars']
                  for name in ('thai-am', 'thai-am-decomposed')]
        self.assertEqual(counts, [2, 3])

    def test_newline_offsets_and_empty_lines(self):
        for name, crlf, cr, lf in [('lf', 0, 0, 1), ('crlf', 1, 0, 0), ('cr', 0, 1, 0)]:
            r = collector.inspect('sample-001', bundle(name), [])['lyr']
            self.assertEqual((r['normalized_scalars'], r['crlf'], r['cr'], r['lf']), (3, crlf, cr, lf))
        r = collector.inspect('sample-001', bundle('blank-line'), [])['lyr']
        self.assertEqual((r['normalized_lf'], r['empty_line_breaks']), (2, 1))
        self.assertTrue(collector.inspect('sample-001', bundle('terminal-lf'), [])['lyr']['terminal_lf'])

    def test_odd_decreasing_equal_are_reported(self):
        for name, key, expected in [('odd', 'trailing_bytes', 1), ('decreasing', 'decreasing_pairs', 1), ('equal-positions', 'equal_pairs', 1)]:
            r = collector.inspect('sample-001', bundle(name), [])['cur']
            self.assertEqual(r[key], expected)
        self.assertEqual(collector.inspect('sample-001', bundle('decreasing'), [])['cur']['decreasing_indices'], [1])

    def test_high_byte_ff_retained_no_sentinel_decision(self):
        r = collector.inspect('sample-001', bundle('high-ff-middle'), [0, 1, 2])['cur']
        self.assertEqual((r['high_ff_words'], r['low_ff_words'], r['historical_high_ff_skip_count']), (2, 2, 1))
        self.assertEqual([p['unsigned_value'] for p in r['selected']], [65279, 65280, 65535])
        self.assertEqual(r['high_ff_policy'], 'Unresolved')

    def test_anomaly_samples_bounded_and_explicit(self):
        r = collector.cur_counts(b'\xff\xff' * 20, [])
        self.assertEqual(len(r['high_ff_indices']), 16)
        self.assertTrue(r['high_ff_indices_truncated'])
        decreasing = b''.join(v.to_bytes(2, 'little') for v in range(30, 0, -1))
        r = collector.cur_counts(decreasing, [])
        self.assertEqual(len(r['decreasing_indices']), 16)
        self.assertTrue(r['decreasing_indices_truncated'])

    def test_header_and_text_failures_are_hypothesis_diagnostics(self):
        for name, status, offset in [('missing-header', 'MissingHeaderTerminator', 6), ('nul', 'InvalidTextHypothesis', 7), ('undefined-tis', 'InvalidTextHypothesis', 7)]:
            r = collector.inspect('sample-001', bundle(name), [])
            self.assertEqual((r['lyr']['status'], r['lyr']['byte_offset']), (status, offset))
            self.assertEqual(r['scalar_count_equals_cursor_words'], 'Unavailable')

    def test_smf_container_does_not_claim_event_validity(self):
        data = bytes.fromhex('4d546864000000060000000101e04d54726b0000000100')
        r = collector.smf_container(data)
        self.assertEqual(r['status'], 'ContainerObserved')
        self.assertEqual(r['track_events'], 'Pending')
        for bad in (data[:-1], data + b'x', b'bad'):
            self.assertEqual(collector.smf_container(bad)['status'], 'RejectedContainer')

    def test_indices_ids_and_limits(self):
        for indices in ([3], [-1], [True], [0, 0], list(range(65))):
            with self.assertRaises(ValueError): collector.inspect('sample-001', bundle('quarter'), indices)
        for name in ('../private', 'private-user', 'sample-0001'):
            with self.assertRaises(ValueError): collector.inspect(name, bundle('quarter'), [])
        with patch.object(collector, 'STAGED_CAP', 1):
            with self.assertRaises(ValueError): collector.inspect('sample-001', bundle('quarter'), [])
        with patch.object(collector, 'AGGREGATE_CAP', 1):
            with self.assertRaises(ValueError): collector.inspect('sample-001', bundle('quarter'), [])

    def test_regular_read_cap_symlink_fifo_and_no_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); p = root / 'member'; p.write_bytes(b'abc')
            self.assertEqual(collector.read_member(p, 3), b'abc')
            with self.assertRaises(ValueError): collector.read_member(p, 2)
            if hasattr(os, 'mkfifo'):
                fifo = root / 'fifo'; os.mkfifo(fifo)
                with self.assertRaises(ValueError): collector.read_member(fifo, 3)
                link = root / 'link'; link.symlink_to(p)
                with self.assertRaises(ValueError): collector.read_member(link, 3)
            self.assertEqual(p.read_bytes(), b'abc')

    def test_size_change_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / 'member'; p.write_bytes(b'abc')
            before = os.stat(p)
            changed = type('Changed', (), dict(st_dev=before.st_dev, st_ino=before.st_ino,
                       st_size=before.st_size + 1, st_mtime_ns=before.st_mtime_ns))()
            with patch.object(collector.os, 'fstat', side_effect=[before, changed]):
                with self.assertRaises(ValueError): collector.read_member(p, 3)

    def test_cli_private_path_not_emitted_and_sources_unchanged(self):
        with tempfile.TemporaryDirectory(prefix='private-username-') as directory:
            members = bundle('thai-combining'); args = []
            for name, data in members.items():
                path = Path(directory) / name; path.write_bytes(data)
                args += ['--' + name.lower(), str(path)]
            proc = subprocess.run([sys.executable, collector.__file__, '--sample-id', 'sample-001',
                '--permission-basis', 'original-synthetic', '--indices', '0,2'] + args,
                capture_output=True, check=False, timeout=5)
            self.assertEqual(proc.returncode, 0)
            self.assertEqual(json.loads(proc.stdout)['ncn_acceptance'], 'Pending')
            self.assertNotIn(b'private-username', proc.stdout + proc.stderr)
            self.assertNotIn('กิ่'.encode(), proc.stdout)
            self.assertLessEqual(len(proc.stdout), collector.OUTPUT_CAP)
            for name, data in members.items(): self.assertEqual((Path(directory) / name).read_bytes(), data)
            proc = subprocess.run([sys.executable, collector.__file__, '--sample-id', 'sample-001',
                '--permission-basis', 'original-synthetic', '--indices', '999'] + args,
                capture_output=True, check=False, timeout=5)
            self.assertEqual(proc.returncode, 1)
            self.assertEqual(proc.stdout, b'')
            self.assertNotIn(b'private-username', proc.stderr)


class ObservationRecordTests(unittest.TestCase):
    def setUp(self):
        self.inventory = collector.inspect('sample-001', bundle('thai-combining'), [0, 1, 2])
        self.inv_bytes = json.dumps(self.inventory).encode()
        self.record = json.loads((Path(__file__).parent / 'fixtures/ncn24/observation-template.json').read_text())

    def verify(self):
        return verifier.verify(self.inv_bytes, json.dumps(self.record).encode())

    def observed(self):
        # Authored mock receipt for tests, NOT a playback observation.
        self.record['inventory_sha256'] = hashlib.sha256(self.inv_bytes).hexdigest()
        self.record['reader'] = dict(name='SyntheticTestReader', version='1.0', build_ref='Unspecified')
        self.record['run'] = dict(status='Completed', platform='SyntheticTest', observation_method='visual')
        self.record['observations'] = [dict(id='obs-001', topic='thai-mark', cursor_index=1,
            measured_midi_tick=None, measured_media_us=None, action='Text', method='visual',
            artifact_sha256=hashlib.sha256(b'original synthetic mock observation').hexdigest())]
        self.record['coverage']['thai-mark'] = dict(status='Observed', reason='recorded')

    def test_pending_template_keeps_six_topics_pending(self):
        r = self.verify()
        self.assertEqual((r['observations'], r['pending_topics'], r['ncn_acceptance']), (0, 6, 'Pending'))

    def test_synthetic_mock_binding_never_accepts(self):
        self.observed(); r = self.verify()
        self.assertEqual((r['observations'], r['observed_topics'], r['pending_topics']), (1, 1, 5))
        self.assertEqual(r['playback_authentication'], 'NotIndependent')
        self.assertEqual(r['ncn_acceptance'], 'Pending')
        self.assertEqual(r['tempo_map_validation'], 'Pending')

    def test_inventory_binding_and_sample_id_tampering(self):
        self.observed()
        for field, value in [('inventory_sha256', '0' * 64), ('sample_id', 'sample-002')]:
            old = self.record[field]; self.record[field] = value
            with self.assertRaises(ValueError): self.verify()
            self.record[field] = old

    def test_visual_tick_and_boolean_rejected(self):
        self.observed()
        for field, value in [('measured_midi_tick', 480), ('measured_media_us', 500000), ('cursor_index', True)]:
            old = self.record['observations'][0][field]; self.record['observations'][0][field] = value
            with self.assertRaises(ValueError): self.verify()
            self.record['observations'][0][field] = old

    def test_missing_selected_position_or_duplicate_observation(self):
        self.observed(); self.record['observations'][0]['cursor_index'] = 99
        with self.assertRaises(ValueError): self.verify()
        self.record['observations'][0]['cursor_index'] = 1
        self.record['observations'].append(copy.deepcopy(self.record['observations'][0]))
        with self.assertRaises(ValueError): self.verify()

    def test_false_coverage_and_acceptance_rejected(self):
        self.record['coverage']['high-ff'] = dict(status='Observed', reason='recorded')
        with self.assertRaises(ValueError): self.verify()
        self.record['coverage']['high-ff'] = dict(status='Pending', reason='not-collected')
        self.record['ncn_acceptance'] = 'Accepted'
        with self.assertRaises(ValueError): self.verify()

    def test_skipped_is_not_observed(self):
        self.record['coverage']['high-ff'] = dict(status='Skipped', reason='feature-absent')
        r = self.verify(); self.assertEqual((r['skipped_topics'], r['observed_topics']), (1, 0))
        self.record['coverage']['high-ff']['reason'] = 'recorded'
        with self.assertRaises(ValueError): self.verify()

    def test_duplicates_bounds_and_private_field_rejected(self):
        data = json.dumps(self.record).encode()
        with self.assertRaises(ValueError): verifier.verify(self.inv_bytes, data.replace(b'"schema": 1', b'"schema": 1, "schema": 1'))
        with self.assertRaises(ValueError): verifier.verify(self.inv_bytes, b'x' * (verifier.CAP + 1))
        with patch.object(verifier, 'STAGED_CAP', 1):
            with self.assertRaises(ValueError): self.verify()
        self.record['absolute_path'] = '/private-user/song'
        with self.assertRaises(ValueError): self.verify()

    def test_reader_measurements_need_method_and_reader_metadata(self):
        self.observed(); row = self.record['observations'][0]
        row.update(method='reader-report', measured_midi_tick=480)
        with self.assertRaises(ValueError): self.verify()
        self.record['run']['observation_method'] = 'reader-report'
        self.assertEqual(self.verify()['ncn_acceptance'], 'Pending')
        self.record['reader']['version'] = 'Pending'
        with self.assertRaises(ValueError): self.verify()

    def test_cli_usage_never_echoes_private_args(self):
        for file in (collector.__file__, verifier.__file__):
            p = subprocess.run([sys.executable, file, '--private-unknown', '/private-user/secret'], capture_output=True, timeout=5)
            self.assertEqual(p.returncode, 64)
            self.assertNotIn(b'private-user', p.stdout + p.stderr)


if __name__ == '__main__':
    unittest.main()
