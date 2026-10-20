#!/usr/bin/env python3
"""Closed shipping live-load CMOS save and its negative control. GPL-2.0-only."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

from rpc_ninja_commands import rebuild_commands, same_path

build, exe, core, fixtures = (Path(p).resolve() for p in sys.argv[1:5])
root = Path(__file__).resolve().parents[1]


def check(library):
    with tempfile.TemporaryDirectory(prefix='rpc-closed-replace-') as directory:
        case = Path(directory)
        system = case / 'system'
        firmware = system / 'arcdroid/firmware'
        firmware.mkdir(parents=True)
        shutil.copyfile(fixtures / 'arcdroid-test.rom', firmware / 'authored.rom')
        first = case / 'first.adf'
        image = (fixtures / 'arcdroid-test.adf').read_bytes()
        first.write_bytes(image)
        # Distinct content keys prevent the second session's unload/deinit
        # from masking a lost save of the first session.
        replacement = bytearray(image)
        replacement[-1] ^= 1
        (system / 'replacement.adf').write_bytes(replacement)
        save = case / 'save'
        save.mkdir()
        result = subprocess.run([str(exe), str(library), str(first), str(system),
                                 str(save), 'shipping-replace'], capture_output=True,
                                text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        assert 'live session replaced before its first frame' in result.stdout
        # Independent ARCCMOS2 wire oracle: test OS=1, A3000=4 MiB,
        # machine-name polynomial tag, CRC32 of the ASCII content SHA1.
        content = hashlib.sha1(image).hexdigest()
        tag = 0
        for byte in b'A3000':
            tag = (tag * 33 + byte) & 255
        payload = bytearray(240)
        payload[199 - 16] = 1  # one floppy, no IDE/ST506
        payload[63 - 16] = 2   # sum of other CMOS bytes + 1
        expected = b'ARCCMOS2' + bytes([2, 1, 4, tag])
        expected += struct.pack('<I', zlib.crc32(content.encode())) + payload
        records = list((save / 'arcdroid/cmos').glob(f'A3000.*.{content}.bin'))
        assert len(records) == 1, 'outgoing CMOS record missing after replacing load'
        assert records[0].read_bytes() == expected, 'outgoing CMOS bytes differ'
        assert len(list((save / 'arcdroid/cmos').glob('*.bin'))) == 2


check(core)
print('closed shipping replacement: exact 256-byte outgoing CMOS record passed', flush=True)
if '--mutant' in sys.argv[5:]:
    cache = (build / 'CMakeCache.txt').read_text()
    ninja = next(line.split('=', 1)[1] for line in cache.splitlines()
                 if line.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
    commands = [row['command'] for row in json.loads(subprocess.check_output(
        [ninja, '-t', 'compdb', '-x'], cwd=build, text=True))]
    source = root / 'libretro/arcdroid_libretro.c'
    original = source.read_text()
    start = original.index('static bool arc_load_game(')
    anchor = '   if (arc_loaded) arc_persist_cmos();'
    offset = original.index(anchor, start)
    cc, link = rebuild_commands(commands, source, 'arcdroid_libretro', build, os.name == 'nt')
    assert not any('ARCDROID_TEST_' in arg for arg in cc)
    assert '-DARC_RPC_ORDINARY_LOAD=0' in cc
    with tempfile.TemporaryDirectory(prefix='rpc-closed-replace-mutant-') as directory:
        temporary = Path(directory)
        mutated = temporary / source.name
        mutated.write_text(original[:offset] + original[offset:].replace(anchor, '   (void)0;', 1))
        obj = temporary / 'frontend.o'
        library = temporary / core.name
        previous = cc[cc.index('-o') + 1]
        cc[cc.index('-o') + 1] = str(obj)
        cc[cc.index('-c') + 1] = str(mutated)
        if '-MF' in cc:
            cc[cc.index('-MF') + 1] = str(temporary / 'frontend.d')
        link = [str(obj) if same_path(arg, previous, build, os.name == 'nt') else arg
                for arg in link]
        link[link.index('-o') + 1] = str(library)
        link = ['-Wl,--out-implib,' + str(temporary / 'mutant.dll.a')
                if arg.startswith('-Wl,--out-implib,') else arg for arg in link]
        subprocess.run(cc, cwd=build, check=True, capture_output=True, timeout=90)
        subprocess.run(link, cwd=build, check=True, capture_output=True, timeout=90)
        try:
            check(library)
        except AssertionError as error:
            if str(error) != 'outgoing CMOS record missing after replacing load':
                raise
            print('replacement-save removal mutant killed: outgoing CMOS record missing')
        else:
            raise RuntimeError('replacement-save removal mutant survived')
