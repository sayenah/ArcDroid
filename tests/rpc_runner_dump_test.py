#!/usr/bin/env python3
"""Exact RGB PPM captures through the real frontend, canonical trace unchanged. GPL-2.0-only."""
import csv
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from rpc_video_goldens import colour

runner, core, directory = sys.argv[1:4]
args = [runner, '--core', core, '--plan', str(Path(directory)/'video-2-palette.arcplan'),
        '--system', directory, '--frames', '2']
with tempfile.TemporaryDirectory(prefix='arc-rpc-dump-') as temp:
    d = Path(temp)
    plain = subprocess.run(args, capture_output=True, check=True)
    result = subprocess.run(args + ['--dump-frame', '0', str(d/'first.ppm'),
        '--dump-frame', '1', str(d/'second.ppm'), '--dump-frame', '0', str(d/'again.ppm'),
        '--timing', str(d/'timing.csv')], capture_output=True, check=True)
    assert result.stdout == plain.stdout, 'frame dumping changed canonical output'
    assert (d/'first.ppm').read_bytes() == (d/'again.ppm').read_bytes()
    headers = re.findall(rb'^video=(\d+)x(\d+) hash=([0-9a-f]+)', result.stdout, re.M)
    for i, name in enumerate(['first', 'second']):
        magic, dims, maximum, rgb = (d/(name+'.ppm')).read_bytes().split(b'\n', 3)
        width, height = map(int, dims.split())
        assert magic == b'P6' and maximum == b'255' and len(rgb) == width*height*3
        assert (width, height) == tuple(int(n) for n in headers[i][:2])
        checksum = 14695981039346656037
        for offset in range(0, len(rgb), 3):
            r, g, b = rgb[offset:offset+3]
            for byte in [b, g, r, 255]:
                checksum = ((checksum ^ byte)*1099511628211) & ((1<<64)-1)
        assert f'{checksum:016x}'.encode() == headers[i][2], 'PPM lost pixel/pitch/colour data'
        if i == 0:
            expected = b''.join(bytes([(colour('palette',3,x,y)>>16)&255,
                (colour('palette',3,x,y)>>8)&255, colour('palette',3,x,y)&255])
                for y in range(height) for x in range(width))
            assert rgb == expected, 'first raster differs from independent palette oracle'
    rows = list(csv.DictReader((d/'timing.csv').open()))
    assert len(rows) == 2 and all(int(row['core_ns']) > 0 for row in rows)
    for extra in [ ['--dump-frame', '3', str(d/'missing.ppm')],
                   ['--dump-frame', '0', str(d/'absent/failed.ppm')],
                   ['--dump-frame', 'invalid', str(d/'invalid.ppm')],
                   ['--dump-frame', '0'] ]:
        bad = subprocess.run(args + extra, capture_output=True)
        assert bad.returncode != 0, 'invalid/incomplete frame capture succeeded'
print('rpc-runner-dump: repeatable exact PPM RGB/pitch, independent palette oracle, canonical identity and failures passed')

# Real observer guest through runner input_state, not a direct answer seam.
import struct
with tempfile.TemporaryDirectory(prefix='arc-rpc-runner-pad-') as temp:
    d=Path(temp);rom=bytearray(struct.pack('<I',0xeafffffe)*524288)
    program={0:0xea00003e,8:0xea00005c}
    boot=[0xe3a0d201,0xe321f010,0xe3a0d201,0xe3a00000,0xe3a01003,0xef0400df,0xeafffffe]
    handler=[0xe3500081,0x01b0f00e,0xe1a0800e,0xe14f9000,0xe3a00081,0xe3a01000,0xe3a02000,0xef000006,0xe351000d,0x0a000003,0xe351001b,0x0a000001,0xeafffff6,0xe1a00000,0xe3a07201,0xe5871000,0xe169f009,0xe1b0f008]
    program.update({0x100+4*i:w for i,w in enumerate(boot)})
    program.update({0x180+4*i:w for i,w in enumerate(handler)})
    for at,w in program.items():struct.pack_into('<I',rom,at,w)
    (d/'guest.rom').write_bytes(rom);(d/'seed').write_bytes(bytes(256))
    (d/'guest.arcplan').write_text(f'version=1\nmodel=arm610\nfirmware={d/"guest.rom"}\ncmos=seed\nseed={d/"seed"}\nrtc_epoch=0\nobserver_cells=12\nobserver_forms=8\n')
    base=[runner,'--core',core,'--plan',str(d/'guest.arcplan'),'--frames','7','--ram-words','1']
    plain=subprocess.run(base,capture_output=True,text=True,check=True)
    neutral=subprocess.run(base+['--pad-at','0','0'],capture_output=True,text=True,check=True)
    assert plain.stdout==neutral.stdout,'neutral pad changed canonical output'
    assert 'TEST OVERRIDE' in plain.stderr and 'recognised-open=1 answer-available=1 modal-unknown=0' in plain.stderr
    for mask,key in [(256,13),(1,27)]:
        result=subprocess.run(base+['--pad-at','0','256','--pad-at','2','0','--pad-at','3','257','--pad-at','4','0','--pad-at','5',str(mask),'--pad-at','6','0'],capture_output=True,text=True,check=True)
        words=[int(x,16) for x in re.findall(r'^ram=([0-9a-f]+)$',result.stdout,re.M)]
        assert words[:5]==[0]*5 and words[5:]==[key,key],(mask,words,result.stderr)
    for options in [['--pad-at','0'],['--pad-at','0','65536'],['--pad-at','-1','0'],['--pad-at','1','0','--pad-at','1','256'],['--pad-at','2','0','--pad-at','1','0']]:
        assert subprocess.run(base+options,capture_output=True).returncode!=0
print('rpc-runner: stderr core logs, port-0 A/B, holds, releases, simultaneous arbitration and invalid scripts passed')

# Persisted CMOS, populated store and a FileCore-shaped privileged disc loop.
from rpc_boot_packages_test import inputs
with tempfile.TemporaryDirectory(prefix='arc-rpc-runner-disc-') as temp:
    d=Path(temp);system=inputs(d);save=d/'save';save.mkdir()
    rom=bytearray(struct.pack('<I',0xeafffffe)*524288)
    def branch(at,to):return 0xea000000|(((to-at-8)//4)&0xffffff)
    struct.pack_into('<I',rom,0,branch(0,0x100));struct.pack_into('<I',rom,8,0xe1b0f00e)
    words=[0xe59fa1f8,0xe3a0003c,0xe5ca0008,0xe3a01ffa,0xe2511001,0x1afffffd]
    for _ in range(4):words += [0xe3a00008,0xe5ca0014,0xe5da0014,0xe5da0014]
    words += [0xe3a02201]
    at=0x100+4*len(words);words += [0xe59f0000|(0x304-at-8),0xe5820000,0xe3a00002,0xe3a01008,0xe3e03000,0xef020033,0xe3a01033,0xef0400df,0xeafffffd]
    for i,w in enumerate(words):struct.pack_into('<I',rom,0x100+4*i,w)
    struct.pack_into('<II',rom,0x300,0x03010fc0,0x006f7754) # authored "Two"
    (d/'disc.rom').write_bytes(rom);(d/'seed').write_bytes(bytes(256))
    (d/'disc.arcplan').write_text(f'version=2\nmodel=sa110\nfirmware={d/"disc.rom"}\ncmos=persist\nseed={d/"seed"}\ncmos_layout=ro370\ncontent_id={"b"*40}\nrtc_epoch=0\nhard_disc=on\nhd_populate=on\nmedia={Path(sys.argv[4])/"disc-request/Set.m3u"}\nobserver_cells=52\n')
    args=[runner,'--core',core,'--plan',str(d/'disc.arcplan'),'--system',str(system),'--save',str(save),'--frames','8']
    for _ in range(2):
        result=subprocess.run(args,capture_output=True,text=True)
        assert result.returncode==0,result.stderr+result.stdout
        assert 'cells=0x34' in result.stderr and 'signal=A gate=seen name=Two' in result.stderr
        assert 'candidate=2 action=resolved reason=unique' in result.stderr
        assert 'action=inserted reason=unique' in result.stderr and 'Disc 2 inserted' in result.stderr,result.stderr
        assert 'user-error-open' not in result.stderr and 'identity-unknown' not in result.stderr
        assert result.stderr.count('RPC disc:')<=6,'unbounded per-frame diagnostics'
        print("\n".join(line for line in result.stderr.splitlines() if line.startswith("RPC disc:")))
    assert any('hd-riscpc' in str(p) for p in save.rglob('*')),'no populated hard disc'
    print('rpc-runner: populated and reopened persisted route, cells 52, privileged disc loop, unique disc 2 insertion and bounded diagnostics passed')

    # Qualification chooses one admitted form; actual R3 values are independent.
    original=(d/'disc.arcplan').read_text()
    device_at=0x100+4*words.index(0xe3e03000)
    for forms,device,admitted in [(2,0,False),(1,0,True),(1,0xffffffff,False),
            (0,0xffffffff,False),(3,0xffffffff,True),(3,1,False),(3,4,False)]:
        instruction=0xe3e03000 if device==0xffffffff else 0xe3a03000|device
        struct.pack_into('<I',rom,device_at,instruction);(d/'disc.rom').write_bytes(rom)
        (d/'disc.arcplan').write_text(original+f'observer_disc_devices=0x{forms:x}\n')
        result=subprocess.run(args,capture_output=True,text=True,check=True)
        assert f'disc-devices=0x{forms:x}' in result.stderr
        assert ('action=inserted reason=unique' in result.stderr)==admitted,(forms,device,result.stderr)
        if not admitted:assert 'gate=device' in result.stderr,(forms,device,result.stderr)
    for value in ['4','0xffffffff','-1','+1','unspecified','2junk']:
        (d/'disc.arcplan').write_text(original+f'observer_disc_devices={value}\n')
        assert subprocess.run(args,capture_output=True).returncode!=0,('invalid device mask',value)
print('rpc-runner: separate unspecified/drive-0 qualification masks, forbidden drives and invalid device masks passed')
