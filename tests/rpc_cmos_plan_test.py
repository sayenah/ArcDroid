#!/usr/bin/env python3
"""Independent envelope/provenance oracle and authored frontend cases. GPL-2.0-only."""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile
import zlib
sys.dont_write_bytecode=True
from rpc_cmos_factory_rom import generate
exe,core,runner,shipping,fixtures,model,kind=sys.argv[1:]
model=int(model);rates=[20,35,100,35];models=['arm610','arm710','sa110','arm7500']

def record(path,magic,firmware,content=None):
    b=path.read_bytes();assert len(b)==512 and b[:8]==magic
    assert struct.unpack_from('<III',b,8)==(1,512,model+1)
    assert struct.unpack_from('<I',b,508)[0]==zlib.crc32(b[:508])
    assert b[72:104]==hashlib.sha256(firmware.read_bytes()).digest()
    if content:assert b[32:72]==content.encode()
    return b

with tempfile.TemporaryDirectory(prefix='arc-rpc-cmos-') as tmp:
    d=Path(tmp);roms=generate(d/'roms');save=d/'save';save.mkdir()
    layout='ro350' if kind=='ro350' else 'ro370'
    text=f'version=2\nmodel={models[model]}\nfirmware={roms/kind}.rom\ncmos=persist\ncmos_layout={layout}\ncontent_id={"a"*40}\nrtc_epoch=946684800\n'
    plan=d/'test.arcplan';plan.write_text(text)
    def run(mode='positive'):
        r=subprocess.run([exe,core,str(plan),str(roms),str(save),mode,str(rates[model]),str(model)],capture_output=True,text=True)
        assert r.returncode==0,r.stderr+r.stdout
    # Shipping rejects before opening any plan/ROM/save record.
    r=subprocess.run([runner,'--core',shipping,'--plan',str(plan),'--save',str(save),'--reject'],capture_output=True,text=True)
    assert r.returncode==0 and not list(save.iterdir()),r.stderr
    if kind=='writer':
        # Independently encode an ordinary record with an intentionally invalid
        # guest checksum: persistence must retain it, never normalise/repair.
        digest=hashlib.sha256((roms/'writer.rom').read_bytes()).digest()
        b=bytearray(512);b[:8]=b'ARCRPCM1';struct.pack_into('<IIIIQ',b,8,1,512,model+1,0x10003,1)
        b[32:72]=b'a'*40;b[72:104]=digest;b[104:344]=bytes([77])*240
        struct.pack_into('<I',b,508,zlib.crc32(b[:508]));base=save/'arcdroid/cmos-riscpc';base.mkdir(parents=True)
        target=base/f'{"a"*40}-m{model+1}-{digest.hex()}.1';target.write_bytes(b)
        run('writer');records=list(base.glob('*.0'))+list(base.glob('*.1'))
        latest=max(records,key=lambda p:struct.unpack_from('<Q',p.read_bytes(),24)[0])
        final=record(latest,b'ARCRPCM1',roms/'writer.rom','a'*40)
        expected=bytearray(240);expected[47]=1;assert final[104:344]==expected and not (base/'defaults').exists()
    elif kind=='nodelete':
        run('nodelete');assert not list(save.rglob('*.0')) and not list(save.rglob('*.1'))
    elif kind=='reset':
        plan.write_text(text.replace('/reset.rom','/full.rom'));run('reset')
        assert not list(save.rglob('*.0')) and not list(save.rglob('*.1'))
    elif kind in ['empty','scatter','incomplete','late']:
        run('negative');assert not list(save.rglob('*.0')) and not list(save.rglob('*.1'))
    else:
        run();base=save/'arcdroid/cmos-riscpc';slots=list(base.glob('*.1'));defaults=list((base/'defaults').glob('*.1'))
        assert len(slots)==len(defaults)==1
        b=record(defaults[0],b'ARCRPCD1',roms/(kind+'.rom'))
        expected=bytearray(240);expected[47]=1
        assert b[104:344]==expected
        assert struct.unpack_from('<IIII',b,344)==(2,1,rates[model],1)
        assert struct.unpack_from('<QQQQQ',b,360)==(946684800,0,45000000000,45000000000,5000000000)
        writes,last=struct.unpack_from('<QQ',b,400);assert writes==(238 if kind=='ro350' else 240) and 0<last<40000000000
        if kind=='delayed':assert last>18022400000
        mask=b'\0\0'+b'\xff'*30
        if kind=='ro350':mask=mask[:24]+b'\xf3'+mask[25:]
        assert b[416:448]==mask and b[448:480]==hashlib.sha256(expected).digest() and not any(b[480:508])
        ordinary=record(slots[0],b'ARCRPCM1',roms/(kind+'.rom'),'a'*40)
        assert ordinary[104:344]==expected and not any(ordinary[344:508])
        # Identical firmware/model/content/epoch and procedure at one versus
        # eight frames per call must produce the entire same default record.
        original_save=save;save=d/'save-one';save.mkdir();run('positive1')
        one=list((save/'arcdroid/cmos-riscpc/defaults').glob('*.1'))
        assert len(one)==1 and one[0].read_bytes()==b
        save=original_save
        original=slots[0].read_bytes();run('record');assert slots[0].read_bytes()==original
        # The same frontend switches through real Arculator persistence.
        import shutil
        fw=roms/'arcdroid/firmware';fw.mkdir(parents=True)
        shutil.copyfile(Path(fixtures)/'arcdroid-test.rom',fw/'authored.data')
        shutil.copyfile(Path(fixtures)/'arcdroid-test.adf',roms/'arc.adf')
        run('cross');assert slots[0].read_bytes()==original
        # Different content uses cached defaults without running generation.
        plan.write_text(text.replace('a'*40,'c'*40));run('cache');assert defaults[0].read_bytes()==b
        # Canonical save confinement includes existing and dangling links.
        if sys.platform!='win32':
            outside=d/'outside';outside.mkdir()
            for target in [outside,outside/'missing']:
                alternate=d/('save-link-'+('existing' if target.exists() else 'dangling'));alternate.mkdir()
                (alternate/'arcdroid').symlink_to(target,target_is_directory=True)
                original_save=save;save=alternate;plan.write_text(text);run('reject');save=original_save
                assert not list(outside.iterdir())
        # Invalid/undeclared families and content identities fail visibly.
        for bad in [text.replace('cmos_layout='+layout,'cmos_layout=unknown'),text.replace('content_id='+'a'*40,'content_id=bad'),text.replace('cmos_layout='+layout+'\n','')]:
            plan.write_text(bad);run('reject')
    print(f'RPC CMOS plan model={model} case={kind}: independent oracle passed')
