#!/usr/bin/env python3
"""Execute the generated controller against authored SWI contracts on four CPUs.
No firmware code or private data. GPL-2.0-only.
"""
from pathlib import Path
import struct, subprocess, sys, tempfile
from rpc_controller_stub import Arm
from rpc_video_rom import Guest

runner, core, builder = sys.argv[1:4]

def firmware(image, child=0, close_error=False, ram_size=0, failure=None):
    # A minimal authored kernel: only the documented SWI contracts used by
    # this helper. Counters independently check effects and exact shutdown.
    a=Arm();a.w=[]
    a.emit(0xeafffffe,0xeafffffe);a.branch('handler')
    while len(a.w)<64:a.emit(0xeafffffe)
    a.label('handler');a.emit(0xe92d5ff0,0xe14fc000,0xe3ccc201,0xe168f00c)
    a.emit(0xe51ec004,0xe3ccc4ff)
    for swi,label in [(0x20006,'query'),(0x20023,'readvar'),(0x20024,'setvar'),(0x600c0,'init'),
                      (0x600de,'start'),(0x600dd,'close'),(0x2005c,'ramread'),(0x2002a,'ramcreate'),(0x11,'exit')]:
        a.literal(4,swi);a.emit(0xe15c0004);a.branch(label,condition=0)
    a.branch('return')
    a.label('query');a.emit(0xe5934020)
    for event,offset in ((1,0x1c),(7,0x24),(8,0x28)):
        a.emit(0xe3540000|event);a.branch('query-next-'+str(event),condition=1)
        a.literal(4,0x10031000+offset);a.emit(0xe5945000,0xe2855001,0xe5845000);a.branch('return')
        a.label('query-next-'+str(event))
    a.branch('return')
    a.label('readvar');a.literal(4,0x10032000);a.emit(0xe5944000,0xe3540000,0x03a02000,0x13e02000);a.branch('return')
    a.label('setvar')
    if failure=='latch-error':a.emit(0xe14fc000,0xe38cc201,0xe168f00c);a.branch('return')
    a.literal(4,0x10032000);a.emit(0xe3a05001,0xe5845000);a.branch('return')
    a.label('ramread');a.literal(1,ram_size)
    if failure=='ramfs-read-error':a.emit(0xe14fc000,0xe38cc201,0xe168f00c)
    a.branch('return')
    a.label('ramcreate');a.literal(4,0x10031018);a.emit(0xe5945000,0xe2855001,0xe5845000)
    if failure=='ramfs-create-error':a.emit(0xe14fc000,0xe38cc201,0xe168f00c)
    a.branch('return')
    a.label('init');a.literal(4,0x1003101c);a.emit(0xe5945000,0xe3550000,0x03a05001,0x05845004)
    a.literal(4,0x10031000);a.emit(0xe5945000,0xe2855001,0xe5845000);a.literal(1,0x4321)
    if failure=='init-error':a.emit(0xe14fc000,0xe38cc201,0xe168f00c)
    a.branch('return')
    a.label('start');a.literal(4,0x10031004);a.emit(0xe5945000,0xe2855001,0xe5845000,0xe5905000,0xe5845008)
    a.literal(0,child)
    if failure=='start-error':a.emit(0xe14fc000,0xe38cc201,0xe168f00c)
    a.branch('return')
    a.label('close');a.literal(4,0x10031008);a.emit(0xe5840000,0xe5841008)
    if close_error:a.emit(0xe14fc000,0xe38cc201,0xe168f00c)
    a.branch('return')
    a.label('exit');a.branch('exit')
    a.label('return');a.emit(0xe8fd9ff0)
    # Bootstrap SVC stack, MMU disabled, then invoke the helper twice from USR.
    a.label('main');main=len(a.w)*4;g=Guest();g.literal(0,0x70);g.emit(0xee010f10)
    g.literal(0,0xd3);g.emit(0xe121f000);g.literal(13,0x10070000)
    for i in range(0,len(image),4):g.write(0x10020000+i,struct.unpack_from('<I',image,i)[0])
    g.literal(0,0x10);g.emit(0xe121f000);g.literal(13,0x10060000)
    for call in range(2):
        if call:
            for i in range(0,len(image),4):g.write(0x10020000+i,struct.unpack_from('<I',image,i)[0])
        # A fresh image load must not recreate the latch outside this allocation.
        g.literal(0,0x10020000);g.literal(1,len(image));g.literal(2,0x10030000);g.literal(3,1)
        g.literal(4,0x10020040);g.emit(0xe1a0e00f,0xe1a0f004)
    g.write(0x10031014,1);g.emit(0xeafffffe)
    a.w.extend(g.w)
    data=a.finish();struct.pack_into('<I',data,0,0xea000000|((main-8)//4))
    return data+struct.pack('<I',0xeafffffe)*((2097152-len(data))//4)

with tempfile.TemporaryDirectory(prefix='arc-controller-') as tmp:
    root=Path(tmp);raw=root/'helper';subprocess.run([builder,'--write',str(raw)],check=True)
    image=raw.read_bytes();(root/'seed').write_bytes(bytes(256))
    for model in ('arm610','arm710','sa110','arm7500'):
        for kind,child,error,ram_size in [('returned',0,False,0),('desktop',99,False,0),('close-error',0,True,0),
                ('ramfs-absent',0,False,0),('ramfs-present',0,False,32768),('bad-plan',0,False,0),('latch-error',0,False,0),('init-error',0,False,0),
                ('start-error',0,False,0),('ramfs-read-error',0,False,0),('ramfs-create-error',0,False,0)]:
            ramfs=kind.startswith('ramfs')
            subprocess.run([builder,'--write-ramfs' if ramfs else '--write',str(raw)],check=True)
            image=bytearray(raw.read_bytes())
            if kind=='bad-plan':image[struct.unpack_from('<I',image,44)[0]+8]=3
            (root/'authored.rom').write_bytes(firmware(image,child,error,ram_size,kind))
            outputs=[]
            for cells in (0,3):
                plan=root/'case.arcplan';plan.write_text(f'version=1\nmodel={model}\nfirmware={root/"authored.rom"}\ncmos=seed\nseed={root/"seed"}\nrtc_epoch=0\nobserver_cells={cells}\n')
                p=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(root),'--save',str(root),
                    '--frames','1','--ram-offset',str(0x31000),'--ram-words','11'],capture_output=True,text=True,timeout=30)
                assert p.returncode==0,(model,kind,p.stderr)
                line=next(line for line in p.stdout.splitlines() if line.startswith('ram='));outputs.append(line)
                words=[int(w,16) for w in line[4:].split(',')]
                expected=[1,1,0x4321,0x6e755225,0x4b534154,0 if error else 1,1 if ramfs and not ram_size else 0,1,0,0,0 if error else 1]
                if kind=='bad-plan':expected=[0,0,0,0,0,1,0,0,0,2,0]
                if kind=='latch-error':expected=[0,0,0,0,0,1,0,2,0,2,0]
                if kind=='init-error':expected=[1,0,0,0,0,1,0,1,0,1,0]
                if kind=='start-error':expected[9]=1
                if kind=='ramfs-read-error':expected=[0,0,0,0,0,1,0,1,0,1,0]
                if kind=='ramfs-create-error':expected=[0,0,0,0,0,1,1,1,0,1,0]
                assert words==expected,(model,kind,words)
            assert outputs[0]==outputs[1],(model,kind,'observation changed guest effects')
    print('controller: four CPUs, one dispatch across duplicate CALL, exact CloseDown, fatal close failure, observation equivalence')
