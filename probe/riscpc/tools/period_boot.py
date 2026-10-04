#!/usr/bin/env python3
"""R6-1 private boot construction policy; GPL-2.0-only, no embedded assets.

Read only the four explicitly supplied, hash-approved combined ROMs. Module
header title/help offsets provide the ROM inventory. Apply the same union
of those module names to both release archives before the guest System merge.
Presence-only RMEnsure guards retain ROM modules; original minimum versions,
additional-module loads and explicit dependency errors stay intact.
"""
import hashlib
import re
import struct

SHA1S = {
    'c31318e712ee7de92e46f9df6c791b614d4c9304': '350',
    '3cf75aa4d4dc8fe57110a124fbd1560d46c549b1': '360',
    '71cbc9f12debaa7aa2aeb91d38682e6c5c4a34a5': '370',
    'c5fe0645e48894fb4b245abeefdc9a65d659af59': '371',
}
LOAD = re.compile(
    r'\bRMEnsure\s+([A-Za-z][A-Za-z0-9_]*)\s+([0-9]+\.[0-9]+)'
    r'\s+(?=(?:X\s+)?(?:RMLoad|RMRun|Run)\s)', re.I)


def module_inventory(data):
    modules = {}
    for header in range(0, len(data) - 52, 4):
        title, help_at = struct.unpack_from('<II', data, header + 16)
        if not (32 <= title < 0x100000 and 32 <= help_at < 0x100000):
            continue
        t, h = header + title, header + help_at
        if max(t, h) >= len(data):
            continue
        te = data.find(b'\0', t, min(t + 32, len(data)))
        he = data.find(b'\0', h, min(h + 256, len(data)))
        if te < 0 or he < 0:
            continue
        name, description = data[t:te], data[h:he]
        if not re.fullmatch(rb'[A-Za-z][A-Za-z0-9_]{1,30}', name):
            continue
        version = re.search(rb'\t+([0-9]+\.[0-9]+)', description)
        if not version or any(c < 32 and c != 9 for c in description):
            continue
        name = name.decode('ascii')
        item = dict(header=header, version=version[1].decode('ascii'),
                    description=description.decode('latin1'))
        if name in modules and modules[name] != item:
            raise ValueError('ambiguous ROM module header: ' + name)
        modules[name] = item
    if 'SharedCLibrary' not in modules or 'WindowManager' not in modules:
        raise ValueError('incomplete ROM module inventory')
    return modules


def read_firmware(paths):
    result = {}
    for path in paths:
        data = path.read_bytes()
        sha1 = hashlib.sha1(data).hexdigest()
        if sha1 not in SHA1S or SHA1S[sha1] in result:
            raise ValueError('period policy firmware hash/duplicate refused')
        result[SHA1S[sha1]] = dict(sha1=sha1, modules=module_inventory(data))
    if set(result) != set(SHA1S.values()):
        raise ValueError('period policy requires all four approved firmware images')
    return result


def retain_rom_modules(data, inventory, filename):
    # Obey scripts are textual. Relocatable modules and executables stay exact.
    if b'\0' in data:
        return data, []
    names = {name.lower() for rom in inventory.values() for name in rom['modules']}
    edits, output = [], []
    for number, line in enumerate(data.decode('latin1').splitlines(keepends=True), 1):
        if line.lstrip().startswith('|'):
            output.append(line)
            continue
        matches = [m for m in LOAD.finditer(line) if m[1].lower() in names]
        if not matches:
            output.append(line)
            continue
        ending = '\r\n' if line.endswith('\r\n') else '\r' if line.endswith('\r') else '\n'
        output.append('| ARCDROID-PROBE R6-1: retain existing ROM modules; original dependency checks unchanged.' + ending)
        amended = line
        for m in reversed(matches):
            amended = amended[:m.start()] + 'RMEnsure ' + m[1] + ' 0.00 ' + amended[m.start():]
            edits.append(dict(file=filename, line=number, module=m[1], minimum=m[2],
                              before=line.rstrip('\r\n'), after=amended.rstrip('\r\n'),
                              roms=[rom for rom, entry in inventory.items()
                                    if m[1].lower() in {n.lower() for n in entry['modules']}]))
        output.append(amended)
    return ''.join(output).encode('latin1'), edits
