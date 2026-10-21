"""Author-only production frontend lifecycle and shipping A/B. GPL-2.0-only."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile,struct,zipfile
from rpc_cmos_factory_rom import generate
from generate_fixtures import build_extended_jfd
from rpc_video_rom import program
from rpc_save_snapshot import save_snapshot
exe,core,fixtures,mode,*extra=sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='arc-r3d3-frontend-') as temporary:
    root=Path(temporary);system=root/'system';roms=generate(system)
    firmware=system/'arcdroid/firmware';firmware.mkdir(parents=True)
    shutil.copyfile(Path(fixtures)/'arcdroid-test.rom',firmware/'authored.data')
    if mode=='shipping-override-os':
        shutil.copyfile(Path(fixtures)/'arcdroid-unsupported-launch-test.rom',firmware/'authored.data')
    content=root/'authored.jfd';image=bytearray(build_extended_jfd(bytes(256),b'Authored host fixture'))
    struct.pack_into('<I',image,304,0 if mode=='shipping-legacy' else (20<<8)|(12<<16));content.write_bytes(image)
    seed=system/'seed';seed.write_bytes(bytes(256))
    (system/'direct.arcplan').write_text(f'version=1\nmodel=sa110\nfirmware={roms/"full.rom"}\ncmos=seed\nseed={seed}\nrate_mips=1\nrtc_epoch=0\n')
    selected_rom=roms/'full.rom'
    if mode.startswith('rate') or mode=='slow':
        rate=50 if mode=='slow' else int(mode[4:]);words=program(2,32,8,3,50,'plain')
        if rate==75:words=[0x9000018e if word==0x90000256 else word for word in words]
        if mode=='slow':words=[0x90001ffe if word==0x90000256 else word for word in words]
        selected_rom=system/'video.rom';selected_rom.write_bytes(struct.pack('<%dI'%len(words),*words)+struct.pack('<I',0xeafffffe)*(524288-len(words)))
    plan=root/('HOST-PRIVATE-SENTINEL.arcplan' if mode.startswith('message-') else 'host.arcplan');plan.write_text(f'version=2\nmodel=sa110\nfirmware={selected_rom}\ncmos=persist\nseed={seed}\ncmos_layout=ro370\ncontent_id={"d"*40}\nrate_mips=1\nrtc_epoch=0\n')
    if mode.startswith('message-'):
        plan.write_text(plan.read_text()+f'message_interface={mode[8:]}\n')
    if mode.startswith('freeze'):
        image=bytearray((Path(fixtures)/'jfd-writes/rw-d.jfd').read_bytes())
        struct.pack_into('<I',image,304,struct.unpack_from('<I',image,304)[0]|2)
        selected=root/'HOST-PRIVATE-SENTINEL-content.zip'
        with zipfile.ZipFile(selected,'w') as archive:archive.writestr('HOST-PRIVATE-SENTINEL-member.jfd',image)
        helper=root/'helper.adf';shutil.copyfile(Path(fixtures)/'arcdroid-test.adf',helper)
        plan.write_text(plan.read_text()+f'media={selected}\ndrive1={helper}\n')
    if mode in ('mount-fail','helper-fail'):
        if mode=='mount-fail':
            helper=root/'helper.adf';shutil.copyfile(Path(fixtures)/'arcdroid-test.adf',helper)
            plan.write_text(plan.read_text()+f'drive1={helper}\n')
        else:
            from rpc_boot_packages_test import inputs
            packages=inputs(root/'boot');shutil.copytree(packages/'arcdroid/riscpc',system/'arcdroid/riscpc')
            plan.write_text(plan.read_text()+'hard_disc=on\nhd_populate=on\n')
    shutil.copyfile(Path(fixtures)/'arcdroid-test.adf',system/'replacement.adf')
    (system/'HOST-PRIVATE-SENTINEL-invalid.zip').write_bytes(b'authored invalid archive')
    outputs=[];stores=[]
    for i,library in enumerate([core,*extra]):
        save=root/(f'HOST-PRIVATE-SENTINEL-save{i}' if mode.startswith(('freeze','message-')) else f'save{i}');save.mkdir()
        command=[exe,library,str(content if mode.startswith('shipping') else plan),str(system),str(save),mode]
        if not mode.startswith('shipping'):
            copied=root/('secondary'+Path(library).suffix);shutil.copyfile(library,copied);command.append(str(copied))
        env=dict(os.environ)
        if not mode.startswith('shipping'):
            # Deliberate dlopen of a copied C module; duplicate exported global
            # names are expected. Memory and undefined-behaviour checks stay on.
            env['ASAN_OPTIONS']=env.get('ASAN_OPTIONS','')+':detect_odr_violation=0'
        run=subprocess.run(command,capture_output=True,text=True,timeout=120,env=env)
        assert run.returncode==0,run.stdout+run.stderr
        output=run.stdout
        if mode.startswith('shipping-override'):
            old='This title is flagged for RiscPC-class machines (StrongARM/RISC OS 3.5+), which ArcDroid does not emulate; booting anyway.'
            new='This title is flagged for RiscPC-class machines; starting on A3000 because it is selected in core options.'
            if not extra or i:
                lines=output.splitlines()
                assert sum(line==f'log:1:N-9: {new}' for line in lines)==1,output
                assert sum(line.startswith('message:') and line.endswith(':'+new) for line in lines)==1,output
                assert old not in output,output
            else:assert 'N-9:' not in output and old in output,output
            output='\n'.join(line for line in output.split('\n') if 'N-9:' not in line and new not in line and old not in line)
        outputs.append(output.replace(str(save),'<save>').replace(str(root),'<case>'))
        stores.append(save_snapshot(save))
    if extra:
        assert outputs[0]==outputs[1], '\n'.join(__import__('difflib').unified_diff(outputs[0].splitlines(),outputs[1].splitlines()))
        assert stores[0]==stores[1] and stores[0], 'save projection differs: '+repr(sorted(name for name in stores[0].keys()|stores[1].keys() if stores[0].get(name)!=stores[1].get(name)))
        assert not any('riscpc' in name for name in stores[0])
        label='unflagged' if mode=='shipping-legacy' else 'RPC-flagged'
        print(f'shipping ordinary {label} content: complete log/message/AV/media/audio projection and {len(stores[0])} persisted/staged files byte-identical; G-3 excluded')
    else:print(outputs[0],end='')
