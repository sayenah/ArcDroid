#!/usr/bin/env python3
"""Test the once-only, deactivated-owned-guard release fault plan seam.
Authored firmware, existing completion boundary, no private content.
GPL-2.0-only.
"""
from pathlib import Path
import struct,subprocess,sys,tempfile
from rpc_video_rom import Guest
runner,core=sys.argv[1:3]
with tempfile.TemporaryDirectory(prefix='arc-gate-release-fault-') as temp:
    root=Path(temp);(root/'seed').write_bytes(bytes(256))
    for model in ('arm610','sa110'):
        for enabled,active,magic in ((False,0,0x31524741),(True,0,0x31524741),(True,1,0x31524741),(True,0,0)):
            g=Guest();g.emit(0xea00003e,*([0xeafffffe]*63))
            g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
            g.literal(13,0x10030000);g.emit(0xe321f010);g.literal(13,0x10031000)
            for at,value in ((0,magic),(4,1),(8,256),(252,active)):g.write(0x10021000+at,value)
            g.write(0x10024000,0)
            for n in range(2):
                g.literal(0,0x1d);g.literal(1,0x10021010);g.literal(2,0x100210fc);g.emit(0xef020020)
                g.literal(4,0x10022000+n*8);g.emit(0xe5840000,0xe10f3000,0xe1a03e23,0xe2033001,0xe5843004)
            g.emit(0xeafffffe);handler=len(g.w)*4
            g.literal(7,0x10024000);g.emit(0xe5976000,0xe2866001,0xe5876000,0xe14f5000,0xe3c55201,0xe169f005,0xe1b0f00e)
            g.w[2]=0xea000000|((handler-16)//4)
            rom=bytearray(struct.pack('<I',0xeafffffe)*524288);rom[:len(g.w)*4]=struct.pack('<'+'I'*len(g.w),*g.w)
            (root/'authored.rom').write_bytes(rom)
            save=root/f'{model}-{int(enabled)}-{active}-{magic}';save.mkdir()
            plan=root/'test.arcplan';plan.write_text(f'version=2\nmodel={model}\nfirmware={root/"authored.rom"}\ncmos=persist\nseed={root/"seed"}\ncmos_layout=ro370\ncontent_id={"b"*40}\nrtc_epoch=0\n'+('gate_release_fail=once\n' if enabled else ''))
            r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(root),'--save',str(save),
                '--frames','8','--ram-offset',str(0x22000),'--ram-words','4'],capture_output=True,text=True,timeout=15)
            injected=enabled and not active and magic==0x31524741
            expected=f'ram={0 if injected else 29:08x},{int(injected):08x},0000001d,00000000'
            assert r.returncode==0 and expected in r.stdout,(model,enabled,active,magic,r.stderr,r.stdout)
    print('Release fault: only an inactive owned guard, once; normal/active/unowned calls retain kernel execution')
