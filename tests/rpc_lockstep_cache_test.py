#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Owner-cache differential over authored input, display, sound and device work."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import time
import zlib
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from rpc_ninja_commands import rebuild_commands
from rpc_timing_rom import Guest


def require(ok,message):
    if not ok:raise RuntimeError(message)


def mutant(build,core,root,kind):
    relative='engine/rpcemu/src/'+('keyboard.c' if kind=='mouse' else 'vidc20.c')
    text=(ROOT/relative).read_text(encoding='utf-8')
    anchors=(['mouse_process(void)'] if kind=='mouse' else
             ['static void dma_reload(bool init)','static uint32_t next_quad(uint32_t address, unsigned *transfers)'])
    for function in anchors:
        anchor=function+'\n{\n    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_iomd));'
        require(text.count(anchor)==1,kind+' invalidation mutant anchor')
        text=text.replace(anchor,anchor.replace('    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_iomd));','    ((void)0);'))
    cache=(build/'CMakeCache.txt').read_text(encoding='utf-8')
    ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
    commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True,encoding='utf-8'))]
    cc,link=rebuild_commands(commands,ROOT/relative,'arcdroid_rpclockstep_libretro_rpc',build,os.name=='nt')
    original=cc[cc.index('-o')+1];source=root/Path(relative).name;obj=root/(Path(relative).stem+'.o');library=root/Path(core).name
    source.write_text(text,encoding='utf-8')
    for flag,value in (('-c',source),('-o',obj),('-MF',obj.with_suffix('.d')),('-MT',obj)):
        if flag in cc:cc[cc.index(flag)+1]=str(value)
    cc+=['-I',str((ROOT/relative).parent)]
    link=[str(obj) if x==original else x for x in link];link[link.index('-o')+1]=str(library)
    link=['-Wl,--out-implib,'+str(root/'mutant.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in link]
    for command in (cc,link):
        result=subprocess.run(command,cwd=build,capture_output=True,text=True,encoding='utf-8',timeout=180)
        require(result.returncode==0,('mutant build error, not a kill',result.stderr))
    return library


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('runner');p.add_argument('core');p.add_argument('fixtures',type=Path)
    p.add_argument('--mutant',type=Path);p.add_argument('--mutant-kind',choices=('mouse','cursor'),default='mouse');p.add_argument('--arguments',action='store_true');a=p.parse_args()
    start=time.monotonic()
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-cache-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
        root=Path(tmp)
        def execute(name,cache,core=a.core,options=(),limit=12000,plan=None):
            stream=root/(name+'.stream');save=root/(name+'-save');save.mkdir(exist_ok=True)
            plan=plan or a.fixtures/'input-0.arcplan'
            args=[a.runner,'--core',str(core),'--plan',str(plan),'--system',str(plan.parent),'--save',str(save),
                '--frames','2' if name.startswith('video-') else '1','--ram-offset','0' if name.startswith('floppy-') else '131072','--ram-words','16','--endpoint-instructions',f'1:{limit}:0','--lockstep-stream',str(stream),
                '--lockstep-cache',cache,*options]
            result=subprocess.run(args,capture_output=True,text=True,encoding='utf-8',timeout=300)
            return result,stream
        if a.arguments:
            for role in ('baseline','candidate'):
                for density in ('baseline','every-instruction','full-audit'):
                    x,s=execute('first','on',options=['--lockstep-role',role,'--lockstep-density',density],limit=17)
                    y,t=execute('second','on',options=['--lockstep-density',density,'--lockstep-role',role],limit=17)
                    require(x.returncode==y.returncode==0,(x.stderr,y.stderr))
                    require(s.read_bytes()==t.read_bytes() and x.stdout==y.stdout,(role,density,'argument order changed stream'))
                    # The role bit is independent of density, including the
                    # normal schedule that shares the human word baseline.
                    flags=struct.unpack_from('<I',s.read_bytes(),157)[0]
                    require(bool(flags&1)==(role=='baseline'),('wrong role',role,density,flags))
            print('baseline density and role: all six configurations are order independent');return
        # The device ROMs include timed FDC completion and absent ATA paths;
        # separate real ATA attachment below exercises its interrupt publisher.
        cases=[('input','input-0',12000),('video','video-paths-3-words',12000),
               ('audio','audio-0',1000),('floppy','devices-3',12000)]
        g=Guest();g.literal(0,0x70);g.emit(0xee010f10)
        # Parallel interrupt plus serial FIQ: two SuperIO -> IOMD writers.
        g.store(0x030109e8,0x10);g.store(0x03010fe4,2)
        g.literal(0,0x030109e4);g.emit(0xe5d01000)
        payload=struct.pack('<'+'I'*len(g.words),*g.words);(root/'superio.rom').write_bytes(payload+bytes(2097152-len(payload)))
        (root/'seed').write_bytes(bytes(256))
        (root/'superio.arcplan').write_text('version=1\nmodel=arm610\nfirmware=superio.rom\ncmos=seed\nseed=seed\nrtc_epoch=0\n',encoding='utf-8')
        # ATA identify raises and acknowledges the actual attached-master IRQ.
        g=Guest();g.literal(0,0x70);g.emit(0xee010f10)
        g.store(0x030107dc,0xec)
        g.literal(0,0x03010fd8);g.emit(0xe5d01000,0xe3110080,0x1afffffc)
        # Observe the actual IRQ before and after the guest status read.
        g.literal(0,0x03200020);g.emit(0xe5901000);g.literal(2,0x10020000);g.emit(0xe5821000)
        g.literal(0,0x030107dc);g.emit(0xe5d01000)
        g.literal(0,0x03200020);g.emit(0xe5901000);g.literal(2,0x10020004);g.emit(0xe5821000)
        payload=struct.pack('<'+'I'*len(g.words),*g.words);rom=payload+bytes(2097152-len(payload));(root/'hd.rom').write_bytes(rom)
        digest=hashlib.sha256(rom).digest();content='a'*40
        seed=bytearray(512);seed[:8]=b'ARCRPCM1';struct.pack_into('<IIIIQ',seed,8,1,512,1,0x10003,1)
        seed[32:72]=content.encode();seed[72:104]=digest;struct.pack_into('<I',seed,508,zlib.crc32(seed[:508]))
        (root/'hd.arcplan').write_text(f'version=2\nmodel=arm610\nfirmware=hd.rom\ncmos=persist\ncmos_layout=ro370\ncontent_id={content}\nrtc_epoch=0\nhard_disc=on\n',encoding='utf-8')
        if a.mutant:cases=[cases[0 if a.mutant_kind=='mouse' else 1]]
        else:cases += [('superio',None,100),('hd',None,10000)]
        for label,fixture,limit in cases:
            values=[]
            for cache in ('on','off'):
                name=label+'-'+cache
                if label=='hd':
                    base=root/(name+'-save')/'arcdroid/cmos-riscpc';base.mkdir(parents=True)
                    (base/f'{content}-m1-{digest.hex()}.1').write_bytes(seed)
                result,stream=execute(name,cache,limit=limit,plan=a.fixtures/(fixture+'.arcplan') if fixture else root/(label+'.arcplan'))
                require(result.returncode==0,(label,cache,result.returncode,result.stderr))
                values.append((result.stdout,stream.read_bytes()))
            require(values[0]==values[1],(label,'owner cache changed guest output or stream'))
            output=values[0][0]
            events=[int(x) for x in re.findall(r' events=(.*)$',output,re.M)[-1].split(',')]
            words=[int(x,16) for x in re.findall(r'^ram=(.*)$',output,re.M)[-1].split(',')]
            if label=='input':require(words[2:4]==[12,7],('mouse motion did not run',words))
            if label=='video':require(any(int(x)>0 for x in re.findall(r' pixels=(\d+)',output)) and
                any(int(x)>0 for x in re.findall(r' bytes=(\d+)',output)),'video DMA did not run')
            if label=='audio':require(any(int(x)>0 for x in re.findall(r' dma=(\d+)/',output)),'sound DMA did not run')
            if label=='floppy':require(events[4]>0 and words[2:4]==[0x40,5],('timed FDC did not complete',events,words))
            if label=='hd':require(events[5]>0 and words[0]&2 and not words[1]&2,
                ('attached ATA IRQ did not raise and acknowledge',events,words))
            if label=='superio':
                irq,fiq=map(lambda x:int(x,16),re.findall(r' irq=([0-9a-f]+) fiq=([0-9a-f]+)',output)[-1])
                require(irq&1 and fiq&0x10,('parallel/serial interrupt paths did not run',irq,fiq))
            print('cache on/off identical:',label,len(values[0][1]),flush=True)
        if a.mutant:
            library=mutant(a.mutant,a.core,root,a.mutant_kind)
            result,_=execute(label+'-mutant','on',core=library,limit=limit,plan=a.fixtures/(fixture+'.arcplan'))
            require(result.returncode==1 and 'lockstep mutating projection' in result.stderr,
                    ('cross-unit dirty-mark mutant survived or failed for unrelated reason',result.returncode,result.stderr))
            control,stream=execute(label+'-mutant-uncached','off',core=library,limit=limit,plan=a.fixtures/(fixture+'.arcplan'))
            require(control.returncode==0,('uncached mutant control failed',control.stderr))
            require((control.stdout,stream.read_bytes())==values[1],
                    'uncached dirty-mark mutant changed the reference guest output or stream')
            print('killed real C cross-unit-'+a.mutant_kind+'-dirty-mark (uncached control passes)',flush=True)
    print('cache fidelity elapsed',round(time.monotonic()-start,3))


if __name__=='__main__':main()
