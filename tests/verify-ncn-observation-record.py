#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check bounded supplied record structure/binding, never authenticate playback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys

CAP = 64 * 1024
STAGED_CAP = 512 * 1024
OUTPUT_CAP = 4096
TOPICS = frozenset(('timing-non60', 'tempo-change', 'newline', 'thai-mark', 'high-ff', 'line-entry'))


def need(ok):
    if not ok: raise ValueError('observation record boundary')


def shape(value, keys):
    need(type(value) is dict and set(value) == set(keys))


def unique(pairs):
    result = {}
    for key, value in pairs:
        need(key not in result); result[key] = value
    return result


def bounded(path):
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_BINARY', 0))
    try:
        info = os.fstat(fd); need(stat.S_ISREG(info.st_mode) and info.st_size <= CAP)
        with os.fdopen(fd, 'rb', closefd=False) as stream: data = stream.read(CAP + 1)
        need(len(data) <= CAP)
        return data
    finally: os.close(fd)


def number(value):
    need(type(value) is int and 0 <= value < 2**64)


def hash_value(value):
    need(type(value) is str and re.fullmatch('[0-9a-f]{64}', value))


def label(value):
    need(type(value) is str and len(value) <= 64 and re.fullmatch('[A-Za-z0-9][A-Za-z0-9 ._-]*', value))


def payload(value, depth=0):
    need(depth <= 12)
    if type(value) is dict:
        need(len(value) <= 64)
        return sum(len(key.encode('utf-8')) + payload(item, depth + 1) for key, item in value.items())
    if type(value) is list:
        need(len(value) <= 1024)
        return sum(payload(item, depth + 1) for item in value)
    if type(value) is str: return len(value.encode('utf-8'))
    if value is None or type(value) is bool: return 1
    number(value); return 8


def verify(inventory_bytes, record_bytes):
    need(len(inventory_bytes) <= CAP and len(record_bytes) <= CAP)
    inv = json.loads(inventory_bytes.decode('utf-8'), object_pairs_hook=unique)
    record = json.loads(record_bytes.decode('utf-8'), object_pairs_hook=unique)
    reserved = (len(inventory_bytes) + len(record_bytes) + payload(inv) + payload(record)
                + 2 * CAP + 2 * OUTPUT_CAP)
    need(reserved <= STAGED_CAP)
    # Validate the inventory fields consumed here. This does not re-read members
    # or assert that an arbitrary supplied inventory is authentic/complete.
    need(type(inv) is dict and inv.get('scope') == 'member-inventory-only'
         and inv.get('schema') in (1, 2, 3) and type(inv.get('schema')) is int
         and inv.get('ncn_acceptance') == 'Pending' and inv.get('legacy_observations') == 'Pending'
         and inv.get('count_equality_is_compatibility') is False)
    shape(inv['members'], ('MID', 'LYR', 'CUR'))
    for member in inv['members'].values():
        shape(member, ('bytes', 'sha256')); number(member['bytes']); hash_value(member['sha256'])
    if inv['schema'] in (2, 3):
        # Schema 1 remains legacy strict-C0 inventory; never reinterpret it.
        eof = inv['lyr']['logical_eof']
        shape(eof, ('kind', 'marker_byte_offset', 'logical_text_bytes', 'ignored_bytes'))
        number(eof['logical_text_bytes']); number(eof['ignored_bytes'])
        size = inv['members']['LYR']['bytes']
        need(eof['kind'] in ('Physical', 'Sub'))
        if eof['kind'] == 'Physical':
            need(eof['marker_byte_offset'] is None and eof['logical_text_bytes'] == size
                 and eof['ignored_bytes'] == 0)
        else:
            number(eof['marker_byte_offset'])
            need(eof['marker_byte_offset'] == eof['logical_text_bytes'] < size
                 and eof['ignored_bytes'] == size - eof['logical_text_bytes'] - 1)
    if inv['schema'] == 3:
        framing = inv['cur']['framing']
        need(type(framing) is dict)
        raw = inv['members']['CUR']['bytes']
        number(inv['cur']['trailing_bytes'])
        need(inv['cur']['trailing_bytes'] == raw % 2)
        if framing.get('status') == 'Selected':
            shape(framing, ('status', 'kind', 'marker_byte_offset', 'logical_cursor_bytes', 'framing_bytes'))
            number(framing['logical_cursor_bytes']); number(framing['framing_bytes'])
            need(framing['logical_cursor_bytes'] % 2 == 0
                 and framing['logical_cursor_bytes'] + framing['framing_bytes'] == raw)
            if framing['kind'] == 'Physical':
                need(raw % 2 == 0 and framing['marker_byte_offset'] is None
                     and framing['framing_bytes'] == 0)
            else:
                need(framing['kind'] == 'TerminalFF' and raw % 2 == 1)
                number(framing['marker_byte_offset'])
                need(framing['marker_byte_offset'] == raw - 1 and framing['framing_bytes'] == 1)
        else:
            shape(framing, ('status', 'code', 'byte_offset'))
            number(framing['byte_offset'])
            need(framing['status'] == 'Rejected' and framing['code'] == 'OddLength'
                 and raw % 2 == 1 and framing['byte_offset'] == raw - 1)
    number(inv['cur']['words'])
    need(inv['cur']['words'] == inv['members']['CUR']['bytes'] // 2)
    need(type(inv['cur']['selected']) is list and len(inv['cur']['selected']) <= 64)
    selected = set()
    for row in inv['cur']['selected']:
        shape(row, ('index', 'byte_offset', 'unsigned_value'))
        for value in row.values(): number(value)
        need(row['index'] < inv['cur']['words'] and row['index'] not in selected
             and row['byte_offset'] == row['index'] * 2 and row['unsigned_value'] <= 65535)
        selected.add(row['index'])
    shape(record, ('schema', 'sample_id', 'inventory_sha256', 'reader', 'run', 'observations', 'coverage', 'ncn_acceptance'))
    need(type(record['schema']) is int and record['schema'] == 1 and record['ncn_acceptance'] == 'Pending')
    need(type(record['sample_id']) is str and re.fullmatch('sample-[0-9]{3}', record['sample_id'])
         and record['sample_id'] == inv.get('sample_id'))
    shape(record['reader'], ('name', 'version', 'build_ref'))
    shape(record['run'], ('status', 'platform', 'observation_method'))
    for value in (*record['reader'].values(), record['run']['platform']): label(value)
    need(record['run']['status'] in ('Pending', 'Completed'))
    need(record['run']['observation_method'] in ('Pending', 'visual', 'reader-report', 'instrumented'))
    need(type(record['observations']) is list and len(record['observations']) <= 128)
    observations, observed_topics = set(), set()
    for row in record['observations']:
        shape(row, ('id', 'topic', 'cursor_index', 'measured_midi_tick', 'measured_media_us', 'action', 'method', 'artifact_sha256'))
        need(type(row['id']) is str and re.fullmatch('obs-[0-9]{3}', row['id']) and row['id'] not in observations)
        need(row['topic'] in TOPICS and row['action'] in ('Text', 'LineTransition', 'EntryMarker', 'Skipped', 'Unavailable'))
        number(row['cursor_index']); need(row['cursor_index'] in selected)
        need(row['method'] in ('visual', 'reader-report', 'instrumented'))
        need(row['method'] == record['run']['observation_method'])
        hash_value(row['artifact_sha256'])
        for name in ('measured_midi_tick', 'measured_media_us'):
            if row[name] is not None: number(row[name])
            if row['method'] == 'visual': need(row[name] is None)
        observations.add(row['id']); observed_topics.add(row['topic'])
    shape(record['coverage'], TOPICS)
    pending, skipped = 0, 0
    for topic, state in record['coverage'].items():
        shape(state, ('status', 'reason'))
        need(state['status'] in ('Pending', 'Observed', 'Skipped'))
        if state['status'] == 'Observed':
            need(topic in observed_topics and state['reason'] == 'recorded')
        elif state['status'] == 'Pending':
            need(topic not in observed_topics and state['reason'] == 'not-collected'); pending += 1
        else:
            need(topic not in observed_topics and state['reason'] in ('feature-absent', 'reader-unavailable', 'measurement-unavailable'))
            skipped += 1
    if observations:
        hash_value(record['inventory_sha256'])
        need(record['inventory_sha256'] == hashlib.sha256(inventory_bytes).hexdigest())
        need(record['run']['status'] == 'Completed' and record['run']['observation_method'] != 'Pending'
             and record['run']['platform'] != 'Pending'
             and record['reader']['name'] != 'Pending' and record['reader']['version'] != 'Pending')
    else:
        need(record['run']['status'] == 'Pending' and record['inventory_sha256'] in ('Pending', hashlib.sha256(inventory_bytes).hexdigest()))
    return dict(status='observation-record-checked', inventory_schema=inv['schema'],
                inventory_text_interpretation='StrictC0' if inv['schema'] == 1 else 'FirstSubEof',
                inventory_cursor_interpretation='OptionalTerminalFF' if inv['schema'] == 3 else 'Unframed',
                observations=len(observations),
                observed_topics=len(observed_topics), pending_topics=pending, skipped_topics=skipped,
                playback_authentication='NotIndependent', tempo_map_validation='Pending',
                mapping_and_sentinel_review='Pending', ncn_acceptance='Pending',
                limits=dict(input_bytes=CAP, staged_bytes=STAGED_CAP, reserved_bytes=reserved, output_bytes=OUTPUT_CAP))



class SafeParser(argparse.ArgumentParser):
    def error(self, message):
        # argparse's default error can echo a private argv value.
        print('{"status":"ObservationRecordUsage","ncn_acceptance":"Pending"}', file=sys.stderr)
        raise SystemExit(64)


def main(argv=None):
    parser = SafeParser(description=__doc__)
    parser.add_argument('inventory', type=Path); parser.add_argument('record', type=Path)
    args = parser.parse_args(argv)
    try:
        result = verify(bounded(args.inventory), bounded(args.record))
        output = json.dumps(result, sort_keys=True)
        need(len(output.encode('utf-8')) <= OUTPUT_CAP)
        print(output, flush=True)
        return 0
    except (ValueError, TypeError, KeyError, OSError, RecursionError, MemoryError):
        print('{"status":"ObservationRecordFailed","ncn_acceptance":"Pending"}', file=sys.stderr)
        return 1


if __name__ == '__main__': sys.exit(main())
