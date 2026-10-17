#!/usr/bin/env python3
"""Checker mutation smoke tests; no production writer rules exercised. GPL-2.0-only."""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(__file__).with_name('rpc_filecore_check.py').read_text()
suite = pathlib.Path(__file__).with_name('rpc_filecore_check_test.py').read_text()
mutants = {
    'sharing-offset': ('((address & 255) - 1) * 512', '(address & 255) * 512'),
    'zone-order': ('first = 2 if identity == 2 else identity // 254', 'first = 0'),
    'directory-checksum': ('data[2047] == directory_checksum(data, count)', 'True'),
    'free-link': ('free = 8 + free if free else 0', 'free = 0'),
    'boot-consistency': ("map_record == record", 'True'),
    'metadata': ('attr=attr, size=size, sha256=h.hexdigest()', 'attr=0, size=size, sha256=h.hexdigest()'),
    'unshared-alias': ('len({e[3] for e in neighbours}) <= 1 or\n                all(e[6] & 255 for e in neighbours)', 'True'),
    'share-size': ('record[40] == 0', 'True'),
    'big-directory': ('record[41] == 0', 'True'),
    'high-zone-count': ('record[42] == 0', 'True'),
    'format-version': ('integer(record, 44) == 0', 'True'),
    'guest-id': ('map_record[:20] == record[:20] and map_record[22:] == record[22:]', 'True'),
    'guest-sequence': ('data[0] == data[2042]', 'True'),
    'guest-terminator': ('data[5 + count * 26] == 0', 'True'),
    'overlap': ('all(at + count <= lo or hi <= at for lo, hi, *_ in self.used)', 'True'),
}
with tempfile.TemporaryDirectory(prefix='arcdroid-filecore-mutants-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'rpc_filecore_check_test.py').write_text(suite)
    (path / 'rpc_filecore_check.py').write_text(source)
    subprocess.run([sys.executable, '-B', str(path / 'rpc_filecore_check_test.py'),
                    sys.argv[1]], check=True, capture_output=True, text=True)
    for name, (old, new) in mutants.items():
        if source.count(old) != 1:
            raise RuntimeError(f'mutant anchor changed: {name}')
        (path / 'rpc_filecore_check.py').write_text(source.replace(old, new))
        result = subprocess.run([sys.executable, '-B', str(path / 'rpc_filecore_check_test.py'),
                                 sys.argv[1]], capture_output=True, text=True)
        if result.returncode == 0:
            raise RuntimeError(f'checker mutant survived: {name}')
        if 'SyntaxError' in result.stderr or 'ImportError' in result.stderr:
            raise RuntimeError(f'invalid checker mutant: {name}')
        print(f'checker mutant killed: {name}')
