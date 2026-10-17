#!/usr/bin/env python3
"""Independent test-side reader for the five-zone RPC FileCore geometry.

Project-authored GPL-2.0-only. Does not import writer, formatter, allocation,
resolution or checksum code. Reads bounded ranges, never the entire disc.
PRM: https://www.riscos.com/support/developers/prm/filecore.html
"""
import hashlib
import struct

SIZE = 132120576
MAP = 62652416
ROOT = 62657536


class Invalid(ValueError):
    pass


def require(condition, reason):
    if not condition:
        raise Invalid(reason)


def integer(data, at, count=4):
    return int.from_bytes(data[at:at + count], 'little')


def bits(data, first, count):
    return (int.from_bytes(data, 'little') >> first) & ((1 << count) - 1)


def zone_checksum(data):
    # Four byte lanes with the previous lane's carry. Separate implementation
    # from the formatter's scalar loop, expressed as a lane recurrence.
    lanes = [0, 0, 0, 0]
    for word in range(127, 0, -1):
        for lane in range(4):
            previous = (lane - 1) % 4
            lanes[lane] += data[4 * word + lane] + (lanes[previous] // 256)
            lanes[previous] %= 256
    lanes[0] += lanes[3] // 256
    for lane in range(1, 4):
        lanes[lane] += data[lane] + lanes[lane - 1] // 256
    return (lanes[0] ^ lanes[1] ^ lanes[2] ^ lanes[3]) & 255


def directory_checksum(data, count):
    end = 5 + 26 * count
    values = list(struct.unpack('<' + 'I' * (end // 4), data[:end // 4 * 4]))
    values += list(data[end // 4 * 4:end])
    values += list(struct.unpack('<9I', data[2008:2044]))
    value = 0
    for item in values:
        value = item ^ ((value >> 13) | ((value << 19) & 0xffffffff))
    return (value ^ (value >> 8) ^ (value >> 16) ^ (value >> 24)) & 255


def filename(raw, guest=False):
    # CR padding; allow all printable Latin-1 except FileCore path/syntax chars.
    name = raw.split(b'\r', 1)[0]
    if guest:
        name = raw[:next((i for i, c in enumerate(raw) if c <= 32), len(raw))]
    require(name and len(name) <= 10, 'name length')
    require(guest or raw[len(name):] == b'\r' * (len(raw) - len(name)), 'name padding')
    require(all(c >= 33 and c != 127 and c not in b'.:*#$&@^%\\' for c in name),
            'name characters')
    return name.decode('latin1')


class Reader:
    def __init__(self, read, guest=False):
        self.read = read
        self.guest = guest
        self.extents = []  # (zone, start bit, end bit, id, physical byte)
        self.used = []
        self.tree = {}

    def exact(self, at, count):
        require(0 <= at <= SIZE and count <= SIZE - at, 'disc range')
        data = self.read(at, count)
        require(len(data) == count, 'short disc read')
        return data

    def maps(self):
        boot = self.exact(3072, 512)
        require(integer(boot, 0) == 0x20000000, 'boot address')
        total = sum(boot[:511])
        while total > 255:
            total = total // 256 + total % 256
        require(boot[511] == total, 'boot checksum')
        record = boot[448:508]
        require(record[:6] == bytes([9, 63, 16, 0, 15, 13]), 'geometry')
        require(record[9:12] == bytes([5, 32, 0]), 'map geometry')
        require(integer(record, 12) == 0x20b and integer(record, 16) == SIZE,
                'root / size')
        # Independent FileSwitch PRM, *Opt 4 table: 0 none, 1 *Load,
        # 2 *Run, 3 *Exec. Populated boot discs require Run; blanks keep none.
        # https://www.riscos.com/support/developers/prm/fileswitch.html
        # FileCore PRM, disc-record offset 7, binds this byte to *Opt 4,n:
        # https://www.riscos.com/support/developers/prm/filecore.html
        require(record[7] in (0, 2), 'boot option')
        self.boot_option = record[7]
        # FileCore PRM vol. 5a, Disc record / Internal disc addresses:
        # byte 40 is log2(sharing granularity in sectors). Zero preserves
        # the one-sector offsets understood by RISC OS 3.50.
        # https://www.riscos.com/support/developers/prm/filecorenew.html
        require(record[40] == 0, 'share size')
        # Same PRM table: byte 41 bit 0 selects >512 MiB addressing, with
        # bits 1..7 reserved. This 126 MiB, Nick-directory format keeps zero.
        # Acorn's later FileCore specification (7.5) assigns bit 1 to big dirs:
        # https://www.4corn.co.uk/archive/Acorn_Developer_Site/reference/ro4/api/html/filecore2.html
        require(record[41] == 0, 'big-directory / addressing flags')
        # PRM vol. 5a reserves bytes 42..59 as zero. Acorn's later table
        # identifies byte 42 as nzones_2; these five zones need no high byte.
        require(record[42] == 0, 'high zone count')
        # The same reserved-zero PRM bytes include the word at 44. The later
        # specification (3.14.5) defines version 0 for existing Nick discs,
        # version 1 for big directories. Check the entire four-byte word.
        require(integer(record, 44) == 0, 'format version')
        data = self.exact(MAP, 2560)
        require(data == self.exact(MAP + 2560, 2560), 'map copies')
        map_record = data[4:64]
        require(map_record == record if not self.guest else
                map_record[:20] == record[:20] and map_record[22:] == record[22:],
                'disc-record copies / boot option')
        require(data[3] ^ data[515] ^ data[1027] ^ data[1539] ^ data[2051] == 255,
                'map cross-check')
        free_bytes = 0
        for zone in range(5):
            block = data[zone * 512:(zone + 1) * 512]
            require(block[0] == zone_checksum(block), 'zone checksum')
            require(bits(block, 23, 1) == 1, 'free header terminator')
            free = bits(block, 8, 15)
            free = 8 + free if free else 0
            free_starts = set()
            at = 512 if zone == 0 else 32
            end = 384 if zone == 4 else 4096
            while at < 4096:
                begin = at
                identity = bits(block, at, 15)
                at += 15
                while at < 4096 and not bits(block, at, 1):
                    at += 1
                require(at < 4096, 'unterminated fragment')
                at += 1
                require(at <= end or (begin == end and zone == 4 and identity == 1
                                     and at == 4096), 'partial final zone')
                physical = (zone * 4064 - 480 + begin - 32) * 8192
                if begin == free:
                    require(begin < end, 'free beyond disc')
                    free_starts.add(begin)
                    free_bytes += (at - begin) * 8192
                    free = begin + identity if identity else 0
                    require(not free or at <= free < end, 'free link range / cycle')
                else:
                    require(identity >= 1, 'unlinked free fragment')
                    if begin < end:
                        require(identity != 1, 'unexpected bad-sector reservation')
                        self.extents.append((zone, begin, at, identity, physical))
                if at == end and end < 4096:
                    continue
            require(free == 0, 'free link is not fragment start')
        reserved = [(e[0], e[1], e[2], e[4]) for e in self.extents if e[3] == 2]
        require(reserved == [(0, 512, 528, 0), (2, 32, 48, MAP)], 'reserved objects')
        self.free_bytes = free_bytes
        return self

    def resolve(self, address, length):
        require(address <= 0x7fffff, 'indirect address range')
        if not length:
            require(address == 0, 'empty object address')
            return []
        identity = address >> 8
        require(identity >= 2 and identity < 1270, 'fragment id range')
        first = 2 if identity == 2 else identity // 254
        extents = sorted((e for e in self.extents if e[3] == identity),
                         key=lambda e: ((e[0] - first) % 5, e[1]))
        skip = ((address & 255) - 1) * 512 if address & 255 else 0
        result = []
        for zone, begin, end, _, physical in extents:
            size = (end - begin) * 8192
            if skip >= size:
                skip -= size
                continue
            take = min(length, size - skip)
            result.append((physical + skip, take))
            length -= take
            skip = 0
            if not length:
                break
        require(length == 0, 'object exceeds allocation')
        return result

    def object_data(self, address, length):
        return b''.join(self.exact(at, count) for at, count in self.resolve(address, length))

    def walk(self, address=0x20b, path='$', parent=0x20b, depth=0):
        require(depth <= 32 and path not in self.tree, 'directory depth / cycle')
        data = self.object_data(address, 2048)
        require(data[1:5] == b'Nick' and data[2043:2047] == b'Nick' and
                data[0] == data[2042] and (self.guest or data[0] == 1),
                'directory signature / sequence')
        require(integer(data, 2010, 3) == parent, 'directory parent')
        name = filename(data[2032:2042], self.guest) if path != '$' else '$'
        require(name == path.rsplit('.', 1)[-1], 'directory name')
        count = 0
        while count < 77 and data[5 + count * 26]:
            count += 1
        require(data[5 + count * 26] == 0 and
                (self.guest or not any(data[5 + count * 26:2008])),
                'directory end / 78 entries')
        require(data[2047] == directory_checksum(data, count), 'directory checksum')
        self.tree[path] = {'directory': True}
        self.claim(address, 2048, path, parent, True)
        previous = b''
        for entry in range(count):
            raw = data[5 + entry * 26:31 + entry * 26]
            name = filename(raw[:10], self.guest)
            folded = raw[:len(name)].upper()
            require(previous < folded, 'FileCore order / duplicate')
            previous = folded
            child = path + '.' + name
            load, execute, size = struct.unpack('<III', raw[10:22])
            addr, flags = integer(raw, 22, 3), raw[25]
            require(flags & 0xc0 == 0, 'reserved entry attributes')
            # Return OS_File attributes, independently of the writer's mapping.
            attr = (flags & 0x33) + (8 if flags & 4 else 0)
            if flags & 8:
                require(size == 2048, 'directory size')
                self.walk(addr, child, address, depth + 1)
                self.tree[child].update(load=load, exec=execute, attr=attr)
            else:
                self.claim(addr, size, child, address, False)
                h = hashlib.sha256()
                for at, count in self.resolve(addr, size):
                    for start in range(0, count, 32768):
                        h.update(self.exact(at + start, min(32768, count - start)))
                self.tree[child] = dict(directory=False, load=load, exec=execute,
                                        attr=attr, size=size, sha256=h.hexdigest())
        return self

    def claim(self, addr, size, path, parent, directory):
        for at, count in self.resolve(addr, size):
            require(all(at + count <= lo or hi <= at for lo, hi, *_ in self.used),
                    'overlapping object bytes')
            self.used.append((at, at + count, addr >> 8, path, parent, directory, addr))
        identity = addr >> 8
        if identity == 2:
            require(path == '$' or (not directory and parent == 0x20b),
                    'reserved object alias')
            if path != '$':
                for at, count in self.resolve(addr, size):
                    require(at + count <= 3072 or at >= 3584, 'reserved boot bytes')
                    require(at + count <= MAP or at >= ROOT + 2048,
                            'reserved map/root bytes')
        neighbours = [entry for entry in self.used if entry[2] == identity]
        # FileCore PRM vol. 2, Internal disc addresses: sector offset 0
        # means this object does not share its disc object (fragment group).
        # https://www.riscos.com/support/developers/prm/filecore.html
        # Multiple extents belonging to one object are still legal.
        require(len({e[3] for e in neighbours}) <= 1 or
                all(e[6] & 255 for e in neighbours), 'unshared disc object alias')
        dirs = {entry[3]: entry for entry in neighbours if entry[5]}
        require(len(dirs) <= 1, 'two shared directories')
        if dirs:
            dirpath = next(iter(dirs))
            require(all(e[3] == dirpath or e[4] == dirs[dirpath][6]
                        for e in neighbours),
                    'sharing across parents')
        else:
            require(len({e[4] for e in neighbours}) <= 1, 'sharing across parents')


def check(read, boot_option=0, guest=False):
    reader = Reader(read, guest=guest).maps().walk()
    require(reader.boot_option == boot_option, 'boot option')
    require({e[3] for e in reader.extents} == {e[2] for e in reader.used},
            'unreferenced allocated object')
    return reader
