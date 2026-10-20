"""QA's RetroArch-ordered shipping ADF and hd.arcplan cases. GPL-2.0-only."""
from pathlib import Path
import shutil,subprocess,sys,tempfile
from rpc_hd_retroarch_rom import fixtures as hd_fixtures

exe,core,fixtures,kind=sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='rpc-primary-order-') as temporary:
    root=Path(temporary)
    if kind=='plan':
        system=root/'rpc';hd_fixtures(system);save=system/'save';content=system/'hd.arcplan';mode='reordered-plan'
    else:
        system=root/'system';firmware=system/'arcdroid/firmware';firmware.mkdir(parents=True)
        shutil.copy2(Path(fixtures)/'arcdroid-test.rom',firmware/'control.rom')
        content=root/'control.adf';shutil.copy2(Path(fixtures)/'arcdroid-test.adf',content)
        save=root/'save';save.mkdir();mode='shipping-reordered'
    run=subprocess.run([exe,core,content,system,save,mode],capture_output=True,text=True,timeout=120)
    assert run.returncode==0,run.stdout+run.stderr
    assert 'load before callbacks, five frames and clean teardown pass' in run.stdout
    print('RetroArch primary sequence:',kind,'passed')
