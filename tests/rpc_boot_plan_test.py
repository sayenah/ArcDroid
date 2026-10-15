#!/usr/bin/env python3
"""Populated plan v2 through ordinary load/store/IDE; shipping refusal. GPL-2.0-only."""
from pathlib import Path
import hashlib
import re
import runpy
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from rpc_boot_packages_test import inputs, require

exe,core,runner,shipping,generator,fixtures=sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='arcdroid-boot-plan-') as temp:
    root=Path(temp);system=inputs(root);save=root/'save';save.mkdir()
    header=root/'rom.h';before=sys.argv;sys.argv=[generator,str(header)]
    runpy.run_path(generator,run_name='__main__');sys.argv=before
    words=[int(v,16) for v in re.findall(r'0x([0-9a-f]{8})u',header.read_text())]
    rom=struct.pack('<'+'I'*len(words),*words)
    rom+=struct.pack('<I',0xeafffffe)*((2097152-len(rom))//4)
    firmware=system/'guest.rom';firmware.write_bytes(rom);digest=hashlib.sha256(rom).digest();content='a'*40
    def seed(directory):
        base=directory/'arcdroid/cmos-riscpc';base.mkdir(parents=True,exist_ok=True)
        record=bytearray(512);record[:8]=b'ARCRPCM1';struct.pack_into('<IIIIQ',record,8,1,512,1,0x10003,1)
        record[32:72]=content.encode();record[72:104]=digest;struct.pack_into('<I',record,508,zlib.crc32(record[:508]))
        (base/f'{content}-m1-{digest.hex()}.1').write_bytes(record)
    seed(save);plan=root/'test.arcplan'
    text=f'version=2\nmodel=arm610\nfirmware={firmware}\ncmos=persist\ncmos_layout=ro370\ncontent_id={content}\nrtc_epoch=946684800\nhd_populate=on\n'
    plan.write_text(text)
    def call(mode='hd'):
        result=subprocess.run([exe,core,str(plan),str(system),str(save),mode,'20','0'],capture_output=True,text=True,timeout=180)
        require(result.returncode==0,result.stdout+result.stderr)
    call();owned=save/'arcdroid/hd-riscpc'/content;manifest=next(owned.glob('manifest.?')).read_bytes()
    require(len(manifest)==24452 and manifest[24320:24328]==b'ARCRBOOT','ordinary plan did not populate with receipt')
    # Exercise the plan parser, not just pure discovery: encoded JFD bytes
    # must never take the raw-sector path ahead of their decoded view.
    primary_save=save;save=root/'jfd-save';shutil.copytree(primary_save,save)
    shutil.copyfile(Path(fixtures)/'launch-family/single-d-hugo-extended.jfd',system/'launch.jfd')
    plan.write_text(text+'media=launch.jfd\nlaunch_action=application\nlaunch_session=1\n')
    call('hd-reload')
    reset_command=[runner,'--core',core,'--plan',str(plan),'--system',str(system),'--save',str(save),'--frames','1']
    result=subprocess.run(reset_command+['--preparation-reset-stage','prepared','--reset-stage-frame','0'],capture_output=True,text=True,timeout=180)
    require(result.returncode==0 and 'reset at stage=prepared delivery=1 outcome=0' in result.stderr and
            'RPC attempt: delivery=5 outcome=8' in result.stderr,'runner did not reset the prepared attempt')
    for invalid in (['--preparation-reset-stage','other'],
                    ['--preparation-reset-stage','prepared','--preparation-reset-at','0'],
                    ['--preparation-reset-stage','prepared','--reset-stage-frame','1']):
        result=subprocess.run(reset_command+invalid,capture_output=True,text=True,timeout=180)
        require(result.returncode==2,'invalid reset-stage command admitted')
    plan.write_text(text+'media=launch.jfd\nlaunch_action=boot\nlaunch_session=2\n')
    call('reject')  # No discovered root !Boot.
    save=primary_save;plan.write_text(text)
    # Source packages may disappear after creation: the receipt is a no-op.
    shutil.rmtree(system/'arcdroid/riscpc/boot');call('hd-reload')
    result=subprocess.run([runner,'--core',shipping,'--plan',str(plan),'--save',str(save),'--reject'],capture_output=True,text=True,timeout=180)
    require(result.returncode==0,'shipping accepted populated RPC plan')
    for addition in ('hard_disc=off\n','hd_import=absent.data\n'):
        plan.write_text(text+addition);call('reject')
    plan.write_text(text.replace('version=2','version=1'));call('reject')
    # Fresh store with packages absent fails visibly and publishes no blank.
    save=root/'missing-save';save.mkdir();seed(save);plan.write_text(text);call('reject')
    require(not list((save/'arcdroid/hd-riscpc'/content).glob('manifest.?')),'missing package published blank')
    print('rpc-boot-plan: populated ordinary open, receipt reopen, real guest IDE, invalid fields and shipping refusal pass')
