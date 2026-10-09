#!/usr/bin/env python3
"""Authored ADFS-style empty-drive control probe through Super I/O/IOMD.
No private firmware. Expected SMC control/status responses are declared here;
R1's existing synchronous data refusals are checked separately. GPL-2.0-only.
"""
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
from rpc_d2_rom import Guest

runner, core = sys.argv[1:3]
g = Guest()
g.init()
g.load(10, 0x03010fc0)  # SMC ports at four-byte MMIO spacing
expected = []


def observe(base, offset, value, mask=None):
    g.read(0, base, offset)
    if mask is not None:
        g.emit(0xe2000000 | mask)  # AND r0,r0,#mask
    g.store(0, 8, len(expected)*4)
    expected.append(value)


def write(port, value):
    g.mov(0, value)
    g.emit(0xe5ca0000 | ((port-0x3f0)*4))


def command(opcode, *parameters):
    write(0x3f5, opcode)
    for byte in parameters:
        write(0x3f5, byte)


def wait_irq():
    loop = len(g.words)
    g.read(0, 9, 0x24)
    g.emit(0xe3100010)
    g.branch(loop, 0)


def result(*bytes_):
    observe(10, 16, 0xd0)
    for byte in bytes_:
        observe(10, 20, byte)
    observe(10, 16, 0x80)


def sense(st0, pcn=None):
    command(8)
    observe(9, 0x24, 0)  # SIS acknowledges FINT; never generates an IRQ.
    result(st0, *([] if pcn is None else [pcn]))


# Reset polling and immediate control results. MSR must not clear reset FINT.
g.write(9, 0x28, 0x10)
write(0x3f2, 8)
observe(10, 16, 0)  # RQM stays low for the entire held-reset interval.
command(4, 0)       # Sense Drive Status must not execute while held.
command(0x0e)      # Neither can a parameterless DUMPREG.
observe(10, 16, 0)
observe(10, 20, 0)
# Wait at least 1 ms even at SA-110's maximum 100 MIPS: no completion IRQ
# or readiness until the guest explicitly releases the DOR reset bit.
for _ in range(10):
    g.delay(100)
observe(10, 16, 0)
observe(9, 0x24, 0)
write(0x3f2, 12)
wait_irq()
observe(10, 16, 0x80)
observe(9, 0x24, 0x10)
for drive in range(4):
    sense(0xc0 | drive, 0)
sense(0x80)
command(0x13, 0, 0x0d, 0)
observe(10, 16, 0x80)
# This control workload's SRT/horizon was authored at 500 kb/s. Program CCR
# explicitly rather than relying on the old, incorrect hardware-reset rate.
# rpc_fdc_test independently verifies the 02H/250 kb/s reset and retention.
write(0x3f7, 0)
command(3, 0xaf, 2)
observe(10, 16, 0x80)
observe(9, 0x24, 0)
write(0x3f2, 0x1c)
observe(10, 8, 0x1c)
observe(10, 28, 0x80)

# Empty media does not inhibit mechanical seek/recalibration/TRK0.
for drive, cylinder in [(0, 44), (0, 2), (1, 3)]:
    write(0x3f2, 0x1c | drive)  # Datasheet: DOR selects the physical carriage.
    command(15, drive, cylinder)
    wait_irq()
    observe(10, 16, 0x80)
    observe(9, 0x24, 0x10)
    sense(0x20 | drive, cylinder)
    command(4, drive)
    result(0x28 | drive)
    observe(9, 0x24, 0)
    observe(10, 28, 0x80)
# A software reset clears controller PCNs, but cannot teleport physical heads.
write(0x3f2, 8)
write(0x3f2, 12)
wait_irq()
for drive in range(4):
    sense(0xc0 | drive, 0)
for drive in [0, 1]:
    write(0x3f2, 0x1c | drive)
    command(4, drive)
    result(0x28 | drive)
write(0x3f2, 0x1c)
for drive in [0, 1, 2]:
    write(0x3f2, 0x1c | drive)
    command(7, drive)
    wait_irq()
    sense((0x20 if drive < 2 else 0x70) | drive, 0)
    command(4, drive)
    result((0x38 if drive < 2 else 0x28) | drive)
    observe(9, 0x24, 0)

# READ ID and READ DATA: retain the declared R1 no-media edge contract.
# An MSR poll cannot swallow FINT; first result read acknowledges it; no DMA.
write(0x3f2, 0x1c)
for opcode, parameters in [(0x4a, [0]), (0x0a, [0]),
                            (0x46, [4, 0, 1, 1, 2, 1, 0x1b, 0xff])]:
    command(opcode, *parameters)
    wait_irq()
    observe(10, 16, 0xd0)
    observe(9, 0x24, 0x10)
    observe(9, 0x30, 0, 1)
    observe(10, 20, 0x44 if opcode == 0x46 else 0x40)
    observe(9, 0x24, 0)
    for byte in [5, 0, 0, 0, 0, 0]:
        observe(10, 20, byte)
    observe(10, 16, 0x80)
    observe(9, 0x10, 0, 4)  # No media means no physical index pulses.
write(0x3f2, 12)
observe(10, 28, 0x80)
g.mov(0, 0xa5)
g.store(0, 8, len(expected)*4)
expected.append(0xa5)
# Exercise actual CPU IRQ delivery after the preceding level/poll checks.
# The IRQ handler records delivery and masks IRQB without touching the FIFO.
command(0x0a, 0)
g.mov(0, 0x53)
g.emit(0xe121f000)  # SVC32, IRQ enabled and FIQ masked
loop = len(g.words)
g.emit(0xeafffffe)
handler = len(g.words)
g.words[6] = 0xea000000 | ((handler-6-2) & 0xffffff)
g.mov(11, 0x5a)
g.store(11, 8, len(expected)*4)
expected.append(0x5a)
g.mov(11, 0)
g.store(11, 9, 0x28)
g.emit(0xe25ef004)
rom = struct.pack('<'+'I'*len(g.words), *g.words)
with tempfile.TemporaryDirectory(prefix='arc-fdc-boot-') as temp:
    d = Path(temp)
    (d/'authored.rom').write_bytes(rom + bytes(2097152-len(rom)))
    (d/'seed').write_bytes(bytes(256))
    for model in ['arm610', 'arm710', 'sa110', 'arm7500']:
        (d/'probe.arcplan').write_text(f'version=1\nmodel={model}\nfirmware=authored.rom\ncmos=seed\nseed=seed\nrtc_epoch=0\n')
        r = subprocess.run([runner, '--core', core, '--plan', str(d/'probe.arcplan'),
            # Reset-held delay now includes ROM service; the declared mechanical
            # seeks include D-144's 79-pulse absent-drive recalibration bound.
            '--system', str(d), '--frames', '180', '--ram-offset', '131072',
            '--ram-words', str(len(expected))], capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        words = [int(w, 16) for w in re.findall(r'^ram=(.*)$', r.stdout, re.M)[-1].split(',')]
        assert words == expected, (model, [(i, a, b) for i, (a, b) in
            enumerate(zip(words, expected)) if a != b])
print('FDC boot probe: four models, reset/SIS/control results, mechanical TRK0, DIR, index and IOMD IRQ/DMA passed')
