#!/usr/bin/env python3
"""Fresh process and same-library frontend switching. GPL-2.0-only."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import os
import hashlib
import struct
import zlib
sys.dont_write_bytecode=True
from rpc_cmos_factory_rom import generate
from rpc_hygiene_rom import generate as guest, error_guest
from generate_fixtures import build_d_discovery

def check(condition, message):
    if not condition:
        raise RuntimeError(message)

def run(exe,core,fixtures,mode,profile="full"):
    with tempfile.TemporaryDirectory(prefix='arc-session-hygiene-') as temp:
        root=Path(temp);system=root/'system';system.mkdir();generate(system)
        save=root/'save';(save/'arcdroid/cmos').mkdir(parents=True)
        fw=system/'arcdroid/firmware';fw.mkdir(parents=True)
        fixtures=Path(fixtures)
        shutil.copyfile(fixtures/'arcdroid-test.rom',fw/'authored.data')
        shutil.copyfile(fixtures/'arcdroid-test.adf',system/'arc.adf')
        shutil.copyfile(fixtures/'arcdroid-chooser-l.adl',system/'chooser.adl')
        arc_refusal=system/'arc-refusal/arcdroid/firmware';arc_refusal.mkdir(parents=True)
        shutil.copyfile(fixtures/'arcdroid-boot-prompt-write.rom',arc_refusal/'authored.data')
        (system/'seed').write_bytes(bytes(256))
        (system/'spin.rom').write_bytes(bytes.fromhex('feffffea')*(2097152//4))
        common=f'model=sa110\nfirmware={system / "spin.rom"}\nrtc_epoch=946684800\n'+('rate_mips=1\n' if profile=='sanitizer' else '')
        (system/'run.arcplan').write_text('version=2\n'+common+f'cmos=persist\ncmos_layout=ro370\ncontent_id={"b"*40}\nmedia={system / "arc.adf"}\n')
        digest=hashlib.sha256((system/'spin.rom').read_bytes()).digest()
        record=bytearray(512);record[:8]=b'ARCRPCM1'
        struct.pack_into('<IIIIQ',record,8,1,512,3,0x10003,1)
        record[32:72]=b'b'*40;record[72:104]=digest
        struct.pack_into('<I',record,508,zlib.crc32(record[:508]))
        store=save/'arcdroid/cmos-riscpc';store.mkdir()
        (store/f'{"b"*40}-m3-{digest.hex()}.1').write_bytes(record)
        for name,firmware in [('failed','empty'),('generation','full')]:
            (system/f'{name}.arcplan').write_text(f'version=2\nmodel=sa110\nfirmware={system / (firmware+".rom")}\ncmos=persist\ncmos_layout=ro370\ncontent_id={"a"*40}\nrtc_epoch=946684800\n')
        (system/'failed.arcplan').write_text((system/'generation.arcplan').read_text()+'hard_disc=on\nhd_populate=on\n')
        for name in ('refusal','format'):
            guest(system/f'{name}.rom',formatting=name=='format')
            digest=hashlib.sha256((system/f'{name}.rom').read_bytes()).digest()
            record[72:104]=digest;struct.pack_into('<I',record,508,zlib.crc32(record[:508]))
            (store/f'{"b"*40}-m3-{digest.hex()}.1').write_bytes(record)
            disc=system/f'{name}.adf';disc.write_bytes(build_d_discovery([],name.encode('ascii')))
            if name=='format':
                peer=system/'format-peer.adf';peer.write_bytes(build_d_discovery([],b'Peer'))
                playlist=system/'format.m3u';playlist.write_text(str(disc)+'\n'+str(peer)+'\n');disc=playlist
            (system/f'{name}.arcplan').write_text(f'version=2\nmodel=sa110\nfirmware={system / (name+".rom")}\ncmos=persist\ncmos_layout=ro370\ncontent_id={"b"*40}\nrtc_epoch=946684800\nmedia={disc}\n')
        error_guest(system/'error.rom')
        (system/'error.arcplan').write_text(f'version=1\nmodel=sa110\nfirmware={system / "error.rom"}\ncmos=seed\nseed={system / "seed"}\nrtc_epoch=0\nobserver_cells=12\nobserver_forms=0xffffffff\n')
        progress=root/"progress.txt"
        print(f"hygiene-{mode}-progress={progress}",flush=True)
        test_env=os.environ.copy();test_env["ARCDROID_HYGIENE_PROGRESS"]=str(progress)
        result=subprocess.run([str(exe),str(core),str(system/'run.arcplan'),str(system),str(save),mode,
            str(system/'failed.arcplan'),str(system/'generation.arcplan')],capture_output=True,text=True,timeout=850,env=test_env)
        check(result.returncode==0,result.stdout+result.stderr)
        projection=[line for line in result.stdout.splitlines() if line.startswith('frontend-arculator-trace=')]
        return projection,result.stdout.replace(str(root),'<case>'),{str(p.relative_to(save)):p.read_bytes() for p in save.rglob('*') if p.is_file()}

if __name__=='__main__':
    exe,core,fixtures=sys.argv[1:4];profile=sys.argv[4] if len(sys.argv)>4 else "full"
    if len(sys.argv)>5:
        print(run(exe,core,fixtures,sys.argv[5],profile)[1],end='')
    else:
        fresh,_,_=run(exe,core,fixtures,'fresh',profile);switched,output,_=run(exe,core,fixtures,'switch',profile)
        check(len(fresh)==1 and fresh==switched,f'{fresh} != {switched}')
        print(output,end='')
        print('plan-core frontend: fresh-process trace, running/terminal/generation/fault, chooser/failed-load, persistence, callbacks, cycles and IDs passed')
