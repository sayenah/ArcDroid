"""Production module harness using only authored inputs. GPL-2.0-only."""
from pathlib import Path
import struct,subprocess,sys,tempfile
from rpc_boot_packages_test import inputs, package, BASE
from rpc_cmos_factory_rom import generate, factory
from rpc_launch_controller_guest import firmware
exe,fixtures,profile=sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='arcdroid-orchestration-') as temp:
    root=Path(temp)
    system=inputs(root)
    many=dict(BASE)
    many.update({f'HardDisc4/Cap/D{d:02}/F{f:02}':(b'authored cap',0xffffff00,0,3) for d in range(32) for f in range(32)})
    package(system/'cap.zip',many)
    products=dict(BASE);products['HardDisc4/Apps/!Example/!Run']=(b'authored installed command',0xffffff00,0,3)
    package(system/'product.zip',products)
    roms=generate(root/'roms')
    (system/'guest-return.rom').write_bytes(firmware(b''))
    (system/'guest-reset.rom').write_bytes(firmware(b'',failure='reset-during'))
    if profile in ('sanitizer','mutant-idle'):
        # Keep the complete factory write sequence and 45 s oracle, but idle
        # on a production SA-110 after the last payload write. The ordinary
        # host matrix and per-model CMOS sanitizer cases retain busy-loop ROMs.
        words=factory('full').words
        idle=max(i for i,w in enumerate(words) if w==0xeafffffe)
        words[idle]=0xee0f0f58  # existing rpc_session_rom SA-110 idle instruction
        (roms/'full.rom').write_bytes(struct.pack('<%dI'%len(words),*words)+struct.pack('<I',0xeafffffe)*(524288-len(words)))
        profile='mutant-idle' if profile=='mutant-idle' else 'sanitizer-idle'
    subprocess.run([exe,str(system),str(roms/'full.rom'),fixtures,profile],check=True,timeout=900)
