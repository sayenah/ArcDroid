#!/usr/bin/env python3
"""Execute the authored helper through RPC CPUs, no private firmware. GPL-2.0-only."""
from pathlib import Path
import struct, subprocess, sys, tempfile
from rpc_video_rom import Guest
runner,core,builder=sys.argv[1:4]
with tempfile.TemporaryDirectory(prefix='arc-rpc-helper-') as tmp:
    root=Path(tmp);disc=root/'helper.adl'
    subprocess.run([builder,'--write',str(disc)],check=True)
    image=disc.read_bytes()[1792:1792+144]
    for model in ('arm610','sa110'):
        for label,offset,value,status in (('noop',140,0,2),('action',140,1,3),
                ('version',136,2,3),('short-plan',48,4,3),('long-plan',48,12,3),
                ('replacement',None,None,7)):
            data=bytearray(image)
            if offset is not None:struct.pack_into('<I',data,offset,value)
            else:struct.pack_into('<6I',data,64,0xe3a00007,0xe582000c,0xe10f3000,0xe3130010,0x01b0f00e,0x11a0f00e)
            g=Guest();g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
            for i in range(0,len(data),4):g.write(0x10020000+i,struct.unpack_from('<I',data,i)[0])
            g.write(0x1002010c,0)
            g.literal(0,0x10020000);g.literal(1,len(data));g.literal(2,0x10020100);g.literal(3,1)
            g.literal(4,0x10020040);g.emit(0xe1a0e00f);g.emit(0xe1a0f004);g.emit(0xeafffffe)
            rom=bytearray(struct.pack('<I',0xeafffffe)*(2097152//4));rom[:len(g.w)*4]=struct.pack('<'+'I'*len(g.w),*g.w)
            (root/'authored.rom').write_bytes(rom);(root/'seed').write_bytes(bytes(256))
            plan=root/'test.arcplan';plan.write_text(f'version=1\nmodel={model}\nfirmware={root/"authored.rom"}\ncmos=seed\nseed={root/"seed"}\nrtc_epoch=0\n')
            r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(root),'--save',str(root),
                '--frames','1','--ram-offset',str(0x2010c),'--ram-words','1'],capture_output=True,text=True,timeout=30)
            assert r.returncode==0,(label,r.stderr)
            assert f'ram={status:08x}' in r.stdout,(model,label,r.stdout)
    for model in ('arm610','sa110'):
        g=Guest();g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
        for i in range(0,len(image),4):g.write(0x02010000+i,struct.unpack_from('<I',image,i)[0])
        g.write(0x0201100c,0)
        g.literal(0,0x0c000000);g.emit(0xe121f000)
        g.literal(0,0x02010000);g.literal(1,len(image));g.literal(2,0x02011000);g.literal(3,1)
        g.literal(4,0x02010040);g.literal(14,0);link=len(g.w)-1;g.emit(0xe1a0f004)
        g.w[link]=len(g.w)*4 | 0x0c000000
        g.literal(4,0x0201100c);g.emit(0xe5940000);g.literal(4,0x1002010c);g.emit(0xe5840000,0xeafffffe)
        rom=bytearray(struct.pack('<I',0xeafffffe)*524288);rom[:len(g.w)*4]=struct.pack('<'+'I'*len(g.w),*g.w)
        (root/'authored.rom').write_bytes(rom)
        plan=root/'test.arcplan';plan.write_text(f'version=1\nmodel={model}\nfirmware={root/"authored.rom"}\ncmos=seed\nseed={root/"seed"}\nrtc_epoch=0\n')
        r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(root),'--save',str(root),
            '--frames','1','--ram-offset',str(0x2010c),'--ram-words','1'],capture_output=True,text=True,timeout=30)
        assert r.returncode==0 and 'ram=00000002' in r.stdout,(model,'User26 return',r.stderr,r.stdout)
    print('RPC helper guest: no-op, unknown action/version/length and replacement execute on ARM610 and SA-110')
