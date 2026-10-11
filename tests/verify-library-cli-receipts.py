#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded receipt consistency. Assertions/hashes do not authenticate execution."""
import hashlib
import json
import struct
from pathlib import Path
import re
import sys

CASES = ('create register scan listing prepare overrides nochange prepare-overrides '
         'invalid-selector relocate prepare-notready rescan remove rediscover create-existing reopen '
         'reopen-unattached prepare-unattached attach-hint attach-roots reattach-nochange prepare-unscanned scan-reopened '
         'prepare-reopened busy drift recovery-reopen recovery-attach recovery-roots recovery-scan '
         'output-loss output-reopen output-listing output-attach output-roots output-scan output-prepare').split()
SAVED = set('register scan overrides relocate rescan remove rediscover attach-hint scan-reopened '
            'recovery-attach recovery-scan output-loss output-attach output-scan'.split())
PREPARED = set('prepare prepare-overrides prepare-reopened output-prepare'.split())
ERRORS = set('invalid-selector prepare-notready prepare-unattached prepare-unscanned busy drift create-existing'.split())
EXITS = {'initial': 1, 'create-existing': 1, 'reopened': 76, 'contender': 75, 'recovery': 74, 'after-output-loss': 0}


def require(value, message):
    if not value:
        raise ValueError(message)


def integer(value, minimum=0):
    return type(value) is int and minimum <= value < 2**64


def checkpoint(row):
    require(isinstance(row, dict), 'checkpoint object')
    require(set(row) == {'size', 'sha256', 'digest', 'sequence', 'revision', 'next_root', 'next_song', 'roots', 'songs'}, 'checkpoint fields')
    require(integer(row['size'], 96) and row['size'] <= 64 * 1048576, 'size range')
    for key in ('sha256', 'digest'):
        require(isinstance(row[key], str) and re.fullmatch('[0-9a-f]{64}', row[key]), 'digest shape')
    for key in ('sequence', 'next_root', 'next_song'):
        require(integer(row[key], 1), 'counter range')
    for key in ('revision', 'roots', 'songs'):
        require(integer(row[key]), 'integer range')
    require(row['roots'] <= 32 and row['songs'] <= 1, 'fixture row bounds')


def privacy(value):
    if isinstance(value, dict):
        require(not any(k in value for k in ('hint', 'path', 'wire', 'ticket', 'pointer', 'epoch')), 'private fields')
        for child in value.values(): privacy(child)
    elif isinstance(value, list):
        for child in value: privacy(child)
    elif isinstance(value, str):
        require(not value.startswith('/'), 'absolute path in response')


def verify_text(text):
    require(len(text.encode('utf-8')) <= 2 * 1048576, 'log cap')
    lines = text.splitlines()
    require(len(lines) <= 128, 'row cap')
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'duplicate JSON key')
            result[key] = value
        return result
    rows = [json.loads(line, object_pairs_hook=unique) for line in lines]
    require(all(isinstance(r, dict) and r.get('kind') in ('environment', 'case', 'exit', 'summary') for r in rows), 'receipt kind')
    env = [r for r in rows if r['kind'] == 'environment']
    require(len(env) == 1 and env[0]['filesystem'] == 'ext4' and env[0]['production_executable'] is True and env[0]['bypass'] is False, 'environment claims')
    track = bytes([0, 255, 5, 1, 65, 0, 255, 47, 0])
    fixture = b'MThd' + struct.pack('>IHHH', 6, 0, 1, 3) + b'MTrk' + struct.pack('>I', len(track)) + track
    require(env[0]['fixture_bytes'] == len(fixture) and env[0]['fixture_sha256'] == hashlib.sha256(fixture).hexdigest(), 'fixture')
    require(integer(env[0]['executable_bytes'], 1) and env[0]['executable_bytes'] <= 64 * 1048576 and re.fullmatch('[0-9a-f]{64}', env[0]['executable_sha256']), 'executable observation')
    cases = [r for r in rows if r['kind'] == 'case']
    require([r['case'] for r in cases] == CASES, 'case order/completeness')
    require(all(type(r.get('code')) is int for r in rows if r['kind'] == 'exit'), 'exit type')
    require({r['session']: r['code'] for r in rows if r['kind'] == 'exit'} == EXITS, 'child exits')
    require(len([r for r in rows if r['kind'] == 'exit']) == len(EXITS), 'duplicate exit')
    summary = [r for r in rows if r['kind'] == 'summary']
    require(len(summary) == 1 and rows[-1] == summary[0], 'terminal summary')
    require(summary[0] == dict(kind='summary', status='completed', cases=len(CASES), native_acceptance='Pending', paths_emitted=False, raw_wires_emitted=False, power_loss_tested=False), 'summary claims')
    previous = None
    indexed = {}
    for r in cases:
        case, before, after, output = r['case'], r['before'], r['after'], r['rows']
        require(set(r) == ({'kind', 'case', 'before', 'after', 'rows', 'delivery_exit', 'mutation_attempts'} if case == 'output-loss' else {'kind', 'case', 'before', 'after', 'rows'}), 'case fields')
        require(before == previous, 'checkpoint continuity')
        checkpoint(after)
        if before is not None:
            checkpoint(before)
        require(isinstance(output, list) and len(output) <= 101 and all(isinstance(x, dict) for x in output), 'output batch')
        privacy(output)
        if output and output[0].get('type') == 'page':
            require(len(output) == output[0].get('count', -1) + 1 and output[0].get('revision') == after['revision'], 'page shape')
        if case == 'create':
            require(before is None and after['sequence'] == 1 and after['revision'] == 0 and after['roots'] == after['songs'] == 0, 'create baseline')
            require(output == [dict(type='session', mode='create', status='Saved', sequence=1, revision=0)], 'create acknowledgment')
        elif case in SAVED:
            require(after['sequence'] == before['sequence'] + 1 and after['revision'] == before['revision'] + 1 and after['sha256'] != before['sha256'], 'Saved transition')
            require(after['next_root'] == before['next_root'] + (1 if case == 'register' else 0) and after['next_song'] == before['next_song'] + (1 if case in ('scan', 'rediscover') else 0), 'high-water transition')
            require(after['roots'] == before['roots'] + (1 if case == 'register' else 0), 'root count transition')
            require(after['songs'] == (1 if case in ('scan', 'rediscover') else 0 if case == 'remove' else before['songs']), 'song count transition')
            if case == 'output-loss':
                require(output == [] and r['delivery_exit'] == 74 and r['mutation_attempts'] == 1, 'lost receipt/retry')
            else:
                require(output and output[0].get('type') == 'commit' and output[0].get('status') == 'Saved' and output[0].get('revision') == after['revision'] and output[0].get('sequence') == after['sequence'], 'Saved acknowledgment')
                require(len(output) == 1 + output[0].get('diagnostics', 0), 'diagnostic count')
        else:
            require(before == after, 'query/rejection changed primary')
            require(output, 'missing output')
            if case in ('nochange', 'reattach-nochange'):
                require(output == [dict(type='commit', status='NoChange', revision=after['revision'], sequence=after['sequence'])], 'NoChange acknowledgment')
            elif case in PREPARED:
                p = output[0]
                require(len(output) == 1 and p.get('type') == 'prepared' and p.get('revision') == after['revision'], 'prepared output')
                require(p.get('song') == (1 if case in ('prepare', 'prepare-overrides') else 2), 'prepared identity')
                require(p.get('source_bytes') == 31 and p.get('cue_count') == 1 and p.get('encoding') == 'utf8' and p.get('lyric_profile') == 1 and p.get('track') == 0, 'prepared source')
                require(integer(p.get('event_count')) and p['event_count'] >= 1 and integer(p.get('last_event_us')), 'prepared events')
                require(not any(k in p for k in ('sequence', 'status', 'commit', 'receipt')), 'query commit receipt')
                title, artist, origin = ('เพลงทดสอบ', 'ศิลปิน', 0) if case == 'prepare-overrides' else ('receipt lost', 'ศิลปิน', 0) if case == 'output-prepare' else ('Live.Set', None, 2)
                require((p.get('title'), p.get('artist'), p.get('title_origin')) == (title, artist, origin), 'prepared metadata')
            elif case in ERRORS:
                require(len(output) == 1 and output[0].get('type') == 'error', 'error output')
                if case == 'create-existing':
                    require(output[0].get('family') == 'service' and output[0].get('code') == 7, 'Create admission rejection')
                elif case == 'busy':
                    require(output[0].get('store_code') == 8, 'Busy family')
                elif case == 'drift':
                    require('binding_code' in output[0] and output[0].get('operation') == 2, 'authority drift family')
                elif case.startswith('prepare-'):
                    require(output[0].get('operation') == 9 and output[0].get('preparation_code') == (3 if case == 'prepare-unattached' else 2) and 'store_code' not in output[0], 'content failure family')
                else:
                    require(output[0].get('family') == 'grammar', 'selector grammar')
            elif case.endswith('reopen') or case == 'reopen':
                require(output == [dict(type='session', mode='session', status='Opened')], 'Open greeting')
        indexed[case] = r
        previous = after
    def item(case, kind):
        found = [r for r in indexed[case]['rows'] if r.get('type') == kind]
        require(len(found) == 1, 'listing row')
        return found[0]
    root = item('reopen-unattached', 'root')
    require(root.get('id') == 1 and root.get('attached') is False and root.get('generation') == 1, 'restored root auto-attached')
    for case, generation in (('attach-roots', 2), ('recovery-roots', 3), ('output-roots', 4)):
        attached = item(case, 'root')
        require(attached.get('attached') is True and attached.get('generation') == generation and attached.get('id') == 1, 'attachment generation')
    listed = item('listing', 'song')
    require(listed.get('locator') == {'encoding':'utf8', 'value':'Live.Set.kar'} and listed.get('state') == 0, 'Ready locator')
    require(listed.get('id') == 1 and listed.get('title') == 'Live.Set' and listed.get('title_origin') == 2, 'filename fallback')
    lost = item('output-listing', 'song')
    require((lost.get('id'), lost.get('title'), lost.get('artist'), lost.get('title_origin')) == (2, 'receipt lost', 'ศิลปิน', 0), 'lost receipt persisted override')
    require(indexed['register']['after']['next_root'] == 2 and indexed['register']['after']['roots'] == 1, 'root allocation')
    require(indexed['scan']['after']['next_song'] == 2 and indexed['scan']['after']['songs'] == 1, 'song allocation')
    require(indexed['remove']['after']['songs'] == 0 and indexed['remove']['after']['next_song'] == 2, 'remove reused counter')
    require(indexed['rediscover']['after']['songs'] == 1 and indexed['rediscover']['after']['next_song'] == 3, 'rediscovery high-water')
    return dict(status='verified', cases=len(cases), child_exits=len(EXITS), native_acceptance='Pending')


if __name__ == '__main__':
    try:
        require(len(sys.argv) == 2, 'usage: verify-library-cli-receipts.py CLI_RUN_LOG')
        path = Path(sys.argv[1]); require(path.stat().st_size <= 2 * 1048576, 'file cap')
        print(json.dumps(verify_text(path.read_text(encoding='utf-8')), sort_keys=True))
    except (ValueError, KeyError, TypeError, OSError) as error:
        print('CLI receipt verification failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
