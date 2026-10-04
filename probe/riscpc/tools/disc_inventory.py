#!/usr/bin/env python3
"""Read-only new-map/new-directory inventory for private probe custody.

GPL-2.0-only. Encoding checks adapted from core/jfd_file_data.c; this reader
handles the multi-zone hard-disc variant independently of allocation order.
Writes only the requested JSON inventory, never extracted file content.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from boot_disc import zone_check, dir_check


class Disc:
    def __init__(self, data):
        self.data = data
        r = data[0xdc0:0xdfc]
        self.sector = 1 << r[0]
        self.id_bits = r[4]
        self.unit = 1 << r[5]
        zones = self.zones = r[9]
        spare = int.from_bytes(r[10:12], 'little')
        self.root = int.from_bytes(r[12:16], 'little')
        size = int.from_bytes(r[16:20], 'little')
        assert len(data) == size and self.sector == 512 and self.id_bits == 15
        self.ids_per_zone = (self.sector * 8 - spare) // (self.id_bits + 1)
        self.centre = zones // 2
        zone_bits = self.sector * 8 - spare
        maps_at = (self.centre * zone_bits - 480) * self.unit
        maps = data[maps_at:maps_at + zones * self.sector]
        assert maps == data[maps_at + zones * self.sector:maps_at + 2 * zones * self.sector]
        assert all(maps[4 + i] == r[i] for i in range(60) if i not in (7, 20, 21))
        self.fragments = []
        cross = 0
        for zone in range(zones):
            b = maps[zone * self.sector:(zone + 1) * self.sector]
            assert zone_check(b) == b[0] and b[2] & 128
            cross ^= b[3]
            def read_bits(at, n):
                return sum(((b[(at + k) // 8] >> ((at + k) % 8)) & 1) << k for k in range(n))
            link = read_bits(8, self.id_bits)
            free = 8 + link if link else 0
            at = 512 if zone == 0 else 32
            end = self.sector * 8 - spare + 32
            if zone == zones - 1:
                end = 32 + size // self.unit - ((zones - 1) * zone_bits - 480)
            while at < end:
                ident = read_bits(at, self.id_bits)
                stop = at + self.id_bits
                while stop < self.sector * 8 and not read_bits(stop, 1):
                    stop += 1
                assert stop < self.sector * 8 and stop < end
                is_free = at == free
                if is_free:
                    free = at + ident if ident else 0
                    assert not free or free > stop
                physical = (zone * zone_bits - 480 + at - 32) * self.unit
                self.fragments.append((zone, ident, physical, (stop + 1 - at) * self.unit, is_free))
                at = stop + 1
            assert at == end and free == 0
        assert cross == 255

    def read(self, address, length):
        ident, shared = (address >> 8) & 32767, address & 255
        if not length:
            return b''
        assert ident >= 2
        start_zone = self.centre if ident == 2 else ident // self.ids_per_zone
        fragments = sorted((f for f in self.fragments if f[1] == ident and not f[4]),
                           key=lambda f: (f[0] - start_zone) % self.zones)
        skip = (shared - 1) * self.sector if shared else 0
        result = bytearray()
        for zone, _, at, n, _ in fragments:
            if skip >= n:
                skip -= n
                continue
            amount = min(n - skip, length - len(result))
            result.extend(self.data[at + skip:at + skip + amount])
            skip = 0
            if len(result) == length:
                return bytes(result)
        raise ValueError('unresolved extent')

    def inventory(self):
        output, seen = {}, set()
        def visit(address, path):
            assert address not in seen
            seen.add(address)
            b = self.read(address, 2048)
            assert b[0] == b[2042] and b[1:5] == b[2043:2047] and b[1:5] in (b'Nick', b'Hugo')
            count = next((i for i in range(77) if not b[5 + 26 * i]), 77)
            assert dir_check(b, count) == b[2047]
            for i in range(count):
                e = b[5 + 26 * i:31 + 26 * i]
                name = e[:10].split(b'\r')[0].split(b'\0')[0].decode('latin1')
                load, execute, length = struct.unpack_from('<III', e, 10)
                child = int.from_bytes(e[22:25], 'little')
                item = path + name
                if e[25] & 8:
                    assert length == 2048
                    visit(child, item + '/')
                else:
                    content = self.read(child, length)
                    output[item] = dict(load=load, execute=execute, attr=e[25], size=length,
                                        sha256=hashlib.sha256(content).hexdigest())
        visit(self.root, '')
        return output


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('image', type=Path)
    p.add_argument('out', type=Path)
    args = p.parse_args()
    data = args.image.read_bytes()
    files = Disc(data).inventory()
    args.out.write_text(json.dumps(dict(sha256=hashlib.sha256(data).hexdigest(), files=files), indent=2) + '\n')
    print('verified file inventories:', len(files))
