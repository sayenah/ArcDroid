#!/usr/bin/env python3
"""Integrated authored MMU permission guest and analytic observations. GPL-2.0-only.
All descriptor writes, privilege changes and accesses are guest instructions.
"""
from rpc_video_rom import Guest


class Matrix(Guest):
    def __init__(self, model):
        super().__init__()
        self.model = model
        self.expected = []
        self.w = [0xeafffffe] * 128
        self.w[0] = 0xea00007e
        self.w[4] = 0xea00001a  # data abort -> 0x80
        # ABT stores count/FSR/FAR into the current result record; R1 is kept.
        self.w[32:38] = [0xe3a0b001, 0xe588b000, 0xee15bf10, 0xe588b008,
                        0xee16bf10, 0xe588b00c]
        # SWI restores supervisor privilege, keeping flags/IRQ masks.
        self.w[2] = 0xea000025  # SWI -> 0xa4
        self.w[41:45] = [0xe14fb000, 0xe38bb003, 0xe16ff00b, 0xe1b0f00e]
        self.w[38] = 0xea000038  # -> 0x180, record exact ABT entry state
        self.w[96:106] = [0xe59fc018,0xe58ce000,0xe14fb000,0xe58cb004,
                          0xe10fb000,0xe58cb008,0xea000000 | ((49-102-2)&0xffffff),
                          0xe1a00000,0x1003f000,0xe1a00000]
        self.w[49] = 0xe25ef004
        # Prefetch returns to declared continuation R10 in SVC32. Preserve the
        # prior data FAR/FSR exactly; no subsequent forbidden instruction runs.
        self.w[3] = 0xea00003b  # -> 0x100
        self.w[64:74] = [0xe3a0b001,0xe588b000,0xe588e004,0xee15bf10,
                         0xe588b008,0xee16bf10,0xe588b00c,0xe3a0b0d3,
                         0xe16ff00b,0xe1b0f00a]
        # Prefetch mailbox preserves SPSR and ABT mode before the handler
        # chooses an explicitly privileged continuation.
        self.w[71]=0xea000002
        self.w[75:84]=[0xe59fc018,0xe14fb000,0xe58cb000,0xe10fb000,
                       0xe58cb004,0xe3a0b0d3,0xeafffff5,0xe1a00000,0x1003f020]
        # Model-specific Undefined (LDRH on ARMv3); skip failing instruction.
        self.w[1] = 0xea000057  # -> 0x168
        self.w[90:93] = [0xe3a0b001,0xe588b000,0xe1b0f00e]
        # Both instruction sets start in SVC26, including reset on SA-110.
        self.literal(0,0);self.emit(0xee010f10)
        self.literal(8,0x10040000);self.literal(0,0x10000300)
        self.emit(0xe3a0102a,0xe5801000,0xe5902000,0xe2822001)
        self.emit(0xe5881000,0xe5882004,0xe5880008,0xe588100c)
        self.expected.append([42,43,0x10000300,42])
        self.literal(0, 0x70); self.emit(0xee010f10)
        self.literal(0, 0xd3); self.emit(0xe121f000)
        self.literal(8,0x10040010);self.literal(0,0x10000300)
        self.emit(0xe3a0102a,0xe5801000,0xe5902000,0xe2822001,
                  0xe5881000,0xe5882004,0xe5880008,0xe588100c)
        self.expected.append([42,43,0x10000300,42])
        # RAM execution and self-modifying code, entirely through guest stores.
        self.write(0x10030000,0xe3a03007);self.write(0x10030004,0xe1a0f00e)
        self.literal(0,0x10030000);self.emit(0xe1a0e00f,0xe1a0f000)
        self.literal(8,0x10040020);self.emit(0xe5883000)
        self.write(0x10030000,0xe3a03009)
        self.literal(0,0x10030000);self.emit(0xe1a0e00f,0xe1a0f000,0xe5883004)
        self.literal(0,0);self.emit(0xe5880008,0xe588000c)
        self.expected.append([7,9,0,0])
        self.fill(0x10004000, 4096, 0)
        for index, physical in [(0, 0), (0x100, 0x10000000)]:
            self.write(0x10004000+index*4, physical | 0xc02)
        self.literal(0, 0x10004000); self.emit(0xee020f10)
        self.literal(0, 0x10000100); self.literal(1, 0x11223344); self.emit(0xe5801000)

    def flush(self):
        self.literal(0, 0)
        self.emit(0xee080f17 if self.model == 2 else 0xee050f10)

    def result_access(self, va, op, permit, fsr, user=False, physical=0x10000100):
        """op 0 LDR, 1 STR; every denied write must preserve RAM."""
        index = len(self.expected)
        self.literal(8, 0x10040000 + index*16)
        self.literal(0, 0); self.emit(0xe5880000, 0xe5880008, 0xe588000c)
        self.literal(0, va); self.literal(1, 0x55667788)
        if user:
            self.literal(2, 0xd0); self.emit(0xe121f002)
        self.emit(0xe5801000 if op else 0xe5901000)
        if user: self.emit(0xef000000)
        if op:
            # An attempted write's observable result is the physical word, so
            # denied cached/user stores cannot pass by merely raising an abort.
            self.literal(2,physical);self.emit(0xe5921000)
        self.emit(0xe5881004)
        value = (0x55667788 if permit else 0x11223344) if op else (0x11223344 if permit else 0x55667788)
        self.expected.append([int(not permit), value,
                              0 if permit else fsr, 0 if permit else va])


def matrix(model):
    g = Matrix(model)
    # Full AP/S/R truth table in both modes; section domain 1 is client.
    allowed = [[0,0,0,0],[1,0,0,0],[1,0,1,0],[0,0,0,0]]
    for ap in range(4):
        for sr in range(4):
            for op in range(4):
                g.write(0x10004010, 0x10000022 | ap << 10)
                g.literal(0, 5); g.emit(0xee030f10)
                g.literal(0, 0x71 | sr << 8); g.emit(0xee010f10); g.flush()
                g.write(0x10000100, 0x11223344)
                permit = bool(allowed[sr][op]) if ap == 0 else op < 2 if ap == 1 else op != 3 if ap == 2 else True
                g.result_access(0x00400100, op & 1, permit, 0x1d, op >= 2)
    # All 16 domain fields/access encodings. Keep domain 0 available to code,
    # tables and result memory by testing it with aliases in domain 1 instead.
    for domain in range(1,16):
        for access in range(4):
            g.write(0x10004010, 0x10000002 | domain << 5)
            g.literal(0, 0x71); g.emit(0xee010f10)
            g.literal(0, 1 | access << (domain*2)); g.emit(0xee030f10); g.flush()
            g.write(0x10000100, 0x11223344)
            g.result_access(0x00400100, 0, access == 3, domain << 4 | (0xd if access == 1 else 9))
    # Exercise domain 0 with executable/result identity mappings in client
    # domain 1, so a no-access target does not stop the test driver itself.
    g.literal(0,5);g.emit(0xee030f10)
    g.write(0x10004000,0xc22);g.write(0x10004400,0x10000c22);g.flush()
    for access in range(4):
        g.write(0x10004010,0x10000002)
        g.literal(0,4|access);g.emit(0xee030f10);g.flush()
        g.write(0x10000100,0x11223344)
        g.result_access(0x00400100,0,access==3,0xd if access==1 else 9)
    g.literal(0,5);g.emit(0xee030f10)
    g.write(0x10004000,0xc02);g.write(0x10004400,0x10000c02);g.flush()
    # Small and large page subpermissions at actual translated hit addresses.
    for large in [False, True]:
        g.literal(0, 5); g.emit(0xee030f10)
        g.write(0x10004010, 0x10008021)
        descriptor = 0x10010000 | (3 << 4) | (2 << 6) | (1 << 8) | (1 if large else 2)
        for page in range(16 if large else 1): g.write(0x10008000+page*4, descriptor)
        g.flush()
        for sub in range(4):
            va = 0x00400000 + sub*(16384 if large else 1024)
            g.write(0x10010000+sub*(16384 if large else 1024), 0x11223344)
            g.result_access(va, 0, sub < 2, 0x1f, True)
            g.result_access(va, 1, sub == 0, 0x1f, True,0x10010000+sub*(16384 if large else 1024))
        for reserved in [0,3]:
            g.write(0x10008000, reserved); g.flush()
            g.result_access(0x00400000, 0, False, 0x17)
    # Prime a privileged cached hit, then actual LDRT/STRT must still fault.
    g.write(0x10004010, 0x10000422); g.flush()
    g.literal(0, 0x00400100); g.emit(0xe5901000)
    for opcode in [0xe4b01000, 0xe4a01000, 0xe4f01000, 0xe4e01000]:
        index = len(g.expected); g.literal(8, 0x10040000+index*16)
        g.write(0x10000100,0x11223344)
        g.literal(0,0x00400100);g.literal(1, 0x55); g.literal(2, 0); g.emit(0xe5882000,0xe5882008,0xe588200c,opcode)
        if not opcode & (1<<20):g.literal(2,0x10000100);g.emit(0xe5921000)
        g.emit(0xe5881004)
        g.expected.append([1,0x55 if opcode & (1<<20) else 0x11223344,0x1d,0x00400100])
    # Privileged mapping stays cached through descriptor mutation, changes on
    # model-specific invalidation, then TTBR and DACR change invalidate it too.
    g.write(0x10004010, 0x10000c22); g.flush(); g.write(0x10000100,0x11223344)
    g.result_access(0x00400100,0,True,0)
    g.write(0x10004010,0); g.result_access(0x00400100,0,True,0)
    g.flush(); g.result_access(0x00400100,0,False,5)
    g.write(0x10004010,3);g.flush();g.result_access(0x00400100,0,False,5)
    g.write(0x10004010,0x10000c22);g.flush();g.result_access(0x00400100,0,True,0)
    g.literal(0,1);g.emit(0xee030f10);g.result_access(0x00400100,0,False,0x19)
    g.literal(0,5);g.emit(0xee030f10);g.result_access(0x00400100,0,True,0)
    # Alternate empty TTBR has only identity code/results, no target section.
    g.write(0x1000c000,0xc02);g.write(0x1000c400,0x10000c02)
    g.literal(0,0x1000c000);g.emit(0xee020f10);g.result_access(0x00400100,0,False,5)
    g.literal(0,0x10004000);g.emit(0xee020f10);g.result_access(0x00400100,0,True,0)
    # Cached fetch crosses a 1 KiB permission boundary in one small page.
    g.write(0x10004010,0x10008021)
    g.write(0x10008000,0x10010072)  # AP3 first 1 KiB, AP1 next
    g.write(0x100103fc,0xe3a03007);g.write(0x10010400,0xe3a03009);g.flush()
    # Establish FAR/FSR from an ordinary denied page read, then prefetch must
    # retain it while recording the precise architectural LR.
    g.result_access(0x00400400,0,False,0x1f,True)
    index=len(g.expected);g.literal(8,0x10040000+index*16)
    g.literal(0,0);g.emit(0xe5880000,0xe5880004,0xe5880008,0xe588000c)
    g.literal(0,0x004003fc)
    continuation_literal=len(g.w)+2;g.literal(10,0)
    g.literal(2,0xd0);g.emit(0xe129f002,0xe1a0f000)
    g.w[continuation_literal]=len(g.w)*4
    g.expected.append([1,0x00400404,0x1f,0x00400400])
    index=len(g.expected);g.literal(8,0x10040000+index*16);g.emit(0xe5883000)
    g.literal(0,0);g.emit(0xe5880004,0xe5880008,0xe588000c)
    g.expected.append([7,0,0,0])
    index=len(g.expected);g.literal(8,0x10040000+index*16)
    g.literal(12,0x1003f020);g.emit(0xe59cb000,0xe588b000,0xe59cb004,0xe588b004)
    g.literal(0,0);g.emit(0xe5880008,0xe588000c)
    g.expected.append([0xd0,0xd7,0,0])
    # ARMv4 halfword vs architectural Undefined; destination stays unchanged.
    g.write(0x10000100,0xabcd1234)
    index=len(g.expected);g.literal(8,0x10040000+index*16)
    g.literal(0,0);g.emit(0xe5880000,0xe5880008,0xe588000c)
    g.literal(0,0x10000100);g.literal(3,0x55);g.emit(0xe1d030b0,0xe5883004)
    g.expected.append([int(model!=2),0x1234 if model==2 else 0x55,0,0])
    g.literal(0,0x70);g.emit(0xee010f10);g.write(0x10000100,0x11223344);g.result_access(0x10000100,0,True,0)
    # Exact data-abort LR/SPSR/mode from both SVC32 and USR32. Set all
    # PSR fields explicitly, so expectations come from ISA entry rules.
    g.literal(0,5);g.emit(0xee030f10)
    g.write(0x10004010,0x10000022);g.flush()
    g.literal(0,0x71);g.emit(0xee010f10)
    for psr in [0xd3,0xd0]:
        index=len(g.expected);g.literal(8,0x10040000+index*16)
        g.literal(0,0);g.emit(0xe5880000,0xe5880004,0xe5880008,0xe588000c)
        g.literal(0,0x00400100);g.literal(1,0x55)
        g.literal(2,psr);g.emit(0xe129f002)
        fault_pc=len(g.w)*4;g.emit(0xe5901000)
        if psr==0xd0:g.emit(0xef000000)
        g.literal(12,0x1003f000)
        for word,offset in [(0,4),(4,8),(8,12)]:
            g.emit(0xe59cb000|word,0xe588b000|offset)
        g.expected.append([1,fault_pc+8,psr,0xd7])
    # CP15 model ID and unsupported register read: Undefined, untouched Rd.
    index=len(g.expected);g.literal(8,0x10040000+index*16)
    g.literal(0,0);g.emit(0xe5880000,0xe588000c)
    g.literal(3,0x55);g.emit(0xee193f10,0xe5883004,0xee10bf10,0xe588b008)
    g.expected.append([1,0x55,[0x41560610,0x41007100,0x4401a102,0x41027100][model],0])
    # Successful completion marker; no firmware/host observation changes flow.
    g.write(0x1003fff0,0xa5);g.emit(0xeafffffe)
    return g.w,g.expected
