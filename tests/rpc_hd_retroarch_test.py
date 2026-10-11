#!/usr/bin/env python3
"""Real runner, force termination, reload and independent store oracle. GPL-2.0-only."""
from pathlib import Path
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib
import re
sys.dont_write_bytecode = True
from rpc_hd_retroarch_rom import CONTENT, COLOURS

def store_check(save, minimum):
    root=Path(save)/'arcdroid/hd-riscpc'/CONTENT
    names={p.name for p in root.iterdir()}
    manifests=[p for p in root.glob('manifest.?')]
    journals=[p for p in root.glob('journal.?')]
    assert len(manifests)==len(journals)==1, (names, 'checkpoint retirement incomplete')
    m=manifests[0].read_bytes()
    assert len(m)==24324 and m[:8]==b'ARCRHDM1' and m[16:56]==CONTENT.encode()
    assert zlib.crc32(m[:-4])==struct.unpack_from('<I',m,len(m)-4)[0]
    generation=struct.unpack_from('<Q',m,88)[0]
    assert generation>=minimum
    assert struct.unpack_from('<6I',m,64)==(512,63,16,256,262144,504)
    owned={manifests[0].name,journals[0].name}
    for index in range(504):
        kind,gen=struct.unpack_from('<I4xQ',m,128+index*48)
        if kind==0: continue
        assert kind==1
        name=f'c{index:03x}.g{gen:016x}';owned.add(name)
        b=(root/name).read_bytes()
        assert len(b)==262276 and b[:8]==b'ARCRHDC1'
        assert struct.unpack_from('<Q',b,72)[0]==gen
        assert b[80:112]==m[128+index*48+16:128+index*48+48]
        assert zlib.crc32(b[:124])==struct.unpack_from('<I',b,124)[0]
        assert zlib.crc32(b[128:-4])==struct.unpack_from('<I',b,len(b)-4)[0]
        assert hashlib.sha256(b[128:-4]).digest()==m[128+index*48+16:128+index*48+48]
    assert names==owned, names-owned
    j=journals[0].read_bytes()
    assert j[:8]==b'ARCRHDJ1' and len(j)<=8388608
    assert zlib.crc32(j[:252])==struct.unpack_from('<I',j,252)[0]
    assert struct.unpack_from('<Q',j,88)[0]==struct.unpack_from('<Q',m,96)[0]
    print(f'checkpoint generation={generation} canonical files={len(names)} journal bytes={len(j)}')
    return generation

def pixels(path,status):
    b=path.read_bytes();header=f'P6\n320 256\n255\n'.encode()
    assert b.startswith(header) and len(b)==len(header)+320*256*3
    assert b[len(header):]==bytes(COLOURS[status])*(320*256), f'expected status {status}; pixel={b[-3:]}'

def main():
    runner,core,fixture=map(lambda p:Path(p).resolve(),sys.argv[1:4])
    with tempfile.TemporaryDirectory(prefix='arc-rpc-hd-native-') as temp:
        d=Path(temp);save=d/'save';shutil.copytree(fixture/'save',save)
        plan=fixture/'hd.arcplan'
        def args(frames,shot,frame):
            return [str(runner),'--core',str(core),'--plan',str(plan),'--system',str(fixture),
                '--save',str(save),'--frames',str(frames),'--ram-offset','131072','--ram-words','1',
                '--dump-frame',str(frame),str(shot)]
        def clean(name,frames,status):
            shot=d/f'{name}.ppm'
            t=time.monotonic()
            r=subprocess.run(args(frames,shot,frames-1),capture_output=True,text=True,timeout=120)
            assert r.returncode==0,r.stdout[-3000:]+r.stderr
            assert r.stdout.splitlines()[-1]==f'ram={status:08x}'
            assert not re.search(r'(?i)damaged|no longer be saved|refused|RPC default generation',r.stderr),r.stderr
            pixels(shot,status);print(f'{name} status={status} elapsed={time.monotonic()-t:.2f}s')
        shot=d/'terminated.ppm';t=time.monotonic()
        with (d/'terminated.log').open('wb') as log:
            p=subprocess.Popen(args(1000000,shot,699),stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+120
                while not shot.exists() or shot.stat().st_size<245775:
                    assert p.poll() is None,'runner exited before termination point'
                    assert time.monotonic()<deadline,'termination screenshot timeout'
                    time.sleep(.01)
                pixels(shot,1);assert p.poll() is None
                p.kill();p.wait(timeout=30);assert p.returncode!=0
            finally:
                if p.poll() is None:p.kill();p.wait(timeout=30)
        assert not re.search(r'(?i)damaged|no longer be saved|refused|RPC default generation',(d/'terminated.log').read_text()),'termination log had a notice'
        print(f'force termination: written generation 1 elapsed={time.monotonic()-t:.2f}s')
        first=store_check(save,1)
        stale=d/'stale';shutil.copytree(save,stale)
        clean('verify1-write2',1500,2);second=store_check(save,first+1)
        clean('verify2',1000,3);assert store_check(save,second)==second
        # A stale generation-1 reload must produce BLUE, never the required
        # generation-2 GREEN. No out-of-disc session flags select the branch.
        current=save;save=stale;clean('stale-reload-rejected',1500,2);save=current
        # Corrupt one pattern word while repairing storage framing/integrity:
        # RED must come from the guest's comparison, not store damage detection.
        root=save/'arcdroid/hd-riscpc'/CONTENT;mp=next(root.glob('manifest.?'))
        m=bytearray(mp.read_bytes());index=16;entry=128+index*48
        gen=struct.unpack_from('<Q',m,entry+8)[0];chunk=root/f'c{index:03x}.g{gen:016x}'
        b=bytearray(chunk.read_bytes());b[128]^=1
        # The chunk embeds its payload hash as well as the manifest reference.
        digest=hashlib.sha256(b[128:-4]).digest();b[80:112]=digest
        struct.pack_into('<I',b,124,zlib.crc32(b[:124]))
        struct.pack_into('<I',b,len(b)-4,zlib.crc32(b[128:-4]));chunk.write_bytes(b)
        m[entry+16:entry+48]=digest;struct.pack_into('<I',m,len(m)-4,zlib.crc32(m[:-4]));mp.write_bytes(m)
        clean('wrong-pattern-rejected',1000,255)
        print('rpc-hd-retroarch-guest: write/kill/reload/verify/new pattern, disk control, stale and wrong-data screens pass')

if __name__=='__main__':main()
