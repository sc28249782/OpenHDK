#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One supplied-log positive check and independently changed receipt variants."""
import copy
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('cli_verifier', Path(__file__).with_name('verify-library-cli-receipts.py'))
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


def check(text):
    verifier.verify_text(text)
    original = [json.loads(line) for line in text.splitlines()]
    def case(rows, name):
        return next(r for r in rows if r.get('case') == name)
    tests = {}
    def changed(name, change):
        rows = copy.deepcopy(original); change(rows)
        tests[name] = '\n'.join(json.dumps(r, ensure_ascii=False) for r in rows)
    changed('missing-case', lambda r: r.remove(case(r, 'prepare')))
    changed('duplicate-case', lambda r: r.insert(2, copy.deepcopy(case(r, 'register'))))
    changed('query-writes', lambda r: case(r, 'prepare')['after'].__setitem__('sequence', 999))
    changed('lost-receipt-rollback', lambda r: case(r, 'output-loss').__setitem__('after', case(r, 'output-loss')['before']))
    changed('automatic-retry', lambda r: case(r, 'output-loss').__setitem__('mutation_attempts', 2))
    changed('lost-receipt-exit', lambda r: next(x for x in r if x.get('session') == 'recovery').__setitem__('code', 0))
    changed('auto-attach', lambda r: case(r, 'reopen-unattached')['rows'][1].__setitem__('attached', True))
    changed('generation-reuse', lambda r: case(r, 'recovery-roots')['rows'][1].__setitem__('generation', 2))
    changed('metadata-loss', lambda r: case(r, 'output-prepare')['rows'][0].__setitem__('title', 'Live.Set'))
    changed('content-storage-confusion', lambda r: case(r, 'prepare-unattached')['rows'][0].__setitem__('store_code', 14))
    changed('private-field', lambda r: case(r, 'prepare')['rows'][0].__setitem__('hint', '/private'))
    changed('false-acceptance', lambda r: r[-1].__setitem__('native_acceptance', 'Accepted'))
    changed('filesystem-bypass', lambda r: r[0].__setitem__('bypass', True))
    tests['duplicate-json-key'] = text.replace('"kind": "summary"', '"kind": "summary", "kind": "summary"')
    tests['oversized-log'] = text + ' ' * (2 * 1048576)
    for name, tampered in tests.items():
        if tampered == text:
            raise ValueError('negative fixture unchanged: ' + name)
        try:
            verifier.verify_text(tampered)
        except (ValueError, KeyError, TypeError):
            continue
        raise ValueError('accepted tampered receipt: ' + name)
    return dict(status='passed', positive_cases=1, negative_cases=len(tests), native_acceptance='Pending')


if __name__ == '__main__':
    try:
        if len(sys.argv) != 2:
            raise ValueError('usage: check-library-cli-verifier.py CLI_RUN_LOG')
        path = Path(sys.argv[1])
        if path.stat().st_size > 2 * 1048576:
            raise ValueError('file cap')
        print(json.dumps(check(path.read_text(encoding='utf-8')), sort_keys=True))
    except (ValueError, KeyError, TypeError, OSError) as error:
        print('CLI verifier tests failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
