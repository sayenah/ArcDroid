#!/usr/bin/env python3
"""Independent plan/import/store oracle; all bytes authored. GPL-2.0-only."""
from pathlib import Path
import hashlib,struct,subprocess,sys,tempfile,zlib,runpy
sys.dont_write_bytecode=True
exe,core,runner,shipping,generator=sys.argv[1:6]
with tempfile.TemporaryDirectory(prefix='arc-rpc-hd-plan-') as temp:
    d=Path(temp);system=d/'system';system.mkdir();save=d/'save';save.mkdir()
    header=d/'rom.h';old=sys.argv;sys.argv=[generator,str(header)];runpy.run_path(generator,run_name='__main__');sys.argv=old
    import re
    words=[int(x,16) for x in re.findall(r'0x([0-9a-f]{8})u',header.read_text())]
    rom=struct.pack('<'+'I'*len(words),*words);rom+=struct.pack('<I',0xeafffffe)*((2097152-len(rom))//4)
    firmware=system/'guest.rom';firmware.write_bytes(rom);digest=hashlib.sha256(rom).digest();content='a'*40
    def seed(root,model=0):
        base=root/'arcdroid/cmos-riscpc';base.mkdir(parents=True,exist_ok=True)
        b=bytearray(512);b[:8]=b'ARCRPCM1';struct.pack_into('<IIIIQ',b,8,1,512,model+1,0x10003,1)
        b[32:72]=content.encode();b[72:104]=digest;struct.pack_into('<I',b,508,zlib.crc32(b[:508]))
        (base/f'{content}-m{model+1}-{digest.hex()}.1').write_bytes(b)
    seed(save);plan=d/'test.arcplan'
    import shutil
    floppy=Path(runner).parent/'tests/fixtures/generated/arcdroid-test.adf'
    shutil.copyfile(floppy,system/'initial.adf');shutil.copyfile(floppy,system/'edited.adf')
    (system/'disk.script').write_text('0 eject\n1 add\n2 replace 1 edited.adf\n3 select 1\n4 insert\n5 eject\n6 remove 0\n7 insert\n')
    text=f'version=2\nmodel=arm610\nfirmware={firmware}\ncmos=persist\ncmos_layout=ro370\ncontent_id={content}\nrtc_epoch=946684800\nhard_disc=on\nmedia=initial.adf\ndisk_control=disk.script\n'
    plan.write_text(text)
    def run(mode='hd',model=0):
        r=subprocess.run([exe,core,str(plan),str(system),str(save),mode,'35' if model else '20',str(model)],capture_output=True,text=True,timeout=120)
        assert r.returncode==0,r.stdout+r.stderr
        if mode=='hd-frame':print(r.stdout,end='')
    r=subprocess.run([runner,'--core',shipping,'--plan',str(plan),'--save',str(save),'--reject'],capture_output=True,text=True)
    assert r.returncode==0,r.stdout+r.stderr
    run('hd-frame' if len(sys.argv)>6 else 'hd');store=save/'arcdroid/hd-riscpc'/content
    manifests=list(store.glob('manifest.?'));assert len(manifests)==1
    b=manifests[0].read_bytes();assert len(b)==24324 and b[:8]==b'ARCRHDM1' and struct.unpack_from('<I',b,24320)[0]==zlib.crc32(b[:24320])
    assert struct.unpack_from('<6I',b,64)==(512,63,16,256,262144,504)
    if len(sys.argv)==6:
        # Frontend pacing must not affect virtual devices, PCM, traces or saves.
        paced=[]
        for pacing in ('flat','burst'):
            root=d/f'paced-{pacing}';root.mkdir();seed(root)
            trace=d/f'{pacing}.csv'
            r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(system),
                '--save',str(root),'--frames','8','--pacing',pacing,'--timing',str(trace),
                '--ram-offset','0','--ram-words','7'],capture_output=True,text=True,timeout=120)
            assert r.returncode==0,r.stdout+r.stderr
            owned=root/'arcdroid/hd-riscpc'/content
            # Host elapsed time is deliberately excluded from the virtual trace.
            rows=[line.split(',') for line in trace.read_text().splitlines()]
            host=rows[0].index('core_ns')
            paced.append((r.stdout,[[v for i,v in enumerate(row) if i!=host] for row in rows],
                {p.name:p.read_bytes() for p in owned.iterdir()}))
        assert paced[0]==paced[1], 'flat/burst guest, PCM, virtual timing or store differed'
        seed(save,1);plan.write_text(text.replace('model=arm610','model=arm710'))
        run('hd-reload',1);assert len(list((save/'arcdroid/hd-riscpc').iterdir()))==1
        plan.write_text(text)
    # Published stores refuse import, preserving every existing canonical byte.
    before={p.name:p.read_bytes() for p in store.iterdir()}
    image=system/'raw.data'
    with image.open('wb') as f:f.truncate(132120576)
    original=hashlib.sha256(image.read_bytes()).digest()
    plan.write_text(text+f'hd_import={image}\n');run('reject')
    assert before=={p.name:p.read_bytes() for p in store.iterdir()}
    # Empty content store accepts the exact raw size through population hooks.
    save=d/'import-save';save.mkdir();seed(save);run()
    assert hashlib.sha256(image.read_bytes()).digest()==original
    store=save/'arcdroid/hd-riscpc'/content;b=next(store.glob('manifest.?')).read_bytes()
    assert struct.unpack_from('<I',b,128)[0]==0  # raw zeros, not formatter bytes
    # Wrong-size and contradictory fields refuse rather than creating a disc.
    save=d/'short-save';save.mkdir();seed(save);image.write_bytes(b'short');run('reject')
    plan.write_text(text.replace('hard_disc=on','hard_disc=off')+f'hd_import={image}\n');run('reject')
    print('rpc-hd-plan: real guest, authored blank, exact read-only import, published refusal, bad fields and shipping rejection pass')
