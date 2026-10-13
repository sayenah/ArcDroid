"""Recovery advisories and terminal precedence through the real frontend. GPL-2.0-only."""
from pathlib import Path
import shutil,subprocess,sys,tempfile
from rpc_cmos_factory_rom import generate
exe,core=sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='arc-hd-notices-') as temp:
    root=Path(temp);roms=generate(root/'roms');seed=root/'seed';seed.write_bytes(bytes(range(256)))
    plan=root/'test.arcplan';plan.write_text(f'version=2\nmodel=arm7500\nfirmware={roms/"full.rom"}\ncmos=persist\nseed={seed}\ncmos_layout=ro370\ncontent_id={"a"*40}\nrtc_epoch=946684800\nhard_disc=on\n')
    baseline=root/'baseline';baseline.mkdir()
    def run(save,mode):
        r=subprocess.run([exe,core,str(plan),str(roms),str(save),mode,'35','3'],capture_output=True,text=True,timeout=60)
        assert r.returncode==0,r.stdout+r.stderr
    run(baseline,'record')
    for mode in ('hd-advisory','hd-advisory-ro','hd-advisory-fallback','hd-advisory-ro-fallback',
                 'hd-advisory-retained','hd-advisory-retained-fallback'):
        save=root/mode;shutil.copytree(baseline,save)
        journals=list((save/'arcdroid/hd-riscpc'/('a'*40)).glob('journal.?'));assert journals
        for p in journals:
            with p.open('ab') as f:f.write(b'partial')
        if 'retained' in mode:
            record=next((save/'arcdroid/cmos-riscpc').glob('*.1'))
            record.with_suffix('.0').write_bytes(b'ARCCMOS2'+bytes(504))
        run(save,mode)
        print(mode+': passed')
