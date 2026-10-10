#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Positive supplied-log check plus bounded verifier tampering cases."""
import importlib.util
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('mutation_verifier', Path(__file__).with_name('verify-native-mutation-receipts.py'))
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


def check(text):
    verifier.verify_text(text)
    tests = {
        'overflow': '\n'.join(line.replace('overflow=0', 'overflow=1') if line.startswith('MUTATION_IO_TRACE case=register-saved ') else line for line in text.splitlines()),
        'missing-case': '\n'.join(line for line in text.splitlines() if not line.startswith('MUTATION_CASE case=scan-saved ')),
        'counter': '\n'.join(line.replace('nextSong=2', 'nextSong=3') if line.startswith('MUTATION_PRIMARY case=scan-saved phase=after ') else line for line in text.splitlines()),
        'publication-order': '\n'.join(line.replace('memory_at_rename=1', 'memory_at_rename=0') if line.startswith('MUTATION_CASE case=remove-saved ') else line for line in text.splitlines()),
        'guard-refresh': '\n'.join(line.replace('guards_shared=1', 'guards_shared=0') if line.startswith('MUTATION_CASE case=scan-saved ') else line for line in text.splitlines()),
        'late-cancel': '\n'.join(line.replace('late_cancelled=1', 'late_cancelled=0') if line.startswith('MUTATION_CASE case=register-late-cancel ') else line for line in text.splitlines()),
        'duplicate': text + '\n' + next(line for line in text.splitlines() if line.startswith('MUTATION_CASE ')),
        'staged-leak': '\n'.join(line.replace('staged_snapshot=0', 'staged_snapshot=1') if line.startswith('MUTATION_CASE case=relocate-write-failure ') else line for line in text.splitlines()),
        'false-acceptance': text.replace('native_acceptance=Pending paths_emitted=0', 'native_acceptance=Accepted paths_emitted=0'),
        'missing-injection': '\n'.join(line.replace('injected=1', 'injected=0') if line.startswith('MUTATION_IO case=reattach-write-failure ') else line for line in text.splitlines()),
    }
    for name, tampered in tests.items():
        if tampered == text:
            raise ValueError(f'negative fixture did not alter input: {name}')
        try:
            verifier.verify_text(tampered)
        except (ValueError, KeyError, UnicodeError):
            continue
        raise ValueError(f'verifier accepted tampered receipts: {name}')
    return {'status': 'passed', 'positive_cases': 1, 'negative_cases': len(tests), 'native_acceptance': 'Pending'}


if __name__ == '__main__':
    try:
        if len(sys.argv) != 2:
            raise ValueError('usage: check-native-mutation-verifier.py NATIVE_SERVICE_RUN_LOG')
        path = Path(sys.argv[1])
        if path.stat().st_size > 24 * 1048576:
            raise ValueError('log file limit')
        print(json.dumps(check(path.read_text(encoding='utf-8')), sort_keys=True))
    except (ValueError, KeyError, OSError, UnicodeError) as error:
        print(f'mutation verifier tests failed: {error}', file=sys.stderr)
        sys.exit(1)
