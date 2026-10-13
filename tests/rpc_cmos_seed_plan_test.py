#!/usr/bin/env python3
"""Seed-plus-persist admission through the real plan client. GPL-2.0-only."""
from pathlib import Path
import struct,subprocess,sys,tempfile,zlib
sys.dont_write_bytecode=True
from rpc_cmos_factory_rom import generate
exe,core,model=sys.argv[1:];model=int(model)
models=['arm610','arm710','sa110','arm7500'];rates=[20,35,100,35]
with tempfile.TemporaryDirectory(prefix='arc-rpc-cmos-seed-') as temp:
    root=Path(temp);roms=generate(root/'roms');save=root/'save';save.mkdir()
    seed=root/'seed.bin';seed.write_bytes(bytes(range(256)))
    plan=root/'seed.arcplan';plan.write_text(f'version=2\nmodel={models[model]}\nfirmware={roms/"full.rom"}\ncmos=persist\nseed={seed}\ncmos_layout=ro370\ncontent_id={"a"*40}\nrtc_epoch=946684800\n')
    def run(mode='record'):
        result=subprocess.run([exe,core,str(plan),str(roms),str(save),mode,str(rates[model]),str(model)],capture_output=True,text=True,timeout=120)
        assert result.returncode==0,result.stdout+result.stderr
    run();slots=list(save.rglob('*.1'));assert len(slots)==1
    record=slots[0];original=record.read_bytes();assert original[104:344]==bytes(range(16,256))
    assert not list(save.rglob('defaults')),'seed must not generate defaults'
    # A saved record remains authoritative when a different seed is supplied.
    seed.write_bytes(bytes(256));run();assert record.read_bytes()==original
    for foreign in (False,True):
        changed=bytearray(original)
        if foreign:
            changed[32]=ord('c');struct.pack_into('<I',changed,508,zlib.crc32(changed[:508]))
        else:changed[104]^=1
        record.write_bytes(changed);before={p.relative_to(save):p.read_bytes() for p in save.rglob('*') if p.is_file()}
        run('reject');after={p.relative_to(save):p.read_bytes() for p in save.rglob('*') if p.is_file()}
        assert before==after,'refused data was replaced by seed'
print(f'RPC seed-plus-persist model={model}: absent, existing, corrupt and wrong-key pass')
