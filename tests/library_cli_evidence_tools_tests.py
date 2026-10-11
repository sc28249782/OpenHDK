#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline collector boundaries; no native eligibility/acceptance is claimed."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True

def load(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module

runner = load('cli_runner', 'run-library-cli-evidence.py')
verifier = load('cli_verifier', 'verify-library-cli-receipts.py')


class EvidenceTools(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.parent = Path(self.temp.name)

    def tearDown(self):
        for child in runner.CHILDREN:
            if child.poll() is None:
                child.kill(); child.wait(timeout=3)
            for stream in (child.stdin, child.stdout):
                if stream and not stream.closed:
                    stream.close()
        runner.CHILDREN.clear()
        self.temp.cleanup()

    def wire(self):
        header = struct.pack('<8sIIQQQQQII', b'OHKCAT\0\0', 1, 0, 0, 1, 0, 1, 1, 0, 0)
        return header + hashlib.sha256(header).digest()

    def test_independent_empty_header(self):
        path = self.parent / 'catalog'; path.write_bytes(self.wire())
        p = runner.primary(path)
        self.assertEqual((p['size'], p['sequence'], p['revision'], p['roots'], p['songs']), (96, 1, 0, 0, 0))
        self.assertEqual(p['digest'], hashlib.sha256(self.wire()[:64]).hexdigest())
        self.assertIsNone(runner.primary(self.parent / 'absent'))

    def test_digest_and_length_rejected(self):
        path = self.parent / 'catalog'
        for wire in (self.wire()[:-1] + b'x', self.wire() + b'x', self.wire()[:7]):
            path.write_bytes(wire)
            with self.assertRaises(ValueError): runner.primary(path)

    def executable(self, body):
        path = self.parent / 'stub'
        path.write_text('#!' + sys.executable + '\n' + body)
        path.chmod(0o700)
        return path

    def test_response_batches_and_exit(self):
        stub = self.executable('''import json,sys
print(json.dumps({'type':'session'}),flush=True)
for line in sys.stdin:
 if line.strip()=='quit':
  print(json.dumps({'type':'quit'}),flush=True);break
 print(json.dumps({'type':'page','count':2}),flush=True)
 print(json.dumps({'type':'song','id':1}),flush=True)
 print(json.dumps({'type':'song','id':2}),flush=True)
''')
        s = runner.Session(stub, self.parent, 'session', 'stub')
        self.assertEqual([r.get('id') for r in s.command('songs')], [None, 1, 2])
        with contextlib.redirect_stdout(io.StringIO()) as out: s.end(0)
        self.assertEqual(json.loads(out.getvalue())['code'], 0)

    def test_output_line_limit(self):
        stub = self.executable("print('x'*70000,flush=True)\n")
        with self.assertRaises(ValueError): runner.Session(stub, self.parent, 'session', 'stub')

    def test_batch_count_limit(self):
        stub = self.executable("import json,sys\nprint('{}',flush=True)\nfor line in sys.stdin: print(json.dumps({'type':'page','count':101}),flush=True)\n")
        s = runner.Session(stub, self.parent, 'session', 'stub')
        with self.assertRaises(ValueError): s.command('songs')

    def test_eof_and_non_utf8(self):
        for body in ("pass\n", "import sys\nsys.stdout.buffer.write(b'\\xff\\n');sys.stdout.flush()\n"):
            stub = self.executable(body)
            with self.assertRaises((ValueError, UnicodeError)):
                runner.Session(stub, self.parent, 'session', 'stub')

    def test_ineligible_parent_has_no_fixture_or_child(self):
        binary = self.executable('pass\n')
        before = set(self.parent.iterdir())
        result = subprocess.CompletedProcess([], 0, b'overlay\n', b'')
        with patch.object(runner.subprocess, 'run', return_value=result), contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(runner.run(binary, self.parent), 69)
        self.assertEqual(set(self.parent.iterdir()), before)
        self.assertEqual(json.loads(out.getvalue())['status'], 'Skipped')
        self.assertFalse(runner.CHILDREN)

    def test_bounded_and_malformed_verifier_input(self):
        for text in (' ' * (2*1048576 + 1), '{}\n' * 129, '{"kind":"case","kind":"case"}', '[]', '{"kind":"failure"}'):
            with self.assertRaises(ValueError): verifier.verify_text(text)


if __name__ == '__main__':
    unittest.main()
