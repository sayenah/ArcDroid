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
exe,core,runner,shipping,fixtures,model,kind=sys.argv[1:8]
profile=sys.argv[8] if len(sys.argv)>8 else "full"
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
    layout='ro350' if kind=='ro350' else 'ro360' if kind=='ro360' else 'ro370'
    # Reset interrupts the full-mask guest; there is no reset.rom. Construct
    # the intended basename directly, independent of native path separators.
    firmware=roms/(('full' if kind in ('reset','fallback') else 'empty' if kind=='terminalfallback' else kind)+'.rom')
    text=f'version=2\nmodel={models[model]}\nfirmware={firmware}\ncmos=persist\ncmos_layout={layout}\ncontent_id={"a"*40}\nrtc_epoch=946684800\n'
    plan=d/'test.arcplan';plan.write_text(text)
    def run(mode='positive'):
        r=subprocess.run([exe,core,str(plan),str(roms),str(save),mode,str(rates[model]),str(model)],capture_output=True,text=True)
        assert r.returncode==0,r.stderr+r.stdout
    # Shipping rejects before opening any plan/ROM/save record.
    r=subprocess.run([runner,'--core',shipping,'--plan',str(plan),'--save',str(save),'--reject'],capture_output=True,text=True)
    assert r.returncode==0 and not list(save.iterdir()),r.stderr
    if kind=='writer':
        # Independently encode an ordinary record with an intentionally invalid
        # guest checksum: the envelope store retains arbitrary payloads;
        # AY-1 L-1 refreshes the year while preserving the checksum error.
        digest=hashlib.sha256((roms/'writer.rom').read_bytes()).digest()
        b=bytearray(512);b[:8]=b'ARCRPCM1';struct.pack_into('<IIIIQ',b,8,1,512,model+1,0x10003,1)
        b[32:72]=b'a'*40;b[72:104]=digest;b[104:344]=bytes([77])*240
        struct.pack_into('<I',b,508,zlib.crc32(b[:508]));base=save/'arcdroid/cmos-riscpc';base.mkdir(parents=True)
        target=base/f'{"a"*40}-m{model+1}-{digest.hex()}.1';target.write_bytes(b)
        run('writer');records=list(base.glob('*.0'))+list(base.glob('*.1'))
        latest=max(records,key=lambda p:struct.unpack_from('<Q',p.read_bytes(),24)[0])
        final=record(latest,b'ARCRPCM1',roms/'writer.rom','a'*40)
        # The final reopen refreshes the year before any writer instruction.
        # The guest writes an otherwise zero payload with valid checksum 1.
        # Refreshing the century adds 20, retaining that valid checksum.
        expected=bytearray(240);expected[177]=20;expected[47]=21
        assert final[104:344]==expected and not (base/'defaults').exists()
        # Keep one valid recovery slot and a corrupt published peer. A live
        # guest may run from the former, but refusal is logged once per load.
        for p in records:p.unlink()
        target.write_bytes(b)
        corrupt=base/f'{"a"*40}-m{model+1}-{digest.hex()}.0'
        corrupt.write_bytes(b'ARCCMOS2'+bytes(504))
        run('refused');assert target.read_bytes()==b and corrupt.read_bytes()==b'ARCCMOS2'+bytes(504)
    elif kind=='nodelete':
        run('nodelete');assert not list(save.rglob('*.0')) and not list(save.rglob('*.1'))
    elif kind=='fallback':
        run('fallback')
    elif kind=='terminalfallback':
        run('terminalfallback');assert not list(save.rglob('*.0')) and not list(save.rglob('*.1'))
    elif kind=='reset':
        run('reset')
        assert list(save.rglob('*.1')), 'preparation reset must restart and publish'
    elif kind in ['empty','scatter','incomplete','late','badchecksum']:
        run('negative');assert not list(save.rglob('*.0')) and not list(save.rglob('*.1'))
    else:
        run();base=save/'arcdroid/cmos-riscpc';slots=[max([*base.glob('*.0'),*base.glob('*.1')],key=lambda p:struct.unpack_from('<Q',p.read_bytes(),24)[0])];defaults=list((base/'defaults').glob('*.1'))
        assert len(slots)==len(defaults)==1
        b=record(defaults[0],b'ARCRPCD1',roms/(kind+'.rom'))
        expected=bytearray(240);expected[183]=2;expected[47]=3
        assert b[104:344]==expected
        assert struct.unpack_from('<IIII',b,344)==(4,1,rates[model],1)
        assert struct.unpack_from('<QQQQQ',b,360)==(946684800,0,45000000000,45000000000,5000000000)
        writes,last=struct.unpack_from('<QQ',b,400);assert writes==(238 if kind=='ro350' else 240) and 0<last<40000000000
        if kind=='delayed':assert last>18022400000
        mask=b'\0\0'+b'\xff'*30
        if kind=='ro350':mask=mask[:24]+b'\xf3'+mask[25:]
        assert b[416:448]==mask and b[448:480]==hashlib.sha256(expected).digest() and not any(b[480:508])
        ordinary=record(slots[0],b'ARCRPCM1',roms/(kind+'.rom'),'a'*40)
        # R-Y1: shared defaults stay exact; session records refresh C0/C1/3F.
        session_expected=bytearray(expected);session_expected[177]=20;session_expected[47]=23
        assert ordinary[104:344]==session_expected and not any(ordinary[344:508])
        if profile=="full":
            # Identical firmware/model/content/epoch and procedure at one versus
            # eight frames per call must produce the entire same default record.
            original_save=save;save=d/'save-one';save.mkdir();run('positive1')
            one=list((save/'arcdroid/cmos-riscpc/defaults').glob('*.1'))
            assert len(one)==1 and one[0].read_bytes()==b
            save=original_save
            # A distinct content/plan epoch reuses exactly this cache; a fresh
            # save at that epoch also independently generates identical bytes.
            other_text=text.replace('a'*40,'d'*40).replace('rtc_epoch=946684800','rtc_epoch=1234567890')
            plan.write_text(other_text);run('cache');assert defaults[0].read_bytes()==b
            original_save=save;save=d/'save-epoch';save.mkdir();run()
            epoch_defaults=list((save/'arcdroid/cmos-riscpc/defaults').glob('*.1'))
            assert len(epoch_defaults)==1 and epoch_defaults[0].read_bytes()==b
            save=original_save;plan.write_text(text)

        original=slots[0].read_bytes();run('record');assert slots[0].read_bytes()==original
        if kind=='full' and model==0 and profile=='full':
            # A p3 default is preserved but cannot satisfy the p4 procedure.
            old=bytearray(b);struct.pack_into('<I',old,344,3)
            old[104+183]=0;old[104+47]=1
            old[448:480]=hashlib.sha256(old[104:344]).digest()
            struct.pack_into('<I',old,508,zlib.crc32(old[:508]))
            previous=defaults[0].with_name(defaults[0].name.replace('-p4-','-p3-'))
            previous.write_bytes(old);defaults[0].unlink()
            # Existing per-content bytes are still authoritative, not repaired.
            run('record');assert slots[0].read_bytes()==original and not defaults[0].exists()
            plan.write_text(text.replace('a'*40,'e'*40));run()
            assert defaults[0].read_bytes()==b and previous.read_bytes()==old
            assert slots[0].read_bytes()==original
            plan.write_text(text)
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
