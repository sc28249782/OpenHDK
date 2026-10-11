#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Production executable sessions in a harness-owned directory; no bypass flag.
Selected output/checkpoint observations only, not source authentication or sync tracing.
"""
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import struct
import subprocess
import sys
import tempfile
import time

MAX_LINE = 65536
CHILDREN = []


def emit(kind, **fields):
    print(json.dumps(dict(kind=kind, **fields), ensure_ascii=False, sort_keys=True), flush=True)


def primary(path):
    if not path.exists():
        return None
    with path.open('rb') as stream:
        wire = stream.read(64 * 1048576 + 1)
    if not 96 <= len(wire) <= 64 * 1048576:
        raise ValueError('checkpoint size')
    if wire[:8] != b'OHKCAT\0\0' or struct.unpack_from('<II', wire, 8) != (1, 0):
        raise ValueError('checkpoint header')
    payload, seq, rev, nr, ns, roots, songs = struct.unpack_from('<QQQQQII', wire, 16)
    if payload != len(wire) - 96 or hashlib.sha256(wire[:-32]).digest() != wire[-32:]:
        raise ValueError('checkpoint digest/length')
    return dict(size=len(wire), sha256=hashlib.sha256(wire).hexdigest(),
                digest=wire[-32:].hex(), sequence=seq, revision=rev,
                next_root=nr, next_song=ns, roots=roots, songs=songs)


class Session:
    def __init__(self, binary, store, mode, label):
        self.label = label
        self.p = subprocess.Popen([str(binary), '--store-dir', str(store),
                                  '--store-name', 'catalog', mode],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL)
        CHILDREN.append(self.p)
        self.pending = b''
        self.greeting = self.row()

    def row(self):
        deadline = time.monotonic() + 15
        while b'\n' not in self.pending:
            if len(self.pending) > MAX_LINE:
                raise ValueError('output line cap')
            wait = deadline - time.monotonic()
            if wait <= 0 or not select.select([self.p.stdout], [], [], wait)[0]:
                raise ValueError('output timeout')
            part = os.read(self.p.stdout.fileno(), 4096)
            if not part:
                raise ValueError('unexpected output EOF')
            self.pending += part
        line, self.pending = self.pending.split(b'\n', 1)
        if len(line) > MAX_LINE:
            raise ValueError('output line cap')
        row = json.loads(line.decode('utf-8'))
        if not isinstance(row, dict):
            raise ValueError('output object')
        return row

    def send(self, command):
        self.p.stdin.write(command.encode('utf-8') + b'\n')
        self.p.stdin.flush()

    def command(self, command):
        self.send(command)
        rows = [self.row()]
        extra = rows[0].get('count', 0) if rows[0].get('type') == 'page' else rows[0].get('diagnostics', 0)
        if not isinstance(extra, int) or not 0 <= extra <= 100:
            raise ValueError('response batch cap')
        rows.extend(self.row() for _ in range(extra))
        return rows

    def end(self, expected, quit_command=True):
        if quit_command:
            if self.command('quit') != [{'type': 'quit'}]:
                raise ValueError('quit output')
        self.p.stdin.close()
        rc = self.p.wait(timeout=15)
        if rc != expected:
            raise ValueError('child exit mismatch')
        if self.pending or (not self.p.stdout.closed and self.p.stdout.read(1)):
            raise ValueError('unexpected trailing output')
        emit('exit', session=self.label, code=rc)
        self.p.stdout.close()


def run(binary, parent):
    binary = binary.resolve(strict=True)
    parent = parent.resolve(strict=True)
    # Eligibility is enforced by the production executable, not this label alone.
    fs = subprocess.run(['findmnt', '-n', '-o', 'FSTYPE', '-T', str(parent)],
                        capture_output=True, timeout=10, check=True).stdout.strip()
    if fs != b'ext4':
        emit('summary', status='Skipped', reason='ext4 parent required', native_acceptance='Pending')
        return 69
    base = Path(tempfile.mkdtemp(prefix='openhdk-cli-matrix-', dir=parent))
    completed = False
    try:
        store, songs, moved = base / 'store', base / 'songs', base / 'moved'
        store.mkdir(); songs.mkdir()
        track = bytes([0, 255, 5, 1, 65, 0, 255, 47, 0])
        midi = b'MThd' + struct.pack('>IHHH', 6, 0, 1, 3) + b'MTrk' + struct.pack('>I', len(track)) + track
        (songs / 'Live.Set.kar').write_bytes(midi)
        checkpoint = store / 'catalog'
        if binary.stat().st_size > 64 * 1048576:
            raise ValueError('executable size cap')
        emit('environment', filesystem='ext4', production_executable=True, bypass=False,
             executable_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), executable_bytes=binary.stat().st_size,
             fixture_sha256=hashlib.sha256(midi).hexdigest(), fixture_bytes=len(midi))
        count = 0

        def record(case, before, rows, **fields):
            nonlocal count
            # Commands/hints/paths/wire bytes are never receipts. CLI output itself
            # contains relative locators and bounded metadata; reject path leakage.
            text = json.dumps(rows, ensure_ascii=False)
            if str(base) in text:
                raise ValueError('absolute path in CLI output')
            emit('case', case=case, before=before, after=primary(checkpoint), rows=rows, **fields)
            count += 1

        def command(session, case, text):
            before = primary(checkpoint)
            record(case, before, session.command(text))

        s = Session(binary, store, 'create', 'initial')
        record('create', None, [s.greeting])
        command(s, 'register', 'register ' + json.dumps(str(songs), ensure_ascii=False))
        command(s, 'scan', 'scan 1')
        command(s, 'listing', 'songs 1 0 100')
        command(s, 'prepare', 'prepare 1')
        command(s, 'overrides', 'overrides 1 "เพลงทดสอบ" "ศิลปิน"')
        command(s, 'nochange', 'overrides 1 "เพลงทดสอบ" "ศิลปิน"')
        command(s, 'prepare-overrides', 'prepare 1')
        command(s, 'invalid-selector', 'remove +1')
        command(s, 'relocate', 'relocate 1 1 Live.Set.kar')
        command(s, 'prepare-notready', 'prepare 1')
        command(s, 'rescan', 'scan 1')
        command(s, 'remove', 'remove 1')
        command(s, 'rediscover', 'scan 1')
        s.end(1)
        collision = Session(binary, store, 'create', 'create-existing')
        record('create-existing', primary(checkpoint), [collision.greeting])
        collision.end(1, False)
        s = Session(binary, store, 'session', 'reopened')
        record('reopen', primary(checkpoint), [s.greeting])
        command(s, 'reopen-unattached', 'roots')
        command(s, 'prepare-unattached', 'prepare 2')
        command(s, 'attach-hint', 'reattach 1 ' + json.dumps(str(songs), ensure_ascii=False))
        command(s, 'attach-roots', 'roots')
        command(s, 'reattach-nochange', 'reattach 1 ' + json.dumps(str(songs), ensure_ascii=False))
        command(s, 'prepare-unscanned', 'prepare 2')
        command(s, 'scan-reopened', 'scan 1')
        command(s, 'prepare-reopened', 'prepare 2')
        contender = Session(binary, store, 'session', 'contender')
        record('busy', primary(checkpoint), [contender.greeting])
        contender.end(75, False)
        songs.rename(moved)
        command(s, 'drift', 'overrides 2 Drift Artist')
        s.end(76, False)
        s = Session(binary, store, 'session', 'recovery')
        record('recovery-reopen', primary(checkpoint), [s.greeting])
        command(s, 'recovery-attach', 'reattach 1 ' + json.dumps(str(moved), ensure_ascii=False))
        command(s, 'recovery-roots', 'roots')
        command(s, 'recovery-scan', 'scan 1')
        before = primary(checkpoint)
        # Reader is closed only after greeting/previous replies were observed.
        # Exactly one mutation is sent. SIGPIPE is ignored by production main.
        s.p.stdout.close()
        s.send('overrides 2 "receipt lost" "ศิลปิน"')
        s.end(74, False)
        record('output-loss', before, [], delivery_exit=74, mutation_attempts=1)
        s = Session(binary, store, 'session', 'after-output-loss')
        record('output-reopen', primary(checkpoint), [s.greeting])
        command(s, 'output-listing', 'songs 1 0 1')
        command(s, 'output-attach', 'reattach 1 ' + json.dumps(str(moved), ensure_ascii=False))
        command(s, 'output-roots', 'roots')
        command(s, 'output-scan', 'scan 1')
        command(s, 'output-prepare', 'prepare 2')
        s.end(0)
        completed = True
        emit('summary', status='completed', cases=count, native_acceptance='Pending',
             paths_emitted=False, raw_wires_emitted=False, power_loss_tested=False)
        return 0
    finally:
        for child in CHILDREN:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill(); child.wait(timeout=3)
        # Owned fixture cleanup only, after selected checkpoint observations.
        # A failed harness retains its fixture for local investigation, not git.
        if completed:
            shutil.rmtree(base)


if __name__ == '__main__':
    try:
        if len(sys.argv) != 3:
            raise ValueError('usage: run-library-cli-evidence.py EXECUTABLE EXT4_PARENT')
        sys.exit(run(Path(sys.argv[1]), Path(sys.argv[2])))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        # Do not leak usernames, paths or command text in published diagnostics.
        emit('failure', status='failed', error_type=type(error).__name__, native_acceptance='Pending')
        sys.exit(1)
