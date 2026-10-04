#!/usr/bin/env python3
"""Private trial FileCore disc builder; GPL-2.0-only, no embedded assets.

New-map/new-directory encoding follows core/install_disc.c and
core/jfd_file_data.c by marked adaptation. Acorn ZIP extra field 0x4341
with ARC0 signature supplies exact load, exec and attributes. Sources stay
in their supplied archives; only the exclusive output image contains bytes.
The supplied !SysMerge must subsequently run in the guest, before freezing.
"""
import argparse
import hashlib
import json
import struct
import zipfile
from pathlib import Path

SIZE = 63 * 16 * 256 * 512
ZONES = 32
MAP_AT = (16 * 4064 - 480) * 1024
MAP_SIZE = ZONES * 512
ROOT_AT = MAP_AT + 2 * MAP_SIZE
ROOT_ID = 0x241


def u32(b, at, value):
    struct.pack_into('<I', b, at, value)


def bits(b, at, length, value):
    for i in range(length):
        if (value >> i) & 1:
            b[(at + i) // 8] |= 1 << ((at + i) % 8)


def zone_check(b):
    s = [0, 0, 0, 0]
    for at in range(508, 0, -4):
        for i in range(4):
            prev = (i - 1) % 4
            s[i] += b[at + i] + (s[prev] >> 8)
            s[prev] &= 255
    s[0] += s[3] >> 8
    for i in range(1, 4):
        s[i] += b[i] + (s[i - 1] >> 8)
    return (s[0] ^ s[1] ^ s[2] ^ s[3]) & 255


def dir_check(b, count):
    def rotate(x):
        return ((x >> 13) | (x << 19)) & 0xffffffff
    c, used = 0, 5 + count * 26
    for at in range(0, used & ~3, 4):
        c = struct.unpack_from('<I', b, at)[0] ^ rotate(c)
    for at in range(used & ~3, used):
        c = b[at] ^ rotate(c)
    for at in range(2008, 2044, 4):
        c = struct.unpack_from('<I', b, at)[0] ^ rotate(c)
    return (c ^ (c >> 8) ^ (c >> 16) ^ (c >> 24)) & 255


def metadata(i):
    at = 0
    while at + 4 <= len(i.extra):
        tag, length = struct.unpack_from('<HH', i.extra, at)
        at += 4
        d = i.extra[at:at + length]
        if len(d) != length:
            raise ValueError('truncated ZIP metadata')
        if tag == 0x4341 and length >= 16 and d[:4] == b'ARC0':
            return struct.unpack_from('<III', d, 4)
        at += length
    if i.is_dir():
        return 0, 0, 3
    # Release-root LICENSE/ReadMe have no RISC OS metadata in some archives.
    if i.filename in ('LICENSE', 'ReadMe.txt'):
        return 0xffffff00, 0, 3
    raise ValueError('missing Acorn metadata: ' + i.filename)


def build(hard, system, output, period_roms=()):
    from period_boot import read_firmware, retain_rom_modules
    inventory = read_firmware(period_roms) if period_roms else None
    period_edits = []
    tree = {'dir': True, 'children': {}, 'meta': (0, 0, 3), 'name': '$'}
    omitted = []

    def add(parts, data, meta, directory):
        parent = tree
        for name in parts[:-1]:
            parent = parent['children'].setdefault(name, {
                'dir': True, 'children': {}, 'meta': (0, 0, 3), 'name': name})
        name = parts[-1]
        node = parent['children'].setdefault(name, {'name': name, 'children': {}})
        node.update(dir=directory, data=data, meta=meta)

    custody = {}
    expected = ('28613a8ee25bf8a207d10409f48b8f7f1af06d7bfcee527a69e2f7d2d15547b5',
                '0259063da9eb3eb85a5ebed9fc37400ab5c66417a886939f3e8c9ddd8dbb9019')
    for source, prefix in ((hard, None), (system, 'SysUpdate')):
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        if digest != expected[0 if prefix is None else 1]:
            raise ValueError('release archive hash refused')
        custody[source.name] = digest
        with zipfile.ZipFile(source) as z:
            for i in z.infolist():
                parts = i.filename.rstrip('/').split('/')
                if prefix is None:
                    if parts[0] == 'HardDisc4':
                        parts = parts[1:]
                        if not parts:
                            continue
                    else:
                        parts = ['RelDocs', 'HD4' + parts[0].split('.')[0]]
                else:
                    parts = [prefix] + [('ReadMe' if n == 'ReadMe.txt' else n) for n in parts]
                if any(len(n.encode('latin1')) > 10 for n in parts):
                    # Three RO5-only names exceed pre-3.8 new-directory limits.
                    # Fail closed for any unsupported name outside RO5 resources.
                    if not ('/RO5' in i.filename or '/!System/500/' in i.filename):
                        raise ValueError('unsupported legacy name: ' + i.filename)
                    omitted.append(i.filename)
                    continue
                if any(n in ('', '.', '..') for n in parts):
                    raise ValueError('invalid FileCore component: ' + i.filename)
                # Unix ZIP dots within a component represent RISC OS slash characters.
                parts = [n.replace('.', '/') for n in parts]
                data = b'' if i.is_dir() else z.read(i)
                if inventory is not None and not i.is_dir():
                    data, edits = retain_rom_modules(data, inventory, source.name + ':' + i.filename)
                    period_edits.extend(edits)
                add(parts, data, metadata(i), i.is_dir())

    image = bytearray(SIZE)
    record = bytearray(60)
    record[:6] = bytes([9, 63, 16, 0, 15, 10])
    record[9:11] = bytes([32, 32])
    u32(record, 12, ROOT_ID)
    u32(record, 16, SIZE)
    record[20:32] = b'DABootTrial0'
    image[0xdc0:0xdfc] = record
    u32(image, 0xc00, 0x20000000)
    # Root boot option deliberately 0. The guest sets Opt 4,2/Configure Boot.
    alloc = [[] for _ in range(ZONES)]
    starts = [512] + [32] * (ZONES - 1)
    ends = [4096] * (ZONES - 1) + [32 + SIZE // 1024 - ((ZONES - 1) * 4064 - 480)]
    alloc[0].append((512, 528, 2))
    starts[0] = 528
    alloc[16].append((32, 80, 2))
    starts[16] = 80
    serial = [3] * ZONES
    nodes = []

    def place(node, root=False):
        if node['dir'] and len(node['children']) > 77:
            raise ValueError('too many legacy directory entries')
        length = 2048 if node['dir'] else len(node['data'])
        if root:
            node.update(address=ROOT_ID, physical=ROOT_AT, length=length)
        elif length == 0:
            node.update(address=0, physical=0, length=0)
        else:
            units = max(16, (length + 1023) // 1024)
            zone = next((j for j in range(ZONES) if starts[j] + units <= ends[j]), None)
            if zone is None:
                raise ValueError('bounded image full / object exceeds zone')
            begin = starts[zone]
            if ends[zone] - begin - units < 16:
                units = ends[zone] - begin
            frag = zone * 254 + serial[zone]
            serial[zone] += 1
            alloc[zone].append((begin, begin + units, frag))
            starts[zone] += units
            physical = ((zone * 4064 - 480) + begin - 32) * 1024
            node.update(address=frag << 8, physical=physical, length=length)
        nodes.append(node)
        if node['dir']:
            for child in sorted(node['children'].values(), key=lambda n: n['name'].upper()):
                child['parent'] = node['address']
                place(child)

    place(tree, True)
    for node in nodes:
        if not node['dir']:
            image[node['physical']:node['physical'] + node['length']] = node['data']
            continue
        d = bytearray(2048)
        d[:5] = b'\x01Nick'
        children = sorted(node['children'].values(), key=lambda n: n['name'].upper())
        for k, child in enumerate(children):
            at = 5 + k * 26
            name = child['name'].encode('latin1').ljust(10, b'\r')
            d[at:at + 10] = name
            load, execute, attr = child['meta']
            struct.pack_into('<III', d, at + 10, load, execute, child['length'])
            d[at + 22:at + 25] = child['address'].to_bytes(3, 'little')
            d[at + 25] = (attr & 0xf7) | (8 if child['dir'] else 0)
        d[2010:2013] = node.get('parent', ROOT_ID).to_bytes(3, 'little')
        d[2013:2032] = node['name'].encode('latin1').ljust(19, b'\r')
        d[2032:2042] = node['name'].encode('latin1').ljust(10, b'\r')
        d[2042:2047] = b'\x01Nick'
        d[2047] = dir_check(d, len(children))
        image[node['physical']:node['physical'] + 2048] = d

    maps = bytearray(MAP_SIZE)
    for j in range(ZONES):
        b = bytearray(512)
        if j == 0:
            b[4:64] = record
        b[3] = 255 if j == 0 else 0
        bits(b, 23, 1, 1)  # Free-link header terminator, even with no free extents.
        for begin, end, frag in alloc[j]:
            bits(b, begin, 15, frag)
            bits(b, end - 1, 1, 1)
        if starts[j] < ends[j]:
            bits(b, 8, 15, starts[j] - 8)
            bits(b, 23, 1, 1)
            bits(b, ends[j] - 1, 1, 1)
        if ends[j] < 4096:
            bits(b, ends[j], 15, 1)
            bits(b, 4095, 1, 1)
        b[0] = zone_check(b)
        maps[j * 512:(j + 1) * 512] = b
    image[MAP_AT:MAP_AT + MAP_SIZE] = maps
    image[MAP_AT + MAP_SIZE:MAP_AT + 2 * MAP_SIZE] = maps
    checksum = sum(image[0xc00:0xdff])
    while checksum > 255:
        checksum = (checksum & 255) + (checksum >> 8)
    image[0xdff] = checksum
    with output.open('xb') as f:
        f.write(image)
    custody.update(size=SIZE, objects=len(nodes), omitted_ro5_names=omitted,
                   selection='full HardDisc4 tree plus !System update',
                   sha1=hashlib.sha1(image).hexdigest(), sha256=hashlib.sha256(image).hexdigest(),
                   merge='NOT YET RUN', boot_option=0)
    if inventory is not None:
        custody.update(period_policy='R6-1 presence guards; explicit dependency errors unchanged',
                       firmware_modules=inventory, period_edits=period_edits)
    output.with_suffix('.json').write_text(json.dumps(custody, indent=2) + '\n')
    print(json.dumps({k: custody[k] for k in ('size', 'objects', 'sha1', 'merge')}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--harddisc4', type=Path, required=True)
    parser.add_argument('--system', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--period-rom', type=Path, action='append', default=[],
                        help='R6-1: repeat for all four approved combined ROMs')
    args = parser.parse_args()
    build(args.harddisc4, args.system, args.out, args.period_rom)
