#!/usr/bin/env python3
"""Retirement through retro_run, with the first post-consume instruction as oracle.
Authored firmware and media only. GPL-2.0-only.
"""
from pathlib import Path
import hashlib, struct, subprocess, sys, tempfile
from rpc_boot_packages_test import inputs
from rpc_controller_stub import Arm
from rpc_launch_qualification import disc
from rpc_video_rom import Guest


def firmware(image):
    a = Arm(); a.w = []
    a.branch('main'); a.emit(0xeafffffe)
    # SWI exception entry executes no intervening guest instruction. R9 is DIR;
    # read its change latch in the VERY FIRST instruction after the consume.
    a.emit(0xe5d98000, 0xe58a8004); a.branch('retired')
    while len(a.w) < 64: a.emit(0xeafffffe)

    def write8(address, value):
        a.literal(0, address); a.literal(1, value); a.emit(0xe5c01000)

    def wait(us):
        g = Guest(); g.wait(us); a.emit(*g.w)

    def sector(number, destination, label):
        for value in (0x46, 1, 0, 0, number, 1, number, 0x35, 255):
            write8(0x03010fd4, value)
        a.literal(4, 0x03010fd0); a.literal(5, 0x03010fd4)
        a.literal(6, 256); a.literal(7, destination)
        a.label(label)
        a.emit(0xe5d40000, 0xe20000f0, 0xe35000f0)
        a.branch(label, condition=1)
        a.emit(0xe5d50000, 0xe4c70001, 0xe2566001)
        a.branch(label, condition=1)
        a.label(label+'-result')
        a.emit(0xe5d40000, 0xe20000f0, 0xe35000d0)
        a.branch(label+'-result', condition=1)
        for _ in range(7): a.emit(0xe5d50000)

    a.label('main'); a.literal(0, 0x70); a.emit(0xee010f10)
    a.literal(0, 0xd3); a.emit(0xe121f000); a.literal(13, 0x10070000)
    for i in range(0, len(image), 4):
        g = Guest(); g.write(0x10020000+i, struct.unpack_from('<I', image, i)[0]); a.emit(*g.w)
    write8(0x03010fc8, 0x3d); wait(2000)  # Drive 1, motor on, reset released.
    for value in (3, 0xf1, 3): write8(0x03010fd4, value)  # Non-DMA.
    write8(0x03010fdc, 2)
    for cylinder in (1, 0):
        for value in (15, 1, cylinder): write8(0x03010fd4, value)
        wait(4000)  # Actual STEP clears the original mount's change latch.
    sector(1, 0x10031010, 'controller-map')
    a.literal(9, 0x03010fdc); a.literal(10, 0x10031000)
    a.emit(0xe5d98000, 0xe58a8000)  # Must be clear before consume.
    status = struct.unpack_from('<I', image, 44)[0]  # Controller plan offset.
    # The generated status block immediately precedes its plan.
    status -= 64
    g = Guest(); g.write(0x10020000+status+32, 1); g.write(0x10020000+status+36, 1); a.emit(*g.w)
    query = image.index(struct.pack('<I', 0xef020006))
    a.literal(0, 0xd0); a.emit(0xe121f000); a.literal(13, 0x10068000)
    for reg, value in enumerate((0, 1, 0, 0x10020000+status, 0x53435241, 64, 1)):
        a.literal(reg, value)
    a.literal(15, 0x10020000+query)

    a.label('retired')
    sector(1, 0x10031110, 'noop-map')
    sector(7, 0x10031210, 'noop-code')
    g = Guest(); g.write(0x10031008, 1); a.emit(*g.w)
    a.label('done'); a.branch('done')
    data = a.finish()
    return data + struct.pack('<I', 0xeafffffe)*((2097152-len(data))//4)


def main():
    runner, core, builder, noop_builder = sys.argv[1:5]
    with tempfile.TemporaryDirectory(prefix='arc-launch-frame-') as temp:
        root = Path(temp); system = inputs(root)
        raw = bytes(disc('returning')); media = system/'launch.adl'; media.write_bytes(raw)
        digest = hashlib.sha256(b'ArcDroid RPC ordered media 1\0'+struct.pack('<Q', len(raw))+raw).digest()
        target = b'ADFS::0.$.!Boot'
        wire = struct.pack('<8sIIII32sII', b'ARCRLPLN', 2, 64+len(target), 1, 0, digest, len(target), 0)+target
        plan = root/'plan.bin'; plan.write_bytes(wire); helper = root/'controller.bin'
        subprocess.run([builder, '--bound', str(helper), str(plan)], check=True, capture_output=True)
        noop = root/'noop.adl'
        subprocess.run([noop_builder, '--write', str(noop)], check=True, capture_output=True)
        (root/'authored.rom').write_bytes(firmware(helper.read_bytes()))
        seed = bytearray(256); seed[199] = 2; seed[63] = 3; (root/'seed').write_bytes(seed)
        for model in ('arm610', 'arm710', 'sa110', 'arm7500'):
            for cells in (0, 3):
                save = root/f'{model}-{cells}'; save.mkdir()
                case = root/'case.arcplan'
                case.write_text(f'version=2\nmodel={model}\nfirmware={root/"authored.rom"}\ncmos=persist\nseed={root/"seed"}\ncmos_layout=ro370\ncontent_id={"a"*40}\nrtc_epoch=946684800\nhd_populate=on\nmedia={media}\nlaunch_action=boot\nlaunch_session={0x123456789abcdef}\nobserver_cells={cells}\n')
                result = subprocess.run([runner, '--core', core, '--plan', str(case), '--system', str(system),
                    '--save', str(save), '--disk-writes', 'off', '--frames', '60',
                    '--ram-offset', str(0x31000), '--ram-words', '196'], capture_output=True, text=True, timeout=180)
                assert result.returncode == 0, (model, cells, result.stderr[-2000:])
                line = [line for line in result.stdout.splitlines() if line.startswith('ram=')][-1]
                words = [int(w, 16) for w in line[4:].split(',')]
                assert words[:3] == [0, 0x80, 1], (model, cells, 'first instruction saw an unretired mount', words[:3], result.stdout[-2500:], result.stderr[-2500:])
                data = struct.pack('<196I', *words)
                assert data[0x10+251:0x10+253] == b'\x45\x41', 'controller OldId'
                assert data[0x110+251:0x110+253] == b'\x44\x41', 'no-op OldId'
                assert data[0x210:0x210+256] == noop.read_bytes()[7*256:8*256], (model, cells, 'mounted no-op bytes')
                assert 'delivery=2 ' in result.stderr, (model, cells, 'consume was not applied', result.stderr[-2000:])
                print(model, cells, 'retro_run: first post-consume instruction sees retirement; mounted no-op verified', flush=True)


if __name__ == '__main__': main()
