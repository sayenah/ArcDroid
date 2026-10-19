#!/usr/bin/env python3
"""Adversarial artifact distribution boundary. GPL-2.0-only."""
import importlib.util,json,shutil,struct,sys,tempfile,zipfile
from pathlib import Path
root=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('guard',root/'tools/check-rpc-artifacts.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
manifest=Path(sys.argv[1])/'rpc-artifact-targets.json'
rows=json.loads(manifest.read_text())['targets'];core=next(r for r in rows if r['target']=='arcdroid_libretro')['file']
# Minimal PE container tests the format boundary independently of host OS.
def image(gate='closed',catalogue='production'):
    data=bytearray(128);data[:2]=b'MZ';struct.pack_into('<I',data,60,64);data[64:68]=b'PE\0\0';struct.pack_into('<H',data,86,0x2000)
    return bytes(data)+b'0'*12+b'\0'+f'ARCDROID-ROUTE-V1:gate={gate};catalogue={catalogue};end'.encode()
with tempfile.TemporaryDirectory(prefix='rpc-classifier-') as temporary:
    stage=Path(temporary)/'stage';stage.mkdir()
    for name in ('COPYING','THIRD_PARTY_NOTICES.md'):shutil.copy2(root/name,stage/name)
    (stage/'SOURCE.json').write_text(json.dumps({'revision':'0'*40,'url':'https://github.com/sayenah/ArcDroid/tree/'+'0'*40,'archive':'https://github.com/sayenah/ArcDroid/archive/'+'0'*40+'.zip'}))
    # Use a closed manifest so test is identical in the open build.
    table=Path(temporary)/'manifest.json';row=next(r for r in rows if r['file']==core).copy();row['gate_open']=row['authored']=0
    table.write_text(json.dumps({'targets':[row]}))
    binary=stage/core;binary.write_bytes(image());guard.classify(stage,table);guard.verify_core(binary,table,stage/'SOURCE.json')
    def rejected():
        try:guard.classify(stage,table)
        except (ValueError,zipfile.BadZipFile):return
        raise AssertionError('unsafe artifact accepted')
    for gate,catalogue in [('open','production'),('open','authored'),('closed','authored')]:binary.write_bytes(image(gate,catalogue));rejected()
    binary.write_bytes(image().replace(b'0'*12+b'\0',b'0'*12+b'-dirty\0'));rejected()
    binary.write_bytes(image());row['gate_open']=1;table.write_text(json.dumps({'targets':[row]}));rejected()
    row['gate_open']=0;table.write_text(json.dumps({'targets':[row]}))
    binary.write_bytes(image());renamed=stage/'renamed.dll';binary.rename(renamed);rejected();renamed.unlink()
    for suffix,data in [('.7z',b'7z\xbc\xaf\x27\x1c'),('.bin',b'Rar!'),('.bin',b'\x28\xb5\x2f\xfd'),('.dat',b'unknown')]:
        bad=stage/('hidden'+suffix);bad.write_bytes(data);rejected();bad.unlink()
    archive=stage/'fixture.zip'
    with zipfile.ZipFile(archive,'w') as out:out.writestr('renamed.data',image('open'))
    rejected();archive.unlink()
    binary.write_bytes(image());(stage/'COPYING').unlink();rejected()
print('rpc-artifact-classifier: open, authored, renamed, nested, unsupported and unlicensed artifacts rejected')
