#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only, privacy-limited inventory for caller-selected lawful NCN members.

Container/header and count observations only. No playback, tempo-map decoding,
NCN cue normalization, sentinel selection or compatibility acceptance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys

MEMBER_CAPS = {'MID': 4 * 1024 * 1024, 'LYR': 64 * 1024, 'CUR': 64 * 1024}
AGGREGATE_CAP = 4 * 1024 * 1024
STAGED_CAP = 9 * 1024 * 1024
OUTPUT_CAP = 64 * 1024
SELECTED_CAP = 64
ANOMALY_CAP = 16


def require(condition):
    if not condition:
        raise ValueError('inventory input/boundary')


def read_member(path, cap):
    # A trusted-directory read, not hostile-path isolation or bundle atomicity.
    require(not path.is_symlink())
    flags = os.O_RDONLY | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_BINARY', 0)
    fd = os.open(path, flags)
    try:
        before = os.fstat(fd)
        require(stat.S_ISREG(before.st_mode) and before.st_size <= cap)
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            data = stream.read(cap + 1)
        after = os.fstat(fd)
        require(len(data) <= cap and len(data) == before.st_size
                and (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) ==
                    (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns))
        return data
    finally:
        os.close(fd)


def smf_container(data):
    # Validate only MThd/MTrk framing. Track events/EOT and tempo remain Pending.
    if len(data) < 14 or data[:4] != b'MThd':
        return dict(status='RejectedContainer', byte_offset=0)
    length, fmt, tracks, q = struct.unpack_from('>IHHH', data, 4)
    if length != 6 or fmt not in (0, 1) or tracks == 0 or (fmt == 0 and tracks != 1) or q == 0 or q >= 0x8000:
        return dict(status='UnsupportedHeader', byte_offset=4)
    at = 14
    for _ in range(tracks):
        if at + 8 > len(data) or data[at:at + 4] != b'MTrk':
            return dict(status='RejectedContainer', byte_offset=at)
        size = struct.unpack_from('>I', data, at + 4)[0]
        at += 8
        if at + size > len(data):
            return dict(status='RejectedContainer', byte_offset=at)
        at += size
    if at != len(data):
        return dict(status='RejectedContainer', byte_offset=at)
    return dict(status='ContainerObserved', format=fmt, tracks=tracks, ppqn=q,
                track_events='Pending', tempo_map='Pending', last_tick='Pending')


def scalar(byte):
    require(byte >= 32 or byte in (9, 10, 13))
    return ord(bytes((byte,)).decode('tis-620', errors='strict'))


def lyr_counts(data):
    # First-SUB logical EOF is NCN24 framing, not decoder leniency.
    marker = data.find(b'\x1a')
    logical_length = len(data) if marker < 0 else marker
    eof = dict(kind='Physical' if marker < 0 else 'Sub',
               marker_byte_offset=None if marker < 0 else marker,
               logical_text_bytes=logical_length,
               ignored_bytes=0 if marker < 0 else len(data) - marker - 1)
    data = memoryview(data)[:logical_length]
    # Explicit TIS-620 hypothesis; never guess/retry another encoding.
    at, crlf, cr, lf = 0, 0, 0, 0
    for _ in range(4):
        while at < len(data) and data[at] not in (10, 13):
            try:
                scalar(data[at])
            except (ValueError, UnicodeError):
                return dict(status='InvalidTextHypothesis', byte_offset=at, logical_eof=eof)
            at += 1
        if at == len(data):
            return dict(status='MissingHeaderTerminator', byte_offset=at, logical_eof=eof)
        at += 2 if data[at:at + 2] == b'\r\n' else 1
    start, count, spaces, tabs, marks = at, 0, 0, 0, 0
    previous_lf, blank_lines, terminal_lf = False, 0, False
    while at < len(data):
        byte = data[at]
        try:
            value = scalar(byte)
        except (ValueError, UnicodeError):
            return dict(status='InvalidTextHypothesis', byte_offset=at, logical_eof=eof)
        newline = byte in (10, 13)
        if data[at:at + 2] == b'\r\n':
            crlf += 1; width = 2
        elif byte == 13:
            cr += 1; width = 1
        else:
            lf += int(byte == 10); width = 1
        count += 1
        spaces += int(byte == 32); tabs += int(byte == 9)
        marks += int(value == 0x0E31 or 0x0E34 <= value <= 0x0E3A or 0x0E47 <= value <= 0x0E4E)
        blank_lines += int(newline and (previous_lf or at == start))
        previous_lf, terminal_lf = newline, newline
        at += width
    return dict(status='CountsObserved', encoding_hypothesis='TIS-620', logical_eof=eof, body_byte_offset=start,
                logical_body_bytes=len(data) - start, normalized_scalars=count, spaces=spaces,
                tabs=tabs, normalized_lf=crlf + cr + lf, crlf=crlf, cr=cr, lf=lf,
                terminal_lf=terminal_lf, empty_line_breaks=blank_lines,
                thai_mark_scalars=marks, grapheme_count='NotComputed')


def cur_counts(data, indices):
    count = len(data) // 2
    require(all(i < count for i in indices))
    previous, high_ff, low_ff, equal, decreasing = None, 0, 0, 0, 0
    anomalies, high_samples, selected = [], [], []
    for index, (value,) in enumerate(struct.iter_unpack('<H', memoryview(data)[:count * 2])):
        high_ff += int(value >= 0xFF00); low_ff += int(value & 255 == 255)
        if value >= 0xFF00 and len(high_samples) < ANOMALY_CAP:
            high_samples.append(index)
        if previous is not None:
            equal += int(value == previous)
            decreasing += int(value < previous)
            if value < previous and len(anomalies) < ANOMALY_CAP:
                anomalies.append(index)
        if index in indices:
            selected.append(dict(index=index, byte_offset=index * 2, unsigned_value=value))
        previous = value
    return dict(status='WordsObserved', endian_hypothesis='little', words=count,
                trailing_bytes=len(data) % 2, equal_pairs=equal, decreasing_pairs=decreasing,
                decreasing_indices=anomalies, decreasing_indices_truncated=decreasing > len(anomalies),
                high_ff_words=high_ff, high_ff_indices=high_samples,
                high_ff_indices_truncated=high_ff > len(high_samples), low_ff_words=low_ff,
                historical_high_ff_skip_count=count - high_ff, selected=selected,
                high_ff_policy='Unresolved')


def inspect(sample_id, members, indices):
    require(type(sample_id) is str and re.fullmatch(r'sample-[0-9]{3}', sample_id))
    require(set(members) == {'MID', 'LYR', 'CUR'} and type(indices) is list and len(indices) <= SELECTED_CAP)
    require(all(type(i) is int and 0 <= i < 32768 for i in indices) and len(set(indices)) == len(indices))
    size = sum(len(data) for data in members.values())
    require(size <= AGGREGATE_CAP and all(len(data) <= MEMBER_CAPS[k] for k, data in members.items()))
    # Raw inputs + conservative maximum read-copy overlap + output/retained
    # summary and conversion buffers; no decoded lyric string/vector retained.
    reserved = size + max(MEMBER_CAPS.values()) + 2 * OUTPUT_CAP
    require(reserved <= STAGED_CAP)
    lyric, cursor = lyr_counts(members['LYR']), cur_counts(members['CUR'], indices)
    same_count = (lyric['normalized_scalars'] == cursor['words']) if lyric['status'] == 'CountsObserved' else 'Unavailable'
    return dict(schema=2, sample_id=sample_id, scope='member-inventory-only',
                members={k: dict(bytes=len(v), sha256=hashlib.sha256(v).hexdigest()) for k, v in members.items()},
                primary=smf_container(members['MID']), lyr=lyric, cur=cursor,
                scalar_count_equals_cursor_words=same_count,
                count_equality_is_compatibility=False, bundle_atomicity='NotClaimed',
                legacy_observations='Pending', ncn_acceptance='Pending',
                limits=dict(member_bytes=MEMBER_CAPS, aggregate_bytes=AGGREGATE_CAP,
                    staged_bytes=STAGED_CAP, reserved_bytes=reserved, output_bytes=OUTPUT_CAP,
                    selected_positions=SELECTED_CAP, anomaly_indices=ANOMALY_CAP))



class SafeParser(argparse.ArgumentParser):
    def error(self, message):
        # argparse's default error can echo a private argv value.
        print('{"status":"InventoryUsage","ncn_acceptance":"Pending"}', file=sys.stderr)
        raise SystemExit(64)


def main(argv=None):
    parser = SafeParser(description=__doc__)
    for name in ('mid', 'lyr', 'cur'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--sample-id', required=True, help='opaque sample-NNN only')
    parser.add_argument('--indices', default='', help='up to 64 comma-separated CUR indices')
    parser.add_argument('--permission-basis', required=True, choices=('lawful-private', 'original-synthetic'))
    args = parser.parse_args(argv)
    try:
        require(len(args.indices) <= 384 and re.fullmatch(r'(?:[0-9]{1,5}(?:,[0-9]{1,5})*)?', args.indices))
        indices = [int(s) for s in args.indices.split(',')] if args.indices else []
        require(re.fullmatch(r'sample-[0-9]{3}', args.sample_id) and len(indices) <= SELECTED_CAP)
        members, total = {}, 0
        for name in ('MID', 'LYR', 'CUR'):
            # Decrease remaining allowance before reading the next member.
            data = read_member(getattr(args, name.lower()), min(MEMBER_CAPS[name], AGGREGATE_CAP - total))
            total += len(data); members[name] = data
        result = inspect(args.sample_id, members, indices)
        result['permission_basis'] = args.permission_basis
        result['permission_independently_verified'] = False
        result['python_version'] = '.'.join(map(str, sys.version_info[:3]))
        result['collector_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        output = json.dumps(result, sort_keys=True, separators=(',', ':')).encode('ascii') + b'\n'
        require(len(output) <= OUTPUT_CAP)
        sys.stdout.buffer.write(output); sys.stdout.buffer.flush()
        return 0
    except (ValueError, TypeError, OSError, MemoryError) as error:
        # Never print native exception text: it can contain paths/usernames.
        print(json.dumps(dict(status='InventoryFailed', error_family=type(error).__name__,
                              ncn_acceptance='Pending'), sort_keys=True), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
