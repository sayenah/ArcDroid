#!/usr/bin/env python3
"""Independent RPC FileCore vectors and blank formatter regression. GPL-2.0-only."""
import pathlib
import struct
import subprocess
import sys
import tempfile
from rpc_filecore_check import Reader, Invalid, SIZE, MAP, ROOT, check, directory_checksum, zone_checksum


def expect_bad(fn, reason):
    try:
        fn()
    except Invalid as error:
        if reason not in str(error):
            raise RuntimeError(f'wrong refusal: {error}; wanted {reason}') from error
    else:
        raise RuntimeError(f'accepted invalid vector: {reason}')


def directory(entries, name='$', parent=0x20b):
    d = bytearray(2048)
    d[:5] = d[2042:2047] = b'\x01Nick'
    d[2010:2013] = parent.to_bytes(3, 'little')
    d[2032:2042] = name.encode().ljust(10, b'\r')
    for index, (name, addr, size) in enumerate(entries):
        at = 5 + index * 26
        d[at:at + 10] = name.encode().ljust(10, b'\r')
        struct.pack_into('<III', d, at + 10, 0xffffff00, 0, size)
        d[at + 22:at + 25] = addr.to_bytes(3, 'little')
        d[at + 25] = 3
    d[2047] = directory_checksum(d, len(entries))
    return d


def vectors():
    # Fixed, independently specified extents: id 1019 starts in zone 4 and wraps
    # through 0, then 1. The first shared sector crosses the first fragment end.
    r = Reader(lambda at, n: b'\0' * n)
    r.extents = [(0, 528, 544, 1019, 131072),
                 (1, 32, 48, 1019, 29360128),
                 (4, 32, 48, 1019, 129236992)]
    if r.resolve(1019 << 8, 262656) != [(129236992, 131072),
                                       (131072, 131072), (29360128, 512)]:
        raise RuntimeError('zone wrap vector')
    if r.resolve((1019 << 8) | 255, 2048) != [(129367040, 1024), (131072, 1024)]:
        raise RuntimeError('sharing sector 254 vector')
    expect_bad(lambda: r.resolve((1019 << 8) | 255, 400000), 'exceeds allocation')
    r.extents = [(4, 368, 384, 1019, 131989504)]
    if r.resolve(1019 << 8, 131072) != [(131989504, 131072)]:
        raise RuntimeError('partial zone final fragment')
    expect_bad(lambda: r.resolve(1019 << 8, 131073), 'exceeds allocation')
    # A 77-entry Nick directory is full but representable. Entry 78 would
    # overwrite its tail. A ten-character leaf is valid; eleven cannot be.
    entries = [('A' + f'{i:09d}', 0, 0) for i in range(77)]
    d = directory(entries)
    def read(at, n):
        if ROOT <= at and at + n <= ROOT + 2048:
            return d[at - ROOT:at - ROOT + n]
        return b'\0' * n
    r = Reader(read)
    r.extents = [(2, 32, 48, 2, MAP), (0, 512, 528, 2, 0)]
    if len(r.walk().tree) != 78:
        raise RuntimeError('77-entry vector')
    d[2007] = 65
    r = Reader(read)
    r.extents = [(2, 32, 48, 2, MAP), (0, 512, 528, 2, 0)]
    expect_bad(r.walk, '78 entries')
    r = Reader(read, guest=True)
    r.extents = [(2, 32, 48, 2, MAP), (0, 512, 528, 2, 0)]
    expect_bad(r.walk, '78 entries')
    from rpc_filecore_check import filename
    expect_bad(lambda: filename(b'ABCDEFGHIJK'), 'name length')
    if filename(b'ABCDEFGHIJ') != 'ABCDEFGHIJ':
        raise RuntimeError('ten-character vector')
    expect_bad(lambda: filename(b'%'), 'name characters')
    # Literal Nick flags 0x17 mean owner R/W, public R, locked file.
    # OS_File's independent access byte is 0x1b, with the lock in bit 3.
    d = directory([('Locked', 0, 0)])
    d[30] = 0x17
    d[2047] = directory_checksum(d, 1)
    r = Reader(read)
    r.extents = [(2, 32, 48, 2, MAP), (0, 512, 528, 2, 0)]
    if r.walk().tree['$.Locked']['attr'] != 0x1b:
        raise RuntimeError('Nick / OS_File locked metadata vector')
    # Detect overlap and invalid cross-parent sharing independent of contents.
    r = Reader(lambda at, n: b'\0' * n)
    r.extents = [(0, 528, 544, 3, 131072)]
    r.claim(0x301, 512, '$/a', 0x20b, False)
    expect_bad(lambda: r.claim(0x301, 512, '$/b', 0x20b, False), 'overlapping')
    expect_bad(lambda: r.claim(0x302, 512, '$/d/b', 0x401, False), 'across parents')


def blank(exe):
    with tempfile.TemporaryDirectory(prefix='arcdroid-rpc-check-') as tmp:
        path = pathlib.Path(tmp) / 'authored.hdf'
        subprocess.run([exe, '--write', str(path)], check=True)
        with path.open('rb') as file:
            def read(at, n):
                file.seek(at)
                return file.read(n)
            r = check(read, boot_option=0)
            if r.tree != {'$': {'directory': True}} or r.free_bytes != SIZE - 262144:
                raise RuntimeError('blank allocation / directory')
            boot = bytearray(read(3072, 512))
            maps = bytearray(read(MAP, 2560))
            root = bytearray(read(ROOT, 2048))
            def overridden(at, n):
                for start, data in ((3072, boot), (MAP, maps), (MAP + 2560, maps), (ROOT, root)):
                    if start <= at and at + n <= start + len(data):
                        return data[at - start:at - start + n]
                return read(at, n)
            # Each guest mutation alone, then combined. Checksums are repaired
            # independently; strict admission must still reject the changed bytes.
            initial = (bytes(boot), bytes(maps), bytes(root))
            def valid_checks():
                maps[0] = zone_checksum(maps[:512])
                root[2047] = directory_checksum(root, 0)
            def changed_id():
                maps[24:26] = b'\x61\x92'
            def changed_sequence():
                root[0] = root[2042] = 0x8f
            def stale_end():
                root[6:120] = bytes(range(114))
            mutations = (changed_id, changed_sequence, stale_end)
            for changes in (*[(x,) for x in mutations], mutations):
                boot[:], maps[:], root[:] = initial
                for change in changes: change()
                valid_checks()
                check(overridden, guest=True)
                try: check(overridden)
                except Invalid: pass
                else: raise RuntimeError('strict accepted guest mutation')
                # A neighbouring sequence/check/map-copy corruption is refused
                # by both modes, even when all tolerated changes are present.
                for where, at in ((root, 2042), (root, 2047), (maps, 25)):
                    where[at] ^= 1
                    if at == 2042: root[2047] = directory_checksum(root, 0)
                    for guest in (False, True):
                        try: check(overridden, guest=guest)
                        except Invalid: pass
                        else: raise RuntimeError('guest integrity corruption accepted')
                    where[at] ^= 1
                    if at == 2042: root[2047] = directory_checksum(root, 0)
            # Rechecksummed adjacent disc-name corruption is not a mutable ID.
            maps[26] ^= 1
            maps[0] = zone_checksum(maps[:512])
            expect_bad(lambda: check(overridden, guest=True), 'disc-record copies')
            boot[:], maps[:], root[:] = initial
            for padding in (b'\0'*9, b' '*9, b'\r'+b'garbage!'):
                root[:] = directory([('A', 0, 0)])
                root[6:15] = padding
                root[2047] = directory_checksum(root, 1)
                assert '$.A' in check(overridden, guest=True).tree
                expect_bad(lambda: check(overridden), 'name')
                # All four tolerances together still preserve integrity.
                changed_id();changed_sequence()
                root[32:120] = bytes(range(88))  # beyond the zero terminator
                maps[0] = zone_checksum(maps[:512])
                root[2047] = directory_checksum(root, 1)
                assert '$.A' in check(overridden, guest=True).tree
                expect_bad(lambda: check(overridden), 'disc-record')
                root[2042] ^= 1
                root[2047] = directory_checksum(root, 1)
                for guest in (False, True):
                    expect_bad(lambda: check(overridden, guest=guest), 'sequence' if guest else 'disc-record')
                boot[:], maps[:], root[:] = initial
            boot[:], maps[:], root[:] = initial
            root[2047] ^= 1
            expect_bad(lambda: check(overridden), 'directory checksum')
            root[2047] ^= 1
            # Independently specified occupied extents and free chain, not
            # generated by the production allocator. Fragment id 1019 resolves
            # zone 4 first, then zone 0; its file spans both fragments.
            saved_root = bytes(root)
            root[:] = directory([('A', 0x20f, 512), ('B', 0x210, 512)])
            check(overridden)  # root's own files may share reserved object 2
            root[:] = directory([('A', 0x201, 512)])
            expect_bad(lambda: check(overridden), 'reserved map/root bytes')
            root[:] = directory([('A/B', 0, 0)])
            if '$.A/B' not in check(overridden).tree:
                raise RuntimeError('RISC OS slash leaf')
            root[:] = saved_root
            def set_bits(block, at, length, value):
                word = int.from_bytes(block, 'little')
                mask = ((1 << length) - 1) << at
                return bytearray(((word & ~mask) | (value << at)).to_bytes(512, 'little'))
            for zone, begin, end in ((0, 528, 4096), (4, 32, 384)):
                block = bytearray(maps[zone * 512:(zone + 1) * 512])
                # Replace old free fragment with a 16-unit allocated extent,
                # followed by the remaining free fragment.
                for bit in range(begin, end):
                    block[bit // 8] &= ~(1 << (bit % 8))
                block = set_bits(block, begin, 15, 1019)
                block = set_bits(block, begin + 15, 1, 1)
                block = set_bits(block, end - 1, 1, 1)
                block = set_bits(block, 8, 15, begin + 16 - 8)
                block[0] = zone_checksum(block)
                maps[zone * 512:(zone + 1) * 512] = block
            root[:] = directory([('Fragmented', 1019 << 8, 131584)])
            def fragmented(at, n):
                if (129236992 <= at and at + n <= 129368064 or
                        131072 <= at and at + n <= 262144):
                    return bytes([0x5a]) * n
                return overridden(at, n)
            import hashlib
            populated = check(fragmented)
            expected = dict(directory=False, load=0xffffff00, exec=0, attr=3,
                            size=131584,
                            sha256=hashlib.sha256(bytes([0x5a]) * 131584).hexdigest())
            if populated.tree['$.Fragmented'] != expected:
                raise RuntimeError('fragmented map / bytes vector')
            # A shared offset at the very last legal start sector crosses the
            # extent boundary, with two disjoint files under the same parent.
            root[:] = directory([('A', (1019 << 8) | 1, 130048),
                                 ('B', (1019 << 8) | 255, 2048)])
            shared = check(fragmented)
            if shared.tree['$.B']['size'] != 2048:
                raise RuntimeError('shared vector')
            # Neither file overlaps the other, but offset zero claims the
            # entire disc object exclusively. Check both directory orders.
            for entries in ([('A', 1019 << 8, 512),
                             ('B', (1019 << 8) | 2, 512)],
                            [('A', (1019 << 8) | 2, 512),
                             ('B', 1019 << 8, 512)]):
                root[:] = directory(entries)
                expect_bad(lambda: check(fragmented), 'unshared disc object alias')
            maps[:] = read(MAP, 2560)
            root[:] = saved_root
            # CRC-valid free link into the middle of an extent cannot pass.
            maps[1] ^= 1
            maps[0] = zone_checksum(maps[:512])
            expect_bad(lambda: check(overridden), 'free')
            maps[:] = read(MAP, 2560)
            maps[2051] ^= 1
            maps[2048] = zone_checksum(maps[2048:2560])
            expect_bad(lambda: check(overridden), 'cross-check')
            maps[:] = read(MAP, 2560)
            boot[455] = 2
            total = sum(boot[:511])
            while total > 255:
                total = total % 256 + total // 256
            boot[511] = total
            expect_bad(lambda: check(overridden, boot_option=2), 'disc-record copies')
            # Independent *Opt 4 vectors: all three disc records agree and
            # their checks are valid, so Exec must fail on its meaning alone.
            for option in (2, 0, 1, 3):
                boot[455] = maps[11] = option
                maps[0] = zone_checksum(maps[:512])
                total = sum(boot[:511])
                while total > 255:
                    total = total % 256 + total // 256
                boot[511] = total
                if option in (0, 2):
                    check(overridden, boot_option=option)
                    expect_bad(lambda: check(overridden, boot_option=2-option),
                               'boot option')
                else:
                    expect_bad(lambda: check(overridden, boot_option=2),
                               'boot option')
            # Independently alter each format field in all three records;
            # valid checks and matching copies isolate its semantic refusal.
            for option in (0, 2):
                boot[:] = read(3072, 512)
                maps[:] = read(MAP, 2560)
                boot[455] = maps[11] = option
                for offset, value, reason in ((40, 1, 'share size'),
                                              (40, 16, 'share size'),
                                              (41, 1, 'addressing flags'),
                                              (41, 2, 'big-directory'),
                                              (42, 1, 'high zone count'),
                                              *((at, 1, 'format version') for at in range(44, 48))):
                    boot[448 + offset] = maps[4 + offset] = value
                    maps[0] = zone_checksum(maps[:512])
                    total = sum(boot[:511])
                    while total > 255:
                        total = total % 256 + total // 256
                    boot[511] = total
                    expect_bad(lambda: check(overridden, boot_option=option), reason)
                    boot[448 + offset] = maps[4 + offset] = 0
                maps[0] = zone_checksum(maps[:512])
                total = sum(boot[:511])
                while total > 255:
                    total = total % 256 + total // 256
                boot[511] = total
                check(overridden, boot_option=option)
    print('rpc-filecore-check: blank, free links, sharing, fragmentation, wrap, final zone, names/directories pass')


if __name__ == '__main__':
    vectors()
    blank(sys.argv[1])
