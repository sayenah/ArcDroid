"""Authored ARM calendar reader over real IOMD/IIC. GPL-2.0-only."""
from rpc_d2_rom import Guest
from rpc_cmos_factory_rom import factory


def calendar_reader():
    g = Guest()
    g.init()
    calls = []

    def pins(*values):
        for value in values:
            g.mov(3, value)
            g.store(3, 9)

    def send(value):
        g.mov(0, value)
        calls.append(len(g.words))
        g.emit(0)

    pins(3, 2)
    send(0xa0)
    send(0)
    pins(0, 2, 3, 2)
    send(0xa1)
    g.mov(6, 0)                 # physical address
    g.mov(11, 1)                # independent RISC OS checksum sum
    loop = len(g.words)
    g.mov(7, 0)
    g.mov(5, 8)
    bit = len(g.words)
    pins(1, 3)
    g.read(0, 9)
    g.emit(0xe1a07087, 0xe2000001, 0xe1877000, 0xe2555001)
    g.branch(bit, 1)
    # Store each byte as a word; ACK except after the last byte.
    g.emit(0xe7887106, 0xe35600ff)  # STR r7,[r8,r6,LSL #2]; CMP r6,#255
    g.mov(3, 0)
    g.emit(0x03a03001)           # MOVEQ r3,#1 (NACK)
    g.store(3, 9)
    g.emit(0xe3833002)
    g.store(3, 9)
    g.emit(0xe356003f, 0x01a0c007)  # checksum at physical 3F
    g.emit(0xe3560010)
    skip_low = len(g.words)
    g.emit(0)
    g.emit(0xe356003f, 0x108bb007)  # ADDNE r11,r11,r7
    g.words[skip_low] = 0x3a000000 | ((len(g.words)-skip_low-2) & 0xffffff)
    g.emit(0xe2866001, 0xe3560c01)  # ADD; CMP #256
    g.branch(loop, 1)
    pins(0, 2, 3)
    g.emit(0xe20bb0ff, 0xe15b000c)  # checksum modulo 256
    g.mov(0, 0)
    g.emit(0x03a00001)
    g.store(0, 8, 0x404)        # checksum valid; invalid CMOS would be reset
    g.read(0, 8, 0xc1*4)
    g.mov(1, 100)
    g.emit(0xe0020190)          # MUL r2,r0,r1 (century is binary)
    g.read(0, 8, 0xc0*4)
    g.emit(0xe0822000)
    g.read(0, 8, 5*4)
    g.emit(0xe1a00320, 0xe0400002, 0xe2000003, 0xe0822000)
    # Kernel-style advance to the RTC's year mod 4. Stale seeds reconstruct
    # 2000..2003, so a host-side snapshot alone cannot satisfy this oracle.
    g.store(2, 8, 0x400)
    g.mov(0, 0xa5)
    g.store(0, 8, 0x408)
    g.emit(0xeafffffe)
    sub = len(g.words)
    for call in calls:
        g.words[call] = 0xeb000000 | ((sub-call-2) & 0xffffff)
    g.mov(4, 128)
    bit = len(g.words)
    g.emit(0xe1100004)
    g.mov(3, 0)
    g.emit(0x13a03001)
    g.store(3, 9)
    g.emit(0xe3833002)
    g.store(3, 9)
    g.emit(0xe1b040a4)
    g.branch(bit, 1)
    pins(1, 3)
    g.emit(0xe1a0f00e)
    return g


def factory_calendar_reader():
    """Held Delete takes the ordinary authored factory; other boots read year."""
    words = factory('full').words
    resume = next(i+2 for i, w in enumerate(words) if w == 0xe3500071)
    stub = Guest()
    stub.init()
    stub.mov(0, 0)
    stub.emit(0xee010f10)
    for offset, value in [(0x40, 255), (0x44, 255), (0x48, 0)]:
        stub.mov(0, value)
        stub.store(0, 9, offset)
    stub.mov(6, 4)
    loop = len(stub.words)
    stub.read(0, 9, 8)
    stub.emit(0xe3100020)
    no_byte = len(stub.words)
    stub.emit(0)
    stub.read(0, 9, 4)
    stub.emit(0xe3500071)
    found = len(stub.words)
    stub.emit(0)
    timer = len(stub.words)
    stub.read(0, 9, 0x10)
    stub.emit(0xe3100020)
    stub.branch(loop, 0)
    stub.mov(0, 32)
    stub.store(0, 9, 0x14)
    stub.emit(0xe2566001)
    stub.branch(loop, 1)
    stub.words[no_byte] = 0x0a000000 | ((timer-no_byte-2) & 0xffffff)
    base = 4096
    stub.words[found] = 0x0a000000 | ((resume-base-found-2) & 0xffffff)
    words[0] = 0xea000000 | (base-2)
    words.extend([0xeafffffe]*(base-len(words)))
    words += stub.words + calendar_reader().words
    return words
