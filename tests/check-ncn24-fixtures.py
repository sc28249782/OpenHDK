#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded offline NCN24 hypothesis checker for original synthetic fixtures only.

This is not an application parser/normalizer or a real-song compatibility test.
The restricted SMF reader accepts format-0 tempo/EOT-only fixture tracks.
"""
import argparse
from fractions import Fraction
import hashlib
import json
import os
import stat
from pathlib import Path
import re
import struct
import sys

MANIFEST_CAP = 128 * 1024
MEMBER_CAP = 4096
DECODED_CAP = 64 * 1024
OUTPUT_CAP = 64 * 1024
EXPORT_CAP = 256 * 1024
POSITION_CAP = 256
STAGED_CAP = 512 * 1024
CASE_IDS = frozenset('empty quarter lf crlf cr terminal-lf blank-line space tab '
    'line-entry-hypothesis thai-combining thai-am thai-am-decomposed equal-positions '
    'floor-collision floor-all-zero tempo-change decreasing odd nul undefined-tis '
    'beyond-primary low-ff high-fe high-ff-zero high-ff-max missing-header empty-cur '
    'empty-body header-crlf header-cr leading-lf high-ff-middle '
    'sub-empty sub-empty-crlf sub-text sub-text-crlf sub-body-crlf sub-body-lf '
    'sub-following-text sub-repeated sub-following-lf sub-following-cr '
    'sub-following-invalid sub-prefix-nul sub-prefix-undefined '
    'sub-header-1 sub-header-2 sub-header-3 sub-header-4 sub-missing-header'.split())
MIDI_IDS = frozenset(('M480', 'M3', 'Mhigh', 'Mtempo'))


class FixtureError(ValueError):
    def __init__(self, code, member, offset):
        self.result = dict(status='Rejected', code=code, member=member, byte_offset=offset)
        super().__init__(code)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def keys(value, expected):
    require(type(value) is dict and set(value) == set(expected), 'object shape')


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key')
        result[key] = value
    return result


def read_bounded(path):
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NONBLOCK', 0))
    try:
        info = os.fstat(fd)
        require(stat.S_ISREG(info.st_mode) and info.st_size <= MANIFEST_CAP,
                'regular manifest/byte limit')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            data = stream.read(MANIFEST_CAP + 1)
    finally:
        os.close(fd)
    require(len(data) <= MANIFEST_CAP, 'manifest byte limit')
    return data


def validate_json(value, depth=0):
    require(depth <= 12, 'JSON depth limit')
    if type(value) is dict:
        require(len(value) <= 64, 'object key limit')
        for key, item in value.items():
            require(type(key) is str and len(key) <= 64, 'key limit')
            validate_json(item, depth + 1)
    elif type(value) is list:
        require(len(value) <= POSITION_CAP, 'array limit')
        for item in value:
            validate_json(item, depth + 1)
    elif type(value) is int:
        require(0 <= value < 2**64, 'integer range')
    elif type(value) is str:
        require(len(value) <= 2 * MEMBER_CAP, 'string limit')
    else:
        raise ValueError('unsupported JSON value')


def parse_manifest(data):
    require(len(data) <= MANIFEST_CAP, 'manifest byte limit')
    manifest = json.loads(data.decode('utf-8'), object_pairs_hook=unique_object)
    validate_json(manifest)
    keys(manifest, ('schema', 'scope', 'ncn_acceptance', 'midis', 'cases', 'timing'))
    require(manifest['schema'] == 2 and manifest['scope'] == 'synthetic-only'
            and manifest['ncn_acceptance'] == 'Pending', 'scope/acceptance')
    return manifest


def unhex(text):
    require(type(text) is str and len(text) <= 2 * MEMBER_CAP
            and len(text) % 2 == 0 and re.fullmatch('[0-9a-f]*', text), 'hex encoding/limit')
    return bytes.fromhex(text)


def midi_fixture(data):
    require(len(data) >= 26 and data[:4] == b'MThd', 'fixture SMF header')
    length, fmt, count, q = struct.unpack_from('>IHHH', data, 4)
    require(length == 6 and fmt == 0 and count == 1 and 0 < q < 0x8000,
            'fixture SMF format/division')
    require(data[14:18] == b'MTrk' and struct.unpack_from('>I', data, 18)[0] == len(data) - 22,
            'fixture SMF track length')
    at, tick, tempo, events, ended = 22, 0, [(0, 500000)], 0, False

    def vlq():
        nonlocal at
        value = 0
        for _ in range(4):
            require(at < len(data), 'truncated fixture VLQ')
            byte = data[at]; at += 1
            value = (value << 7) | (byte & 127)
            if byte < 128:
                return value
        raise ValueError('fixture VLQ exceeds four bytes')

    while at < len(data):
        events += 1
        require(events <= 128 and not ended, 'fixture event limit/EOT')
        tick += vlq()
        require(tick < 2**32 and at + 2 <= len(data) and data[at] == 255, 'fixture event')
        kind = data[at + 1]; at += 2
        size = vlq()
        require(at + size <= len(data), 'truncated fixture meta')
        payload = data[at:at + size]; at += size
        if kind == 81:
            require(size == 3 and int.from_bytes(payload, 'big') > 0, 'fixture tempo')
            tempo.append((tick, int.from_bytes(payload, 'big')))
        elif kind == 47:
            require(size == 0 and at == len(data), 'fixture EOT')
            ended = True
        else:
            raise ValueError('unsupported synthetic SMF event')
    require(ended, 'missing fixture EOT')
    return dict(ppqn=q, last_tick=tick, tempo=tempo)


def media_time(midi, tick):
    elapsed, previous, rate = Fraction(0), 0, 500000
    for boundary, new_rate in midi['tempo']:
        if boundary > tick:
            break
        elapsed += Fraction((boundary - previous) * rate, midi['ppqn'])
        previous, rate = boundary, new_rate
    elapsed += Fraction((tick - previous) * rate, midi['ppqn'])
    return elapsed.numerator // elapsed.denominator


def text_scalar(byte, offset):
    if byte < 32 and byte not in (9, 10, 13):
        raise FixtureError('InvalidText', 'LYR', offset)
    try:
        return ord(bytes((byte,)).decode('tis-620', errors='strict'))
    except UnicodeError:
        raise FixtureError('InvalidText', 'LYR', offset) from None


def logical_eof(lyr):
    marker = lyr.find(b'\x1a')
    length = len(lyr) if marker < 0 else marker
    return dict(kind='Physical' if marker < 0 else 'Sub',
                marker_byte_offset=None if marker < 0 else marker,
                logical_text_bytes=length,
                ignored_bytes=0 if marker < 0 else len(lyr) - marker - 1)


def body_observation(lyr):
    # NCN24 framing only; shared text_scalar still rejects C0 SUB.
    lyr = memoryview(lyr)[:logical_eof(lyr)['logical_text_bytes']]
    at = 0
    for _ in range(4):
        while at < len(lyr) and lyr[at] not in (10, 13):
            text_scalar(lyr[at], at); at += 1
        if at == len(lyr):
            raise FixtureError('HeaderTermination', 'LYR', at)
        at += 2 if lyr[at:at + 2] == b'\r\n' else 1
    scalars, offsets = [], []
    while at < len(lyr):
        byte = lyr[at]
        scalar = text_scalar(byte, at)
        require(len(scalars) < POSITION_CAP, 'scalar limit')
        offsets.append(at)
        scalars.append(10 if byte == 13 else scalar)
        at += 2 if lyr[at:at + 2] == b'\r\n' else 1
    return scalars, offsets


def candidate_observation(midi, lyr, cur):
    # Diagnostic validation order is fixed here, not a production error contract.
    try:
        scalars, offsets = body_observation(lyr)
        if len(cur) % 2:
            raise FixtureError('OddLength', 'CUR', len(cur) - 1)
        require(len(cur) // 2 <= POSITION_CAP, 'position limit')
        positions = list(struct.unpack('<' + 'H' * (len(cur) // 2), cur))
        ticks = []
        for index, value in enumerate(positions):
            if index and value < positions[index - 1]:
                raise FixtureError('Decreasing', 'CUR', index * 2)
            tick = value * midi['ppqn'] // 24
            if tick > midi['last_tick']:
                raise FixtureError('BeyondPrimary', 'CUR', index * 2)
            ticks.append(tick)
        if len(scalars) != len(positions):
            raise FixtureError('CountMismatch', 'CUR', len(cur))
        return dict(status='Candidate' if scalars else 'NoLyrics', scalars=scalars,
                    member_offsets=offsets, positions=positions, ticks=ticks,
                    media_us=[media_time(midi, tick) for tick in ticks])
    except FixtureError as error:
        return error.result


def logical_owned(value):
    # Logical JSON strings/keys and eight bytes per numeric value. Container,
    # allocator and interpreter overhead are outside this payload accounting.
    if type(value) is dict:
        return sum(len(key.encode('utf-8')) + logical_owned(item) for key, item in value.items())
    if type(value) is list:
        return sum(logical_owned(item) for item in value)
    return len(value.encode('utf-8')) if type(value) is str else 8


def check(manifest, manifest_bytes=MANIFEST_CAP):
    validate_json(manifest)
    keys(manifest, ('schema', 'scope', 'ncn_acceptance', 'midis', 'cases', 'timing'))
    require(manifest['schema'] == 2 and manifest['scope'] == 'synthetic-only'
            and manifest['ncn_acceptance'] == 'Pending', 'scope/acceptance')
    # One conservative coexistence allowance: raw manifest + parsed payload +
    # decoded members + retained/serialized receipts + conversion/work buffers.
    reserved = (manifest_bytes + logical_owned(manifest) + DECODED_CAP +
                2 * OUTPUT_CAP + 4 * MEMBER_CAP + POSITION_CAP * 5 * 8)
    require(0 <= manifest_bytes <= MANIFEST_CAP and reserved <= STAGED_CAP,
            'shared logical staging limit')
    keys(manifest['midis'], MIDI_IDS)
    require(type(manifest['cases']) is list and len(manifest['cases']) == len(CASE_IDS),
            'case completeness')
    require(type(manifest['timing']) is list and len(manifest['timing']) == 4, 'timing matrix')
    used, total, midis, members, results = set(), 0, {}, {}, []
    for name, row in manifest['midis'].items():
        keys(row, ('hex', 'ppqn', 'last_tick', 'last_us'))
        data = unhex(row['hex']); total += len(data)
        observation = midi_fixture(data)
        require((observation['ppqn'], observation['last_tick'],
                 media_time(observation, observation['last_tick'])) ==
                (row['ppqn'], row['last_tick'], row['last_us']), 'SMF expectation: ' + name)
        midis[name], members[name] = observation, data
    for row in manifest['cases']:
        keys(row, ('id', 'midi', 'lyr_hex', 'cur_hex', 'expected'))
        name = row['id']
        require(name in CASE_IDS and name not in used and row['midi'] in MIDI_IDS,
                'case identity/completeness')
        used.add(name)
        lyr, cur = unhex(row['lyr_hex']), unhex(row['cur_hex'])
        total += len(lyr) + len(cur)
        require(total <= DECODED_CAP, 'aggregate decoded bytes')
        actual = candidate_observation(midis[row['midi']], lyr, cur)
        require(actual == row['expected'], 'candidate expectation: ' + name)
        results.append(dict(case=name, expected_status=actual['status'],
            logical_eof=logical_eof(lyr),
            mid_sha256=hashlib.sha256(members[row['midi']]).hexdigest(),
            lyr_sha256=hashlib.sha256(lyr).hexdigest(), cur_sha256=hashlib.sha256(cur).hexdigest()))
    require(used == CASE_IDS, 'case completeness')
    bpms = set()
    for row in manifest['timing']:
        keys(row, ('bpm', 'wiki_units', 'musical_units'))
        bpm = row['bpm']
        require(bpm in (60, 90, 120, 180) and bpm not in bpms, 'BPM matrix')
        bpms.add(bpm)
        candidate = Fraction(24 * 60, bpm)
        require(row['wiki_units'] == [candidate.numerator, candidate.denominator]
                and row['musical_units'] == 24, 'timing expectation')
    return dict(schema=2, status='fixture-verified', scope='synthetic-only', cases=len(results),
                primary_vectors=len(midis), decoded_bytes=total, receipts=results,
                representative_observations='Pending', ncn_acceptance='Pending',
                limits=dict(manifest_bytes=MANIFEST_CAP, member_bytes=MEMBER_CAP,
                    decoded_bytes=DECODED_CAP, output_bytes=OUTPUT_CAP,
                    staged_bytes=STAGED_CAP, reserved_bytes=reserved,
                    export_bytes=EXPORT_CAP, positions=POSITION_CAP))


def export_fixtures(manifest, directory):
    # Call only after check succeeds. Existing targets are never overwritten.
    total = sum(len(unhex(manifest['midis'][r['midi']]['hex'])) +
                len(unhex(r['lyr_hex'])) + len(unhex(r['cur_hex'])) for r in manifest['cases'])
    require(total <= EXPORT_CAP, 'export byte limit')
    directory.mkdir()  # Existing directories, files and symlinks fail here.
    for row in manifest['cases']:
        for folder, suffix, data in (
            ('Song', 'mid', unhex(manifest['midis'][row['midi']]['hex'])),
            ('Lyrics', 'lyr', unhex(row['lyr_hex'])), ('Cursor', 'cur', unhex(row['cur_hex']))):
            parent = directory / row['id'] / folder
            parent.mkdir(parents=True)
            with (parent / ('example.' + suffix)).open('xb') as stream:
                stream.write(data)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).parent / 'fixtures/ncn24/vectors.json')
    parser.add_argument('--export', type=Path, help='new directory for synthetic bundles, including rejection cases')
    args = parser.parse_args(argv)
    try:
        data = read_bounded(args.manifest)
        manifest = parse_manifest(data)
        result = check(manifest, len(data))
        output = json.dumps(result, sort_keys=True, separators=(',', ':')).encode('ascii') + b'\n'
        require(len(output) <= OUTPUT_CAP, 'output byte limit')
        if args.export is not None:
            export_fixtures(manifest, args.export)
        sys.stdout.buffer.write(output)
        sys.stdout.buffer.flush()
        return 0
    except (ValueError, TypeError, KeyError, OSError, OverflowError, RecursionError, MemoryError) as error:
        # Do not leak native paths, usernames or lyric bytes in error text.
        print(json.dumps(dict(status='fixture-check-failed', error_family=type(error).__name__,
                              ncn_acceptance='Pending'), sort_keys=True), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
