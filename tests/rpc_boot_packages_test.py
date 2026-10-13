#!/usr/bin/env python3
"""Authored ARC0 packages and independent transformed-tree expectations.

GPL-2.0-only. Contains no OS, firmware or real package contents. All names
outside the public bootstrap contract are invented. Private inventories are
never opened by this test.
"""
from pathlib import Path
import hashlib
import json
import re
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from rpc_filecore_check import check

DRIVER = sys.argv[1]
ASSETS = Path(__file__).resolve().parents[1] / 'assets/riscpc-boot'
IDENTITY = '0123456789012345678901234567890123456789'
BASE = {
    'HardDisc4/!Boot/!Boot': (b'authored inactive icon command\r', 0xffffeb00, 2, 3),
    'HardDisc4/!Boot/!Run': (b'authored inactive startup command\r', 0xffffeb00, 3, 3),
    'HardDisc4/!Boot/PreDesktop/task': (b'authored inactive task\r', 0xffffff00, 4, 3),
    'HardDisc4/!Boot/RO350Hook/Boot': (b'authored inactive hook\r', 0xffffff00, 5, 3),
    'HardDisc4/!Boot/Themes/!Sprites': (b'authored sprite placeholder', 0xfffff900, 6, 3),
    'HardDisc4/!Boot/Resources/!System/SysPaths': (b'authored path placeholder\r', 0xffffff00, 7, 3),
    'HardDisc4/!Boot/Resources/!System/!Boot': (b'authored resource placeholder\r', 0xffffeb00, 8, 3),
    'HardDisc4/!Boot/Resources/!System/400/Stay': (b'authored neighbour', 0xffffff00, 9, 3),
    'HardDisc4/!Boot/Resources/!System/500x/Stay': (b'authored near numeric name', 0xffffff00, 10, 3),
    'HardDisc4/!Boot/Resources/!System/500/LongerThan10': (b'authored excluded', 0xffffff00, 11, 3),
    'HardDisc4/!Boot/RO500Hook/LongerThan10': (b'authored excluded hook', 0xffffff00, 12, 3),
    'HardDisc4/Leaf.dot': (b'authored dotted name', 0xffffff00, 13, 3),
    'LICENSE': (b'Authored fixture notice; GPL-2.0-only.', 0xffffff00, 14, 3),
}
SYSTEM = {
    '!System/SysPaths': BASE['HardDisc4/!Boot/Resources/!System/SysPaths'],
    '!System/Extra': (b'authored added resource', 0xffffff00, 15, 3),
    '!SysMerge': (b'authored omitted program', 0xffffff00, 16, 3),
    'ReadMe.txt': (b'Authored fixture readme.', 0xffffff00, 17, 3),
}
# Explicit expectations; no import from transformation or writer code.
EXPECTED = {
    '$.!Boot.Inactive.!Boot': BASE['HardDisc4/!Boot/!Boot'],
    '$.!Boot.Inactive.!Run': BASE['HardDisc4/!Boot/!Run'],
    '$.!Boot.Inactive.PreDesktop.task': BASE['HardDisc4/!Boot/PreDesktop/task'],
    '$.!Boot.Inactive.RO350Hook.Boot': BASE['HardDisc4/!Boot/RO350Hook/Boot'],
    '$.!Boot.Themes.!Sprites': BASE['HardDisc4/!Boot/Themes/!Sprites'],
    '$.!Boot.Resources.!System.SysPaths': BASE['HardDisc4/!Boot/Resources/!System/SysPaths'],
    '$.!Boot.Resources.!System.!Boot': BASE['HardDisc4/!Boot/Resources/!System/!Boot'],
    '$.!Boot.Resources.!System.400.Stay': BASE['HardDisc4/!Boot/Resources/!System/400/Stay'],
    '$.!Boot.Resources.!System.500x.Stay': BASE['HardDisc4/!Boot/Resources/!System/500x/Stay'],
    '$.!Boot.Resources.!System.Extra': SYSTEM['!System/Extra'],
    '$.Leaf/dot': BASE['HardDisc4/Leaf.dot'],
    '$.RelDocs.HardDisc4.LICENSE': BASE['LICENSE'],
    '$.RelDocs.PlingSys.ReadMe/txt': SYSTEM['ReadMe.txt'],
}
for name, dest, kind in [('Multisync', '$.Multisync', 0xfed),
                         ('BootEnv', '$.BootEnv', 0xfff),
                         ('BootRes', '$.BootRes', 0xfff),
                         ('BootTasks', '$.BootTasks', 0xfff),
                         ('Run', '$.!Boot.!Run', 0xfeb),
                         ('Boot', '$.!Boot.!Boot', 0xfeb),
                         ('ArcTasks', '$.!Boot.ArcTasks', 0xfea)]:
    data = (ASSETS / name).read_bytes()
    if name != 'Multisync':
        data = data.replace(b'\n', b'\r')
    EXPECTED[dest] = data, 0xfff00000 | (kind << 8), 0, 3


def require(ok, reason):
    if not ok:
        raise RuntimeError(reason)


def package(path, entries, method=zipfile.ZIP_DEFLATED, metadata=True):
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w', compression=method) as archive:
        rows = entries.items() if isinstance(entries, dict) else entries
        for name, (data, load, execute, attr) in rows:
            info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = method
            info.extra = struct.pack('<HH4sIII', 0x4341, 16, b'ARC0', load, execute, attr) if metadata else b''
            archive.writestr(info, data)


def inputs(root, hard=None, system=None, method=zipfile.ZIP_DEFLATED):
    directory = root / 'system/arcdroid/riscpc/boot'
    package(directory / 'HardDisc4.zip', BASE if hard is None else hard, method)
    package(directory / 'PlingSystem.zip', SYSTEM if system is None else system, method)
    return root / 'system'


def run(system, save, units=1, mode='populate', cut=0, phase=0, success=True):
    save.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([DRIVER, str(system), str(save), str(units), mode, str(cut), str(phase)],
                            capture_output=True, text=True, timeout=180)
    if success:
        require(result.returncode == 0, f'{mode} refused: {result.stderr}')
    else:
        require(result.returncode in (2, 3), f'{mode} did not refuse: {result.stdout} {result.stderr}')
    stats = next((json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')), {})
    return stats, result


def store_tree(save):
    root = save / 'arcdroid/hd-riscpc' / IDENTITY
    manifests = list(root.glob('manifest.?'))
    require(manifests, 'no published root')
    manifest = max((p.read_bytes() for p in manifests), key=lambda b: struct.unpack_from('<Q', b, 88)[0])
    require(len(manifest) == 24452 and struct.unpack_from('<I', manifest, 8)[0] == 2,
            'receipt envelope')
    require(zlib.crc32(manifest[:-4]) == struct.unpack_from('<I', manifest, len(manifest)-4)[0], 'manifest CRC')
    require(manifest[24320:24328] == b'ARCRBOOT', 'receipt magic')
    require(struct.unpack_from('<III', manifest, 24328) == (1, 1, AUTHORED_STEP_REVISION), 'receipt revisions')
    def read(at, count):
        result = bytearray()
        while count:
            index, offset = divmod(at, 262144)
            take = min(count, 262144-offset)
            entry = manifest[128+48*index:176+48*index]
            kind, = struct.unpack_from('<I', entry)
            if kind == 0:
                result.extend(bytes(take))
            else:
                require(kind == 1, 'chunk kind')
                gen, = struct.unpack_from('<Q', entry, 8)
                with (root / f'c{index:03x}.g{gen:016x}').open('rb') as file:
                    file.seek(128+offset)
                    result.extend(file.read(take))
            at += take
            count -= take
        return bytes(result)
    return check(read, boot_option=2), manifest


def compare(save, expected=EXPECTED):
    reader, manifest = store_tree(save)
    actual = {p: v for p, v in reader.tree.items() if not v['directory']}
    reference = {p: dict(directory=False, load=load, exec=execute, attr=attr,
                         size=len(data), sha256=hashlib.sha256(data).hexdigest())
                 for p, (data, load, execute, attr) in expected.items()}
    require(actual == reference, 'transformed tree, metadata or bytes differ')
    directories = {'$'}
    for path in reference:
        parent = path.rsplit('.', 1)[0]
        while parent != '$':
            directories.add(parent)
            parent = parent.rsplit('.', 1)[0]
    require({p for p, v in reader.tree.items() if v['directory']} == directories, 'transformed directories differ')
    require(reader.boot_option == 2, 'boot option is not run')
    require(struct.unpack_from('<QQ', manifest, 24408) == (len(expected), sum(len(v[0]) for v in expected.values())),
            'receipt file/byte counts')
    return reader, manifest


def no_root(save):
    require(not list((save/'arcdroid/hd-riscpc'/IDENTITY).glob('manifest.?')), 'refusal published a root')


# Authored-step revision 1: independent SHA-256 goldens of the source assets.
# The receipt's authored revision is checked against the same revision below.
AUTHORED_STEP_REVISION = 1
ASSET_SHA256_BY_REVISION = {1: {
    'Multisync': '8ab8d572817801276883103a3154250f8691ae9df0636661f7a7e172de573b27',
    'BootEnv': '942d3602e8d43695640e45df293b5d63b9aed45b3530e2d7c0898a133b565add',
    'BootRes': '7bca5af47a9c8d74668a583046b602c2b6a2199fc203193da64233b37942effd',
    'BootTasks': '1d605cb0b8e1f06e4de2495ff4e31a126f9931d41526043d24ea3694a7929504',
    'ArcTasks': '24b50db4a3317d85e21ebe9eb4c1ff92bbae20d4fe9df7588e45481d7ecf1fa1',
    'Run': '3ef1d46087dd9745538d4264266f576fd09d497c0bd33f3ad218e798d859949e',
    'Boot': '2190a1ea1bdae384556f0dba674f34db39588398c32669bb812ddd0468b4380d',
}}


def check_asset_revision(directory=ASSETS, revision=AUTHORED_STEP_REVISION):
    expected = ASSET_SHA256_BY_REVISION.get(revision)
    require(expected is not None, 'authored revision lacks asset goldens')
    actual = {name: hashlib.sha256((directory/name).read_bytes()).hexdigest()
              for name in expected}
    require(actual == expected, 'authored boot asset changed without revision update')


def cases(root):
    check_asset_revision()
    initial = root / 'normal'
    system = inputs(initial)
    outputs = []
    for units in (1, 7, 32):
        save = root / f'steps-{units}'
        stats, _ = run(system, save, units)
        compare(save)
        _, manifest = store_tree(save)
        for i, name in enumerate(('HardDisc4.zip', 'PlingSystem.zip')):
            digest = hashlib.sha256((system/'arcdroid/riscpc/boot'/name).read_bytes()).digest()
            require(manifest[24344+32*i:24376+32*i] == digest, 'receipt package hashes')
        owned = save/'arcdroid/hd-riscpc'/IDENTITY
        outputs.append({p.name:p.read_bytes() for p in owned.iterdir()})
        require(stats['memory'] <= 8388608 and stats['max_bytes'] <= 4194304 and stats['max_ops'] <= 128, 'step bounds')
    require(outputs[0] == outputs[1] == outputs[2], 'step sizes changed output')
    before = outputs[0]
    stats, _ = run(root/'absent-system', root/'steps-1', mode='reopen')
    require(stats['package_opens'] == stats['units'] == 0, 'receipt reopen read packages')
    require(before == {p.name:p.read_bytes() for p in (root/'steps-1/arcdroid/hd-riscpc'/IDENTITY).iterdir()}, 'receipt no-op changed store')
    for mode in ('legacy', 'forge'):
        stats, result = run(root/'absent-system', root/mode, mode=mode)
        require(stats['phase'] == 15 and stats['package_opens'] == 0 and 'left unchanged' in result.stderr, 'legacy/guest-forgery policy')
    # Pure metadata date differences retain the newer stamp; a directory keeps
    # HardDisc4's load/exec/attributes, regardless of PlingSystem's copy.
    hard = dict(BASE); other = dict(SYSTEM); expected = dict(EXPECTED)
    key = 'HardDisc4/!Boot/Resources/!System/SysPaths'
    value = hard[key];hard[key] = value[0], 0xffffff03, 22, 3
    other['!System/SysPaths'] = value[0], 0xffffff02, 999, 3
    hard['HardDisc4/!Boot/Resources/!System/'] = b'', 0xffffff11, 101, 0x13
    other['!System/'] = b'', 0xffffff12, 202, 0x33
    expected['$.!Boot.Resources.!System.SysPaths'] = hard[key]
    directory = root/'dates';system2 = inputs(directory, hard, other)
    run(system2, directory/'save');reader, _ = compare(directory/'save', expected)
    require(reader.tree['$.!Boot.Resources.!System']['load'] == 0xffffff11 and
            reader.tree['$.!Boot.Resources.!System']['attr'] == 0x13, 'directory metadata replaced')
    # ARC0 uses OS_File attributes: lock is bit 3, independently converted to
    # Nick bit 2. Directory bit 3 is unrelated to that source lock bit.
    hard=dict(BASE);expected=dict(EXPECTED)
    hard['HardDisc4/Locked/']=(b'',0xffffff00,0,0x3b)
    item=(b'authored locked file',0xffffff00,0,0x1b)
    hard['HardDisc4/Locked/Leaf']=item;expected['$.Locked.Leaf']=item
    directory=root/'locked';run(inputs(directory,hard),directory/'save')
    reader,_=compare(directory/'save',expected)
    require(reader.tree['$.Locked']['attr']==0x3b,'directory lock metadata lost')
    for badattr in (4,64,128,256):
        hard=dict(BASE);hard['HardDisc4/BadAttr']=(b'x',0xffffff00,0,badattr)
        directory=root/f'bad-attr-{badattr}'
        run(inputs(directory,hard),directory/'save',success=False);no_root(directory/'save')
    hard=dict(BASE);other=dict(SYSTEM);expected=dict(EXPECTED)
    old=hard[key];hard[key]=old[0],0xffffff01,1,3
    other['!System/SysPaths']=old[0],0xffffff02,0,3
    expected['$.!Boot.Resources.!System.SysPaths']=other['!System/SysPaths']
    directory=root/'newer-pling';run(inputs(directory,hard,other),directory/'save');compare(directory/'save',expected)
    # Both stored and deflated ZIP members exercise the streaming reader.
    directory = root/'stored';run(inputs(directory, method=zipfile.ZIP_STORED), directory/'save');compare(directory/'save')
    # Authored pseudo-random bytes force partial inflate-output quanta, both
    # in a shared window and in an object larger than that window.
    rng=random.Random(167);hard=dict(BASE);expected=dict(EXPECTED)
    for name,size in (('Random',131777),('SharedRand',63001)):
        item=(bytes(rng.getrandbits(8) for _ in range(size)),0xffffff00,0,3)
        hard['HardDisc4/'+name]=item;expected['$.'+name]=item
    directory=root/'partial-deflate'
    run(inputs(directory,hard),directory/'save',units=7);compare(directory/'save',expected)
    failures = {}
    for label, changed in [('bytes', (b'X'*len(value[0]), 0xffffff00, 7, 3)),
                           ('type', (value[0], 0xfffffe00, 7, 3)),
                           ('attributes', (value[0], 0xffffff00, 7, 0x13))]:
        other = dict(SYSTEM);other['!System/SysPaths'] = changed;failures[label] = dict(BASE), other
    for label, extra in [('long-name', {'HardDisc4/LongerThan10': (b'x', 0xffffff00, 0, 3)}),
                         ('case', {'HardDisc4/leaf.dot': (b'x', 0xffffff00, 0, 3)}),
                         ('library-name', {'HardDisc4/%': (b'x', 0xffffff00, 0, 3)}),
                         ('traversal', {'HardDisc4/../bad': (b'x', 0xffffff00, 0, 3)}),
                         ('asset-collision', {'HardDisc4/BootEnv': (b'x', 0xffffff00, 0, 3)}),
                         ('depth', {'HardDisc4/'+'/'.join(['d']*32+['f']): (b'x', 0xffffff00, 0, 3)}),
                         ('file-directory', {'HardDisc4/Leaf.dot/child': (b'x', 0xffffff00, 0, 3)})]:
        hard = dict(BASE);hard.update(extra);failures[label] = hard, dict(SYSTEM)
    other = dict(SYSTEM);other['!System/Extra/'] = b'', 0xffffff00, 0, 3
    failures['merge-file-directory'] = dict(BASE), other
    # A legal 251-byte ZIP name becomes a >255-byte transformed path after
    # the !Boot.Resources prefix is added; depth and component lengths fit.
    other=dict(SYSTEM)
    other['!System/'+'/'.join(['ABCDEFGHIJ']*22+['F'])]=(b'x',0xffffff00,0,3)
    failures['transformed-path-cap']=dict(BASE),other
    # The latter needs a HardDisc4 file at the directory's merged destination.
    failures['merge-file-directory'][0]['HardDisc4/!Boot/Resources/!System/Extra'] = b'x', 0xffffff00, 0, 3
    hard = dict(BASE);del hard['HardDisc4/!Boot/!Run'];failures['missing-bootstrap'] = hard, dict(SYSTEM)
    hard = dict(BASE);hard['HardDisc4/!Boot/Resources/!System/SysPaths2'] = hard.pop(key)
    other = dict(SYSTEM);del other['!System/SysPaths'];failures['missing-system-bootstrap'] = hard, other
    for label, (hard, other) in failures.items():
        directory = root/label;sy = inputs(directory, hard, other)
        run(sy, directory/'save', success=False);no_root(directory/'save')
    # Reported VFS failures before FORMAT, for both package handles. Open and
    # size happen in OPEN (0); reads also occur in HASH/INDEX/PREFLIGHT (1/2/3).
    for kind in ('harddisc-package', 'system-package'):
        for operation, phase in (('open-read', 0), ('size', 0), ('read', 1),
                                 ('read', 2), ('read', 3)):
            directory = root/f'vfs-{kind}-{operation}-{phase}'
            sy = inputs(directory);save = directory/'save'
            state,_ = run(sy,save,mode=f'package-fault-{kind}-{operation}',
                          cut=1,phase=phase,success=False)
            require(state['phase']==16 and state['package_faults']==1 and state['points']==0,
                    'pre-FORMAT package failure was not reached / refused')
            no_root(save)
            require(not any(p.is_file() for p in save.rglob('*')),
                    'pre-FORMAT package failure left stray files')
            reopened,_ = run(root/'absent-system',save,mode='verify')
            require(reopened=={'published':0,'receipt':0},
                    'pre-FORMAT package failure published state')
    # Missing packages, missing ARC0 and local/central mismatch refuse before any root.
    for label in ('missing-package', 'no-arc0', 'local-metadata', 'truncated', 'crc', 'corrupt-deflate', 'duplicate'):
        directory = root/label;sy = inputs(directory);path = sy/'arcdroid/riscpc/boot/HardDisc4.zip'
        if label == 'missing-package':path.unlink()
        elif label == 'no-arc0':package(path, BASE, metadata=False)
        elif label == 'duplicate':package(path, list(BASE.items())+[next(iter(BASE.items()))])
        else:
            data = bytearray(path.read_bytes())
            if label == 'local-metadata':
                name, = struct.unpack_from('<H', data, 26);data[30+name+8] ^= 1
            elif label == 'truncated':del data[-25:]
            elif label == 'crc':
                crc, = struct.unpack_from('<I', data, 14);struct.pack_into('<I', data, 14, crc^1)
                at = data.index(b'PK\x01\x02');struct.pack_into('<I', data, at+16, crc^1)
            else:
                names, extras = struct.unpack_from('<HH', data, 26);data[30+names+extras] ^= 255
            path.write_bytes(data)
        run(sy, directory/'save', success=False);no_root(directory/'save')
    # Each configured cap and both boundaries of depth/fanout are executable.
    for label, count, expected_ok in (('fanout-77', 70, True), ('fanout-78', 71, False)):
        hard=dict(BASE);expected=dict(EXPECTED)
        for i in range(count):
            name=f'F{i:03d}';item=(b'x',0xffffff00,0,3)
            hard['HardDisc4/'+name]=item;expected['$.'+name]=item
        directory=root/label;sy=inputs(directory,hard)
        run(sy,directory/'save',success=expected_ok)
        if expected_ok:compare(directory/'save',expected)
        else:no_root(directory/'save')
    directory=root/'depth-32';hard=dict(BASE);name='/'.join(['d']*31+['f']);item=(b'x',0xffffff00,0,3)
    hard['HardDisc4/'+name]=item;expected=dict(EXPECTED);expected['$.'+name.replace('/','.')]=item
    run(inputs(directory,hard),directory/'save');compare(directory/'save',expected)
    directory=root/'input-cap';sy=inputs(directory)
    with (sy/'arcdroid/riscpc/boot/HardDisc4.zip').open('wb') as f:f.truncate(33554433)
    run(sy,directory/'save',success=False);no_root(directory/'save')
    directory=root/'entry-cap';hard=dict(BASE)
    for i in range(4097):hard[f'HardDisc4/F{i:05d}']=(b'',0xffffff00,0,3)
    sy=inputs(directory,hard);run(sy,directory/'save',success=False);no_root(directory/'save')
    directory=root/'member-cap';sy=inputs(directory)
    path=sy/'arcdroid/riscpc/boot/HardDisc4.zip';data=bytearray(path.read_bytes())
    at=data.index(b'PK\x01\x02');struct.pack_into('<I',data,22,16777217);struct.pack_into('<I',data,at+24,16777217);path.write_bytes(data)
    run(sy,directory/'save',success=False);no_root(directory/'save')
    directory=root/'expanded-cap';hard=dict(BASE)
    for i in range(7):hard[f'HardDisc4/L{i}']=(b'z'*16777216,0xffffff00,0,3)
    sy=inputs(directory,hard);run(sy,directory/'save',success=False);no_root(directory/'save')
    directory=root/'node-cap';hard=dict(BASE)
    for i in range(40):
        for j in range(40):hard[f'HardDisc4/A{i:02d}/B{j:02d}/C/D/F']=(b'',0xffffff00,0,3)
    sy=inputs(directory,hard);run(sy,directory/'save',success=False);no_root(directory/'save')
    directory=root/'capacity';hard=dict(BASE)
    for i in range(20):
        for j in range(60):hard[f'HardDisc4/D{i:02d}/E{j:02d}/']=(b'',0xffffff00,0,3)
    sy=inputs(directory,hard);run(sy,directory/'save',success=False);no_root(directory/'save')
    directory=root/'zone-short';hard=dict(BASE);expected=dict(EXPECTED)
    for name,size in [('LargeA',16777216),('LargeB',1505*8192)]:
        item=(b'q'*size,0xffffff00,0,3);hard['HardDisc4/'+name]=item;expected['$.'+name]=item
    run(inputs(directory,hard),directory/'save',units=7);compare(directory/'save',expected)
    # Larger objects, simultaneous sibling pools and a map-zone crossing.
    directory=root/'large';hard=dict(BASE);expected=dict(EXPECTED)
    for i in range(6):
        name=f'Large{i}';item=(bytes([65+i])*8388608,0xffffff00,0,3)
        hard['HardDisc4/'+name]=item;expected['$.'+name]=item
    for i in range(8):
        name=f'Small{i}';item=(bytes([97+i])*64000,0xffffff00,0,3)
        hard['HardDisc4/'+name]=item;expected['$.'+name]=item
    sy=inputs(directory,hard);run(sy,directory/'save',units=7);reader,_=compare(directory/'save',expected)
    require(len({e[0] for e in reader.extents if e[3]!=2})>=2,'large objects did not cross zones')
    # Unknown receipt/populator/authored revisions refuse without modifying files.
    for at in (24328, 24332, 24336):
        directory = root/f'unknown-{at}';shutil.copytree(root/'steps-1', directory)
        owned=directory/'arcdroid/hd-riscpc'/IDENTITY;path=next(owned.glob('manifest.?'));data=bytearray(path.read_bytes())
        struct.pack_into('<I',data,at,99);struct.pack_into('<I',data,len(data)-4,zlib.crc32(data[:-4]));path.write_bytes(data)
        before={p.name:p.read_bytes() for p in owned.iterdir()}
        _,result=run(root/'absent-system',directory,success=False)
        require('created by a newer version of ArcDroid' in result.stderr,'future revision mislabeled as damage')
        require(before=={p.name:p.read_bytes() for p in owned.iterdir()},'unknown revision mutated store')
    for phase in (0, 2, 3, 4, 7, 8, 9, 11, 12, 10, 13):
        directory=root/f'cancel-{phase}';sy=inputs(directory)
        stats,_=run(sy,directory/'save',mode='cancel',phase=phase,success=False)
        require(stats['phase']==17,'cancel result');no_root(directory/'save')
        run(sy,directory/'save');compare(directory/'save')
    directory=root/'changed-restored';sy=inputs(directory,method=zipfile.ZIP_STORED)
    folder=sy/'arcdroid/riscpc/boot';shutil.copyfile(folder/'HardDisc4.zip',folder/'Original.zip')
    alternate=dict(BASE);old=alternate['HardDisc4/!Boot/!Boot'];alternate['HardDisc4/!Boot/!Boot']=(old[0],old[1],99,old[3])
    package(folder/'Alternate.zip',alternate,zipfile.ZIP_STORED)
    run(sy,directory/'save',mode='swap',success=False);no_root(directory/'save')
    for phase in (4, 7, 8, 9, 11, 12):
        directory=root/f'change-{phase}';sy=inputs(directory)
        run(sy,directory/'save',mode='change',phase=phase,success=False);no_root(directory/'save')
    print('rpc-boot-packages: transformations, ARC0, merge/exclusion, corruption, receipts, cancellation and stepping pass')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='arcdroid-boot-packages-') as temp:
        cases(Path(temp))
