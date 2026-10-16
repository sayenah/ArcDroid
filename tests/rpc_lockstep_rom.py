#!/usr/bin/env python3
"""Build-time authored lockstep directed programs. GPL-2.0-only; no ROM inputs."""
import struct
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from rpc_plan_matrix_rom import Matrix
from rpc_timing_rom import Guest
from rpc_lockstep_expect import FEATURES, MODELS, ROWS, domain, supports
from rpc_lockstep_schedule import sample


class Corpus(Matrix):
    def __init__(self, model):
        super().__init__(model)
        self.markers = {}
        self.rompatch = {}

    def normal(self):
        self.literal(0, 0x70); self.emit(0xee010f10)
        self.literal(0, 0xd3); self.emit(0xe121f000)

    def record(self, reg, expected):
        address = 0x10040000 + 16*len(self.expected)
        self.literal(8, address); self.emit(0xe5880000 | reg << 12)
        self.literal(0, 0); self.emit(0xe5880004, 0xe5880008, 0xe588000c)
        self.expected.append([expected, 0, 0, 0])

    def marker(self, feature):
        self.literal(1, 1)
        self.markers[feature] = len(self.expected)*4
        self.record(1, 1)

    def call(self, address):
        offset = len(self.w)+2; self.literal(11, 0)
        self.literal(12, address); self.emit(0xe1a0f00c)
        self.w[offset] = len(self.w)*4


def modes(g, value):
    # A hot User32 loop masks attempted I/F changes in an MSR CPSR write.
    g.normal(); g.literal(0, 0xd0); g.emit(0xe121f000)
    g.literal(0, 0x10); g.emit(0xe121f000, 0xe10f1000)
    g.emit(0xe20110ff)
    g.emit(0xef000000)  # Matrix's SWI handler returns SVC32 with old I/F.
    g.record(1, 0xd0)
    # Explicit SVC26 then SVC32 transitions; 26-bit R15 carry has its own item.
    g.literal(0, 0x70); g.emit(0xee010f10)
    g.literal(0, 0xc3); g.emit(0xe121f000)
    g.emit(0xe10f1000, 0xe20110ff); g.record(1, 0xc3)
    g.normal()
    # User26 R15 writes in a cacheable hot body can change NZCV, never I/F
    # or mode. TEQP requests all of them; only its top four bits may change.
    body=[0xe132f003]*16+[0xe10f1000,0xe1a0f00b]
    for i,word in enumerate(body):g.write(0x10016000+4*i,word)
    g.write(0x10004010,0x10000c0a)
    g.literal(0,1);g.emit(0xee030f10)
    g.literal(0,0x1075 if g.model==2 else 0x75);g.emit(0xee010f10);g.flush()
    g.literal(2,0);g.literal(3,0xfc000003)
    g.literal(0,0x80);g.emit(0xe121f000)
    g.call(0x00416000);g.emit(0xef000000);g.record(1,0xf0000080)
    g.normal()


def domains(g, value):
    g.normal()
    g.write(0x10004010, 0x10000c22)
    for access in (0, 1, 2, 3):
        g.literal(0, 1 | access << 2); g.emit(0xee030f10)
        g.literal(0, 0x71); g.emit(0xee010f10); g.flush()
        g.write(0x10000100, 0x11223344)
        permit, fsr = domain(access)
        g.result_access(0x00400100, 0, permit, fsr)
    g.normal()


def permission_hit(g, value):
    g.normal(); g.write(0x10004010, 0x10000422)
    g.literal(0, 5); g.emit(0xee030f10)
    g.literal(0, 0x71); g.emit(0xee010f10); g.flush()
    g.write(0x10000100, 0x11223344)
    g.literal(0, 0x00400100); g.emit(0xe5901000)  # privileged hit warms AP=1
    g.result_access(0x00400100, 0, False, 0x1d, user=True)
    g.normal()


def carry26(g, value):
    g.normal()
    # Enter at the last eight words of the 26-bit VA aperture. Replace the
    # low section from that high page, flush there, and return through a ROM
    # trampoline whose physical and virtual addresses coincide at MMU-off.
    address = 0x10040000 + 16*len(g.expected)
    g.literal(8, address)
    g.write(0x100040fc, 0x10000c02)
    g.write(0x14000000, 0xea003ffe)  # VA 0 -> VA 0x10000
    g.write(0x14010000, 0xee010f10)  # disable MMU using r0=0x70
    g.rompatch.update({0x10004:0xe3a000d3, 0x10008:0xe121f000,
                       0x1000c:0xe5881000, 0x10010:0xe1a0f00a})
    body = [0xe5845000, 0xee080f17 if g.model == 2 else 0xee050f10] + [0xe1a00000]*5 + [0xe1a0100f]
    for i, word in enumerate(body): g.write(0x100fffe0+4*i, word)
    g.literal(4, 0x10004000); g.literal(5, 0x14000c02)
    g.literal(0, 1); g.emit(0xee030f10)
    g.literal(0, 0x71); g.emit(0xee010f10); g.flush()
    g.literal(0, 0); g.emit(0xe128f000)  # clear NZCV before the raw carry
    g.literal(0, 0x83); g.emit(0xe121f000)
    g.literal(0, 0x70)
    ret = len(g.w)+2; g.literal(10, 0); g.literal(12, 0x03ffffe0); g.emit(0xe1a0f00c)
    g.w[ret] = len(g.w)*4
    g.expected.append([0x0c000007, 0, 0, 0])
    g.normal(); g.write(0x10004000, 0xc02)


def fetch_evict(g, value, denied=False):
    g.normal()
    # These two VAs hash to the same direct-mapped TLB slot. The first
    # instruction changes its own descriptor; only the colliding data install
    # makes the next fetch observe the replacement physical page.
    va, alias = 0x00400100, 0x10500100
    address = 0x10040000 + 16*len(g.expected)
    for bank, result in ((0x10000000, 1), (0x14000000, 2)):
        for i, word in enumerate((0xe5801000, 0xe5992000, 0xe3a03000|result, 0xe1a0f00a)):
            g.write(bank+0x100+4*i, word)
    g.write(0x10004010, 0x10000c02)
    g.write(0x10004414, 0x14000c22 if denied else 0x14000c02)
    g.literal(8, address); g.literal(0, 0); g.emit(0xe5880000,0xe5880008,0xe588000c)
    g.literal(0, 1); g.emit(0xee030f10)
    g.literal(0, 0x71); g.emit(0xee010f10); g.flush()
    g.literal(0, 0x10004010); g.literal(1, 0x14000c02); g.literal(9, alias)
    ret = len(g.w)+2; g.literal(10, 0); g.literal(12, va); g.emit(0xe1a0f00c)
    g.w[ret] = len(g.w)*4
    g.literal(8, address); g.emit(0xe5883004)
    g.expected.append([1 if denied else 0, 2, 0x19 if denied else 0, alias if denied else 0])
    g.normal()


def smc(g, value, form):
    g.normal()
    pairs = [(0x10010000, 0x12010000), (0x11010000, 0x13010000)]
    if g.model != 3: pairs.append((0x02010000, 0x02210000))
    for execute, alias in pairs:
        g.write(execute, 0xe3a03001); g.write(execute+4, 0xe1a0f00b)
        g.call(execute); g.record(3, 1)
        opcode = 0xe3a03000 | value
        g.literal(0, alias); g.literal(1, opcode)
        g.emit({'str':0xe5801000, 'strb':0xe5c01000, 'stm':0xe8800002,
                'strt':0xe4a01000, 'swp':0xe1002091}[form])
        g.call(execute); g.record(3, value)


def pc_next(g, value):
    g.normal()
    # Store into the next instruction, inside the same contiguous RAM body.
    code = 0x10011000
    for i, opcode in enumerate((0xe50f1004, 0xe3a03001, 0xe1a0f00b)):
        g.write(code+4*i, opcode)
    g.literal(1, 0xe3a03000 | value); g.call(code); g.record(3, value)


def partial_stm(g, value):
    g.normal()
    g.write(0x10004018, 0x10008001)
    g.write(0x10008000, 0x10020ff2); g.write(0x10008004, 0)
    g.literal(0, 1); g.emit(0xee030f10)
    g.literal(0, 0x71); g.emit(0xee010f10); g.flush()
    g.literal(8, 0x10040000+16*len(g.expected))
    g.literal(0, 0); g.emit(0xe5880000,0xe5880004,0xe5880008,0xe588000c)
    for reg, number in ((1,17),(2,34),(3,51),(4,68),(9,0x00600ff8)): g.literal(reg,number)
    g.emit(0xe8a9001e)
    g.expected.append([1,0,7,0x00601000]); g.normal()
    for offset, expected in ((0,17),(4,34),(8,0),(12,0)):
        g.literal(0,0x10020ff8+offset);g.emit(0xe5901000);g.record(1,expected)


def va_key(g, value):
    g.normal()
    body = (0xe28f3000, 0xeb000001, 0xea000002, 0xe1a00000,
            0xe1a0400e, 0xe1a0f00e, 0xe59f5000, 0xe1a0f00b, 0x71)
    for i, word in enumerate(body): g.write(0x10018000+4*i, word)
    g.write(0x10004010, 0x10000c02); g.write(0x10004014, 0x10000c02)
    g.literal(0, 1); g.emit(0xee030f10)
    g.literal(0, 0x71); g.emit(0xee010f10); g.flush()
    for va in (0x00418000, 0x00518000):
        g.call(va); g.record(3, va+8); g.record(4, va+8); g.record(5, 0x71)
    g.normal(); g.call(0x12018000)
    g.record(3, 0x12018008); g.record(4, 0x12018008); g.record(5, 0x71)


def maintenance(g, value):
    g.normal()
    code = 0x10012000
    for i, opcode in enumerate((0xe3a0302a, 0xee070f17 if g.model == 2 else 0xee070f10, 0xe1a0f00b)):
        g.write(code+4*i, opcode)
    g.write(0x10004000, 0xc0a); g.write(0x10004400, 0x10000c0a)
    g.literal(0, 1); g.emit(0xee030f10)
    g.literal(0, 0x1075 if g.model == 2 else 0x75); g.emit(0xee010f10); g.flush()
    for _ in range(32): g.call(code)
    g.record(3, 42); g.normal()
    g.write(0x10004000, 0xc02); g.write(0x10004400, 0x10000c02)


def cache_conflict(g, value):
    g.normal()
    code = 0x10001000
    # The code and every data line use set zero. Round-robin replacement
    # eventually evicts the executing line on unified-cache models.
    sets = (4, 128, 16, 64)[g.model]
    ways = (64, 4, 32, 4)[g.model]
    stride = sets * (32 if g.model == 2 else 16)
    for i, word in enumerate((0xe4991000 | stride, 0xe2577001, 0x1afffffc, 0xe1a0f00b)):
        g.write(code+4*i, word)
    g.write(0x10004000, 0xc0a); g.write(0x10004400, 0x10000c0a)
    g.write(0x10004404, 0x10100c0a)
    g.literal(0, 1); g.emit(0xee030f10)
    g.literal(0, 0x1075 if g.model == 2 else 0x75); g.emit(0xee010f10); g.flush()
    g.literal(9, 0x10100000); g.literal(7, 4*(ways+2))
    g.call(code); g.record(7, 0)
    g.normal(); g.write(0x10004000, 0xc02); g.write(0x10004400, 0x10000c02)


def interrupts(g, value):
    g.normal()
    # FIQ vector stores a monotonic count then clears the force-bit mask.
    g.w[7] = 0xea000067  # vector 0x1c -> handler at 0x1c0
    g.w[112:122] = [0xe59f0018, 0xe3a01000, 0xe5801000, 0xe59f0010,
                   0xe5901000, 0xe2811001, 0xe5801000, 0xe25ef004,
                   0x03200038, 0x1003e000]
    g.write(0x1003e000, 0); g.write(0x03200038, 128)
    g.literal(0, 0x93); g.emit(0xe121f000)  # unmask F, pending forced FIQ
    g.literal(0, 0x1003e000); g.emit(0xe5901000); g.record(1, 1)
    g.normal()


def irq_ordinal(g, value):
    g.normal()
    address = 0x10040000 + 16*len(g.expected)
    g.w[6] = 0xea000062  # IRQ vector at 0x18 -> 0x1a8
    # Keep this handler separate from the FIQ and abort handlers.
    g.w[106:110] = [0xe5886000, 0xe588e004, 0xe5801000, 0xe25ef004]
    g.write(0x03200018, 128)
    g.literal(8, address); g.literal(0, 0x03200018); g.literal(1, 0)
    g.literal(6, 0); g.literal(5, 0x53)
    boundary = len(g.w)*4
    g.emit(0xe121f005, 0xe2866001)
    g.expected.append([0, boundary+8, 0, 0])
    g.record(6, 1); g.normal()


def masked_refresh(g, value):
    g.normal(); g.write(0x03200018, 128)
    g.literal(0, 0x83); g.emit(0xe121f000)
    g.literal(2, 0); g.literal(3, 0x0c000003)
    g.emit(0xe132f003)  # TEQP changes R15 I/F, leaves reg[16] until event check


def buffer(g, value):
    g.normal()
    # B=1/C=0 section; 16 stores in one STM generate real buffered requests.
    g.write(0x10004804, 0x10100c06)
    g.literal(0, 3); g.emit(0xee030f10)
    g.literal(0, 0x10004000); g.emit(0xee020f10)
    g.literal(0, 0x79); g.emit(0xee010f10); g.flush()
    g.literal(9, 0x20100000); g.literal(1, value)
    g.emit(*([0xe8890002]*12))
    g.emit(0xe5992000); g.record(2, value)
    g.normal()


def clock(g, value):
    g.normal(); g.emit(0xee0f0f51, 0xee0f0f52)


def timing_registers(g, value):
    g.normal()
    code=0x10013000
    body=[0x15893000]*12
    if g.model==3:body.append(0x158c2000)
    body.append(0x158a1000)
    body.append(0xe1a0f00b)
    for i,word in enumerate(body):g.write(code+4*i,word)
    g.write(0x10004000,0xc0a);g.write(0x10004400,0x10000c0a)
    g.write(0x10004804,0x10100c06);g.write(0x100040c8,0x03200c02)
    g.literal(0,1);g.emit(0xee030f10)
    g.literal(0,0x107d if g.model==2 else 0x7d);g.emit(0xee010f10);g.flush()
    if g.model==2:g.emit(0xee0f0f51)
    g.literal(9,0x20100000);g.literal(10,0x03200080);g.literal(12,0x0320003c)
    g.literal(1,0x19);g.literal(2,7);g.literal(3,value)
    g.emit(0xe1500000);g.call(code)  # warm without requests
    # call() uses r12 for its target, so reload the clock-register address.
    g.literal(12,0x0320003c)
    for repetition in range(32 if g.model==3 else 1):
        if g.model==3:g.literal(2,7 if repetition%2==0 else 0)
        ret=len(g.w)+2;g.literal(11,0);g.literal(0,code);g.emit(0xe3500001,0xe1a0f000)
        g.w[ret]=len(g.w)*4
    g.normal();g.write(0x10004000,0xc02);g.write(0x10004400,0x10000c02)
    # This ROM instruction's own queued ROM fetch is repriced by its store.
    g.write(0x03200080,0x1a);g.write(0x03200080,0x19)
    g.literal(0, 0x03200080); g.emit(0xe5901000); g.record(1, 0x19)


def idle(g, value):
    g.normal()
    code=0x10014000
    body=[0x15893000]*12+[0x1e0f0f58 if g.model==2 else 0x158a1000,0xe1a0f00b]
    for i,word in enumerate(body):g.write(code+4*i,word)
    g.write(0x10004000,0xc0a);g.write(0x10004400,0x10000c0a)
    g.write(0x10004804,0x10100c06);g.write(0x100040c8,0x03200c02)
    g.literal(0,1);g.emit(0xee030f10)
    g.literal(0,0x107d if g.model==2 else 0x7d);g.emit(0xee010f10);g.flush()
    if g.model==2:g.emit(0xee0f0f51)
    g.literal(9,0x20100000);g.literal(10,0x0320001c)
    g.literal(1,1);g.literal(3,value)
    g.emit(0xe1500000);g.call(code)
    # Timer0 fires after (999+1)/2 MHz = 500 us, well after buffered issue.
    g.write(0x03200040,231);g.write(0x03200044,3)
    g.write(0x03200014,0x20);g.write(0x03200018,0x20);g.write(0x03200048,0)
    g.literal(1,1);g.emit(0xe3500001);g.call(code)
    g.emit(0xe5992000);g.record(2,value)
    g.normal();g.write(0x10004000,0xc02);g.write(0x10004400,0x10000c02)
    # A pending force-bit IRQ wakes while I remains masked as well.
    g.write(0x03200018,0x80)
    if g.model==2:g.emit(0xee0f0f58)
    else:g.write(0x0320001c,1)
    g.write(0x03200018,0)


def prefetch(g, value):
    g.normal()
    g.write(0x10004010, 0)
    g.literal(0, 1); g.emit(0xee030f10)
    g.literal(0, 0x71); g.emit(0xee010f10); g.flush()
    g.result_access(0x00400100, 0, False, 5)
    # Matrix's prefetch handler records the abort and resumes via r10.
    g.literal(8, 0x10040000+16*len(g.expected)); g.literal(0, 0)
    g.emit(0xe5880000, 0xe5880008, 0xe588000c)
    g.literal(0, 1); g.emit(0xee030f10)
    g.literal(0, 0x71); g.emit(0xee010f10); g.flush()
    ret = len(g.w)+2; g.literal(10, 0); g.literal(12, 0x00400000); g.emit(0xe1a0f00c)
    g.w[ret] = len(g.w)*4
    # LR of a prefetch abort is offending VA+4; FAR/FSR retain preceding data fault.
    g.expected.append([1, 0x00400004, 5, 0x00400100])
    g.normal()


def delay_zero(g, value, take_irq=False):
    g.normal()
    g.write(0x03010fc8, 0x0c)  # DOR: release reset, enable controller IRQ
    g.wait(1000)               # reset completion is specified at 500 us
    fifo=0x03010fd4
    for unit in range(4):
        g.literal(0,fifo);g.literal(1,8);g.emit(0xe5c01000,0xe5d02000,0xe5d02000)
    g.write(0x03200028,0x10)
    g.literal(9,0x03200024);g.literal(0,fifo)
    g.literal(1,15);g.emit(0xe5c01000)
    g.literal(1,0);g.emit(0xe5c01000)
    # SEEK 0 -> 0 schedules step_time(0)==0. Its IRQ must precede the
    # immediately following guest read, despite the final parameter's MMIO.
    if take_irq:
        g.w[6]=0xea000062
        g.w[106:110]=[0xe5886000,0xe588e004,0xe5801000,0xe25ef004]
        address=0x10040000+16*len(g.expected)
        g.write(0x03200018,0)
        g.literal(8,address);g.literal(0,0);g.emit(0xe5880000,0xe5880004,0xe5880008,0xe588000c)
        g.literal(10,fifo);g.literal(0,0x03200028);g.literal(1,0)
        g.literal(6,0);g.literal(5,0x53);g.emit(0xe121f005)
        boundary=len(g.w)*4
        g.emit(0xe5ca1000,0xe2866001)
        g.expected.append([1,boundary+12,0,0])
        g.emit(0xe5992000);g.record(2,0)
        g.normal()
    else:
        g.emit(0xe5c01000,0xe5992000);g.record(2,0x10)
    g.write(0x03200028,0)


def burst_rom(g, value):
    g.normal();g.write(0x03200080,0x19)
    g.emit(0xee070f17 if g.model==2 else 0xee070f10)
    body=[0xe3a03000]+[0xe2833001]*32+[0xe1a0f00b]
    for i,word in enumerate(body):g.rompatch[0x30000+4*i]=word
    g.call(0x30000);g.record(3,32)



BUILDERS = {'modes': modes, 'domains': domains, 'permission-hit': permission_hit,
            'carry26': carry26, 'fetch-evict': fetch_evict,
            'denied-evict': lambda g, v: fetch_evict(g, v, True),
            'pc-next': pc_next, 'maintenance': maintenance, 'interrupts': interrupts,
            'va-key': va_key, 'irq-ordinal': irq_ordinal, 'masked-refresh': masked_refresh,
            'partial-stm': partial_stm,
            'delay-zero': delay_zero,
            'irq-event': lambda g,v: delay_zero(g,v,True),
            'burst-rom': burst_rom,
            'buffer': buffer, 'clock': clock, 'timing-registers': timing_registers,
            'cache-conflict': cache_conflict,
            'idle': idle, 'prefetch': prefetch}


def build(model, seed, features=FEATURES, omit=None):
    g = Corpus(model)
    chosen = sorted(features, key=lambda feature: (sample(seed, model, FEATURES.index(feature)), feature))
    for feature in chosen:
        if feature == 'masked-refresh': continue
        if not supports(feature, model): continue
        if feature == omit:
            g.markers[feature] = len(g.expected)*4
            g.expected.append([1, 0, 0, 0])
            # Reserve the signature slot without producing its realised marker.
            continue
        value = 2 + sample(seed, model, FEATURES.index(feature)) % 126
        if feature.startswith('smc-'): smc(g, value, feature[4:])
        else: BUILDERS[feature](g, value)
        g.marker(feature)
    g.normal(); g.literal(1, 0x600d); g.record(1, 0x600d)
    if 'masked-refresh' in features:
        if omit != 'masked-refresh': masked_refresh(g, 0); g.marker('masked-refresh')
        else:
            g.markers['masked-refresh'] = len(g.expected)*4
            g.expected.append([1, 0, 0, 0])
    g.emit(0xeafffffe)
    if g.rompatch:
        while len(g.w)*4 <= max(g.rompatch): g.w.append(0xeafffffe)
        for address, word in g.rompatch.items(): g.w[address//4] = word
    return g.w, [x for record in g.expected for x in record], g.markers


def write_item(directory, name, model, words):
    data = struct.pack('<'+'I'*len(words), *words)
    if len(data) > 2097152: raise ValueError('authored ROM exceeds 2 MiB')
    (directory/(name+'.rom')).write_bytes(data + bytes(2097152-len(data)))
    (directory/(name+'.arcplan')).write_bytes(('version=1\nmodel='+MODELS[model]+'\nfirmware='+name+'.rom\ncmos=seed\nseed=authored.seed\nrtc_epoch=0\n').encode('utf-8'))


def directed(directory):
    items = []
    for model in range(4):
        for row, features in ROWS.items():
            words, expected, markers = build(model, 0, features)
            name = row.lower()+'-'+str(model)
            write_item(directory, name, model, words)
            items.append(dict(name=name, model=model, row=row, expected=expected, markers=markers, fatal=False,
                              terminal_cpsr=0xc3 if 'masked-refresh' in markers else None))
        # True mid-body fatal, from illegal mode 0x14, after an authored RAM commit.
        g = Guest(); g.literal(0,0x70); g.emit(0xee010f10)
        g.store(0x10040000, 0xface); g.literal(0,0x14); g.emit(0xe121f000)
        name = 'fatal-mode-'+str(model); write_item(directory,name,model,g.words)
        items.append(dict(name=name,model=model,row='E8',expected=[0xface],markers={},fatal=True,reason='Bad mode',
                          ram_evidence='awaiting emitter',terminal_registers={'0':0x14,'1':0xface,'15':(len(g.words)-1)*4+8,'16':0x14}))
        # F is cleared by the *raw* 26-bit PC increment after the TLB flush.
        # The immediately following prefetch abort must skip the event check.
        g = Corpus(model); g.normal()
        slot = 0x10040000+16*len(g.expected)
        g.write(slot, 0); g.write(slot+4, 0)
        g.w[3] = 0xe588e000  # first ABT instruction commits LR before FIQ
        g.w[4] = 0xea007ffa  # VA 0x10 -> ROM 0x20000 after FIQ returns
        g.w[7] = 0xea000067
        g.w[112:123] = [0xe59f001c,0xe5901000,0xe5801004,
                        0xe59f0014,0xe3a01000,0xe5801000,0xe25ef004,
                        0xe1a00000,0xe1a00000,slot,0x03200038]
        g.rompatch.update({0x20000:0xe3a00070,0x20004:0xee010f10,
                           0x20008:0xe3a000d3,0x2000c:0xe121f000,0x20010:0xeafffffe})
        g.write(0x100040fc,0x10000c02)
        g.write(0x100ffff0,0xe5845000)
        g.write(0x100ffff4,0xee080f17 if model==2 else 0xee050f10)
        g.literal(8,slot);g.literal(4,0x100040fc);g.literal(5,0)
        g.write(0x03200038,128)
        g.literal(0,1);g.emit(0xee030f10)
        g.literal(0,0x71);g.emit(0xee010f10);g.flush()
        g.literal(0,0x43);g.emit(0xe121f000)
        g.literal(12,0x03fffff0);g.emit(0xe1a0f00c)
        g.expected.append([0x03fffffc,0x03fffffc,0,0])
        while len(g.w)*4<=max(g.rompatch):g.w.append(0xeafffffe)
        for address,word in g.rompatch.items():g.w[address//4]=word
        name='prefetch-fiq-'+str(model);write_item(directory,name,model,g.w)
        items.append(dict(name=name,model=model,row='G-3/Q-8',expected=[x for r in g.expected for x in r],markers={},fatal=False))
        # Invalid first-level descriptors in physical SuperIO space. Every
        # prefetch abort re-walks that MMIO dispatch, without timing the walk.
        # The before-walk marker is independently known; exact walk chronology
        # is differential-only until the M2b physical-dispatch log exists.
        g=Guest();g.literal(0,0x70);g.emit(0xee010f10)
        g.literal(0,0xd3);g.emit(0xe121f000);g.store(0x10040000,0xcafe)
        g.literal(0,0x03010000);g.emit(0xee020f10)
        g.literal(0,3);g.emit(0xee030f10)
        g.literal(0,0x71);g.emit(0xee010f10,0xeafffffe)
        name='walk-mmio-'+str(model);write_item(directory,name,model,g.words)
        items.append(dict(name=name,model=model,row='G-3/G-4 walks',expected=[0xcafe],markers={},fatal=False,
                          terminal_cpsr=0xd7,walk_mmio=True,differential_only='Exact MMIO walk ordering needs the M2b event stream; marker and ABT mode have independent expectations.'))
    return items
