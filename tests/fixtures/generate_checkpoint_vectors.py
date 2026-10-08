# SPDX-License-Identifier: GPL-3.0-or-later
# Independent OHK-STORE-030 fixtures: Python struct/hashlib, no C++ encoder.
import hashlib
import pathlib
import struct

def wire(sequence, revision, nr, ns, roots, songs):
    payload = b''.join(roots + songs)
    header = struct.pack('<8sIIQQQQQII', b'OHKCAT\0\0', 1, 0, len(payload), sequence,
                         revision, nr, ns, len(roots), len(songs))
    return (header + payload + hashlib.sha256(header + payload).digest()).hex()

def root(identity, generation, encoding=0, index=None, hint=None):
    h = b'' if hint is None else hint.encode('utf-8')
    return struct.pack('<QQBBBBQI', identity, generation, 0, encoding, index is not None,
                       hint is not None, index or 0, len(h)) + h

def song(identity, r, locator, title='', artist=''):
    a, b, c = [s.encode('utf-8') for s in (locator, title, artist)]
    return struct.pack('<QQB3xIII', identity, r, 0, len(a), len(b), len(c)) + a + b + c

vectors = {
    'empty': wire(1, 0, 1, 1, [], []),
    'rootOnly': wire(1, 1, 2, 1, [root(1, 1)], []),
    'rich': wire(0x0102030405060708, 77, 100, 1000,
                 [root(2, 9, 1, 7, 'E:/เพลง'), root(8, 2**64-1, hint='E:/เพลง')],
                 [song(4, 2, 'Live.Set.kar', ' Title/Artist\t ', 'ศิลปิน'),
                  song(90, 8, 'e\u0301.mid', 'É\r\n')]),
}
output = '// SPDX-License-Identifier: GPL-3.0-or-later\n// Generated independently; regenerate with generate_checkpoint_vectors.py.\n#pragma once\nnamespace CheckpointFixtures {\n'
for name, value in vectors.items():
    output += f'inline constexpr const char* {name} =\n'
    output += '\n'.join('    "' + value[i:i+96] + '"' for i in range(0, len(value), 96)) + ';\n'
output += '}\n'
pathlib.Path(__file__).with_name('checkpoint_vectors.hpp').write_text(output)
