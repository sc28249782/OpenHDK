#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Selected receipt consistency only: no source, byte provenance or durability proof."""
import json
import re
import sys
from pathlib import Path

OPS = ('register', 'reattach', 'scan', 'relocate', 'remove')
SCENARIOS = ('saved', 'write-failure', 'late-fence', 'uncertain', 'late-cancel')
SPECIALS = ('targeted-repair', 'same-path-nochange', 'equal-key-relocate',
            'remove-rediscovery', 'same-path-drift', 'unrelated-drift',
            'store-containment', 'saved-hint-attachment', 'descriptor-peak')
CASES = {f'{op}-{scenario}' for op in OPS for scenario in SCENARIOS}
ALL = CASES | set(SPECIALS)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(row, key):
    value = row[key]
    require(re.fullmatch(r'-?[0-9]+', value), f'invalid integer: {key}')
    return int(value)


def digest(value):
    return bool(re.fullmatch(r'[0-9a-f]{64}', value))


def verify_text(text):
    require(len(text.encode('utf-8')) <= 24 * 1048576, 'log limit')
    rows = []
    for line in text.splitlines():
        if not line.startswith('MUTATION_'):
            continue
        fields = line.split()
        pairs = [item.split('=', 1) for item in fields[1:]]
        require(all(len(p) == 2 for p in pairs), 'malformed field')
        require(len({p[0] for p in pairs}) == len(pairs), 'duplicate field')
        rows.append((fields[0], dict(pairs)))
    require(len(rows) <= 20000, 'row limit')

    def selected(kind):
        return [r for k, r in rows if k == kind]

    def indexed(kind, keys=('case',)):
        result = {}
        for row in selected(kind):
            key = tuple(row[k] for k in keys)
            require(key not in result, f'duplicate {kind}: {key}')
            result[key] = row
        return result

    known = {'MUTATION_CASE', 'MUTATION_PRIMARY', 'MUTATION_ROOT', 'MUTATION_SONG',
             'MUTATION_ACK', 'MUTATION_RECOVERY', 'MUTATION_SPECIAL', 'MUTATION_SUMMARY',
             'MUTATION_ALLOCATION', 'MUTATION_IO_TRACE', 'MUTATION_IO', 'MUTATION_ARTIFACT',
             'MUTATION_ADMISSION_TRACE', 'MUTATION_ADMISSION_RECORD'}
    require(all(k in known for k, _ in rows), 'unknown receipt type')
    require(all(r['case'] in ALL for _, r in rows if 'case' in r), 'unknown case')
    cases = indexed('MUTATION_CASE')
    require({k[0] for k in cases} == CASES, 'operation matrix incomplete')
    special = indexed('MUTATION_SPECIAL')
    require({k[0] for k in special} == set(SPECIALS), 'special matrix incomplete')
    primaries = indexed('MUTATION_PRIMARY', ('case', 'phase'))
    expected = {(c, p) for c in ALL for p in ('before', 'after')} | {('remove-rediscovery', 'removed')}
    require(set(primaries) == expected, 'primary matrix incomplete')
    roots, songs = {}, {}
    for key, header in primaries.items():
        require(96 <= number(header, 'bytes') <= 16384 and digest(header['sha256']) and digest(header['digest']), 'primary shape')
        for field in ('sequence', 'nextRoot', 'nextSong'):
            require(0 < number(header, field) < 2**64, 'counter range')
        require(0 <= number(header, 'revision') < 2**64, 'revision range')
        rr = [r for r in selected('MUTATION_ROOT') if (r['case'], r['phase']) == key]
        ss = [r for r in selected('MUTATION_SONG') if (r['case'], r['phase']) == key]
        require(len(rr) == number(header, 'roots') <= 32 and len(ss) == number(header, 'songs') <= 8, 'projection row count')
        for records, high in ((rr, 'nextRoot'), (ss, 'nextSong')):
            ids = [number(r, 'id') for r in records]
            require(ids == sorted(set(ids)) and all(0 < i < number(header, high) for i in ids), 'ID/order/high-water')
        for r in rr:
            require(0 < number(r, 'generation') < 2**64 and r['mode'] == '0' and r['encoding'] in ('0', '1'), 'root policy')
            require(r['hint_present'] in ('0', '1') and 0 <= number(r, 'hint_bytes') <= 4096, 'hint bounds')
            require(r['hint_present'] == '1' or r['hint_bytes'] == '0', 'absent hint bytes')
            require(r['track'] == 'none' or 0 <= number(r, 'track') < 2**64, 'track')
        for s in ss:
            require(number(s, 'root') in [number(r, 'id') for r in rr], 'song/root association')
            for field in ('locator_hex', 'title_hex', 'artist_hex'):
                if field != 'locator_hex' and s[field] == 'absent':
                    continue
                require(re.fullmatch(r'(?:[0-9a-f]{2}){1,4096}', s[field]), 'text representation')
                bytes.fromhex(s[field]).decode('utf-8')
        roots[key] = [{k: v for k, v in r.items() if k not in ('case', 'phase')} for r in rr]
        songs[key] = [{k: v for k, v in r.items() if k not in ('case', 'phase')} for r in ss]
    require(all((r['case'], r['phase']) in primaries for k, r in rows if k in ('MUTATION_ROOT', 'MUTATION_SONG')), 'unframed projection')

    def preserve(tag):
        a, b = primaries[tag, 'before'], primaries[tag, 'after']
        require(all(a[k] == b[k] for k in a if k not in ('case', 'phase')), f'primary preservation: {tag}')
        require(roots[tag, 'before'] == roots[tag, 'after'] and songs[tag, 'before'] == songs[tag, 'after'], 'row preservation')

    def advanced(tag):
        a, b = primaries[tag, 'before'], primaries[tag, 'after']
        require(number(b, 'revision') == number(a, 'revision') + 1 and number(b, 'sequence') == number(a, 'sequence') + 1, f'commit advancement: {tag}')
        require(a['sha256'] != b['sha256'] and a['digest'] != b['digest'], 'unchanged commit digest')
        return a, b

    acknowledgments = indexed('MUTATION_ACK')
    require(set(acknowledgments) == set(cases), 'ack matrix incomplete')
    for (tag,), r in cases.items():
        op, scenario = tag.split('-', 1)
        require(r['operation'] == op and r['scenario'] == scenario, 'case/operation association')
        saved = scenario in ('saved', 'late-cancel')
        require(r['succeeded'] == r['staged_snapshot'] == str(int(saved)), 'staged result leakage')
        require(r['memory_preserved'] == str(int(not saved)) and r['primary_preserved'] == str(int(scenario in ('write-failure', 'late-fence'))), 'memory/primary outcome')
        require(r['pending_cleared'] == r['root_mapping'] == '1', 'pending/mapping failure')
        require(r['guards_shared'] == str(int(not (saved and op == 'reattach'))), 'original guard sharing')
        renamed = scenario in ('saved', 'uncertain', 'late-cancel')
        require(r['rename_observed'] == r['memory_at_rename'] == str(int(renamed)), 'memory publication ordering')
        require(r['late_cancelled'] == str(int(scenario == 'late-cancel')), 'late cancellation')
        require(r['state'] == ('RecoveryRequired' if scenario in ('late-fence', 'uncertain') else 'Ready'), 'service state')
        expected_errors = {'saved': (-1, -1, -1), 'late-cancel': (-1, -1, -1), 'write-failure': (5, 13, -1), 'late-fence': (9, 14, 5), 'uncertain': (5, 16, -1)}
        require(tuple(number(r, k) for k in ('error', 'store_error', 'binding_error')) == expected_errors[scenario], 'typed error family')
        ack = acknowledgments[tag,]
        require(ack['present'] == ack['token_matches_primary'] == str(int(saved)), 'ack outcome')
        for f in ('sequence', 'revision'):
            require(ack[f] == (primaries[tag, 'after'][f] if saved else '0'), 'ack primary association')
        if not renamed:
            preserve(tag)
            continue
        a, b = advanced(tag)
        ra, rb = roots[tag, 'before'], roots[tag, 'after']
        sa, sb = songs[tag, 'before'], songs[tag, 'after']
        if op == 'register':
            require(number(b, 'nextRoot') == number(a, 'nextRoot') + 1 and b['nextSong'] == a['nextSong'] and rb[:-1] == ra and sa == sb, 'registration projection')
            require(rb[-1]['id'] == a['nextRoot'] and rb[-1]['generation'] == '1', 'registered allocator association')
        elif op == 'reattach':
            require(a['nextRoot'] == b['nextRoot'] and a['nextSong'] == b['nextSong'] and sa == sb and len(ra) == len(rb) == 1, 'reattach shape')
            require(ra[0]['id'] == rb[0]['id'] and number(rb[0], 'generation') == number(ra[0], 'generation') + 1, 'generation advance')
        else:
            require(ra == rb and a['nextRoot'] == b['nextRoot'] and a['nextSong'] == b['nextSong'], 'unchanged root/counter association')
            if op == 'scan':
                require(sa == sb, 'revision-only scan rows')
            elif op == 'remove':
                require(len(sa) == 1 and not sb, 'removed row')
            else:
                require(len(sa) == len(sb) == 1 and sa[0]['locator_hex'] != sb[0]['locator_hex'], 'relocation key')
                require(all(sa[0][k] == sb[0][k] for k in ('id', 'root', 'title_hex', 'artist_hex')), 'relocation ownership')
    recoveries = indexed('MUTATION_RECOVERY')
    require({k[0] for k in recoveries} == {f'{op}-uncertain' for op in OPS}, 'recovery matrix')
    for (tag,), r in recoveries.items():
        require(r['fresh_owner'] == r['projection_matches'] == r['unattached'] == '1' and r['revision'] == primaries[tag, 'after']['revision'], 'fresh recovery')
    for tag in ('same-path-nochange', 'same-path-drift', 'unrelated-drift', 'store-containment', 'descriptor-peak'):
        preserve(tag)
    for tag in ('targeted-repair', 'saved-hint-attachment'):
        advanced(tag)
        require(number(roots[tag, 'after'][0], 'generation') == number(roots[tag, 'before'][0], 'generation') + 1, 'attachment generation')
    advanced('equal-key-relocate')
    require(songs['equal-key-relocate', 'before'] == songs['equal-key-relocate', 'after'], 'equal-key rows changed')
    before, removed, after = (primaries['remove-rediscovery', phase] for phase in ('before', 'removed', 'after'))
    require(not songs['remove-rediscovery', 'removed'] and before['nextSong'] == removed['nextSong'] and number(after, 'nextSong') == number(before, 'nextSong') + 1, 'remove/rediscover high-water')
    require(songs['remove-rediscovery', 'after'][0]['id'] == before['nextSong'] and songs['remove-rediscovery', 'after'][0]['title_hex'] == 'absent', 'removed override resurrection')
    for f in ('revision', 'sequence'):
        require(number(removed, f) == number(before, f) + 1 and number(after, f) == number(removed, f) + 1, 'lifecycle ordering')
    for r in special.values():
        for k, v in r.items():
            if k not in ('case', 'binding_error', 'state', 'active_roots', 'prospective_peak', 'descriptor_limit', 'fd_before', 'fd_after'):
                require(v == '1', f'special outcome: {k}')
    for tag in ('same-path-drift', 'unrelated-drift', 'store-containment'):
        r = special[tag,]
        require(r['state'] == ('Ready' if tag == 'store-containment' else 'RecoveryRequired'), 'special service state')
        require(r['binding_error'] == ('4' if tag == 'store-containment' else '5'), 'special binding error')
    peak = special['descriptor-peak',]
    require(peak['active_roots'] == '32' and peak['prospective_peak'] == '46' and peak['descriptor_limit'] == '45' and peak['fd_before'] == peak['fd_after'], 'descriptor peak')
    for kind, record_kind in (('MUTATION_ADMISSION_TRACE', 'MUTATION_ADMISSION_RECORD'), ('MUTATION_IO_TRACE', 'MUTATION_IO')):
        headers = indexed(kind)
        expected_tags = CASES | (set(SPECIALS) - {'equal-key-relocate', 'remove-rediscovery'}) if kind.endswith('ADMISSION_TRACE') else CASES
        require({k[0] for k in headers} == expected_tags, 'trace matrix incomplete')
        for (tag,), h in headers.items():
            rr = [r for r in selected(record_kind) if r['case'] == tag]
            require(h['overflow'] == '0' and len(rr) == number(h, 'count') <= 256, 'trace overflow/count')
            for r in rr:
                require(r['injected'] in ('0', '1'), 'injected flag')
                if record_kind.endswith('_IO'):
                    require(number(r, 'rc') < 0 or r['errno'] == '0', 'errno on successful call')
                elif r['call'] == 'identity-mount':
                    require(number(r, 'ino') > 0 and number(r, 'mount') > 0, 'identity/mount')
        require(all((r['case'],) in headers for r in selected(record_kind)), 'unframed trace')
    for op in OPS:
        for scenario in ('write-failure', 'uncertain'):
            require(any(r['case'] == f'{op}-{scenario}' and r['injected'] == '1' for r in selected('MUTATION_IO')), 'missing injection receipt')
        require(any(r['case'] == f'{op}-saved' and r['call'] == 'renameat' and r['rc'] == '0' and r['injected'] == '0' for r in selected('MUTATION_IO')), 'missing native publication')
    artifacts = selected('MUTATION_ARTIFACT')
    for tag in CASES:
        require(sum(r['case'] == tag for r in artifacts) == (1 if tag.endswith('write-failure') else 2), 'artifact matrix incomplete')
    for r in artifacts:
        require(r['case'] in CASES and number(r, 'ino') > 0 and r['links'] == '1', 'artifact identity')
        if number(r, 'bytes') <= 256:
            require(number(r, 'captured') == number(r, 'bytes') and r['capture_errno'] == '0' and digest(r['prefix_sha256']), 'artifact capture')
        else:
            require(r['captured'] == '-1' and r['capture_errno'] == '27' and r['prefix_sha256'] == 'none', 'oversize artifact must report unavailable')
    allocations = indexed('MUTATION_ALLOCATION', ('operation',))
    require({k[0] for k in allocations} == set(OPS), 'allocation matrix')
    for (op,), r in allocations.items():
        require(number(r, 'failures') > 0 and r['success'] == r['rollback_preserved'] == r['pending_cleared'] == '1' and r['fd_before'] == r['rollback_fd_after'], 'allocation rollback')
        require(number(r, 'fd_after') == number(r, 'fd_before') + (op == 'register'), 'owned success descriptor delta')
        require(r['scope'] == ('store-staging' if op == 'scan' else 'entry'), 'scan injection scope')
    summary = selected('MUTATION_SUMMARY')
    require(len(summary) == 1 and summary[0]['cases'] == str(len(ALL)) and summary[0]['native_acceptance'] == 'Pending' and summary[0]['paths_emitted'] == '0' and summary[0]['namespace_alias'] == 'Skipped', 'summary scope')
    return {'status': 'verified', 'mutation_cases': len(ALL), 'operations': len(OPS), 'primary_receipts': len(primaries),
            'allocation_sweeps': len(allocations), 'artifact_receipts': len(artifacts),
            'admission_traces': len(indexed('MUTATION_ADMISSION_TRACE')), 'io_traces': len(indexed('MUTATION_IO_TRACE')),
            'native_acceptance': 'Pending', 'scope': 'selected observations; paths and raw hint-bearing wires omitted; no independent byte/provenance proof'}


if __name__ == '__main__':
    try:
        require(len(sys.argv) == 2, 'usage: verifier NATIVE_SERVICE_RUN_LOG')
        path = Path(sys.argv[1])
        require(path.stat().st_size <= 24 * 1048576, 'log file limit')
        print(json.dumps(verify_text(path.read_text(encoding='utf-8')), sort_keys=True))
    except (ValueError, KeyError, OSError, UnicodeError) as error:
        print(f'mutation receipt verification failed: {error}', file=sys.stderr)
        sys.exit(1)
