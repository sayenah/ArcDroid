"""Production module harness using only authored inputs. GPL-2.0-only."""
from pathlib import Path
import struct,subprocess,sys,tempfile
from rpc_boot_packages_test import inputs
from rpc_cmos_factory_rom import generate, factory
exe,fixtures,profile=sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='arcdroid-orchestration-') as temp:
    root=Path(temp)
    system=inputs(root)
    roms=generate(root/'roms')
    if profile=='sanitizer':
        # Keep the complete factory write sequence and 45 s oracle, but idle
        # on a production SA-110 after the last payload write. The ordinary
        # host matrix and per-model CMOS sanitizer cases retain busy-loop ROMs.
        words=factory('full').words
        idle=max(i for i,w in enumerate(words) if w==0xeafffffe)
        words[idle]=0xee0f0f58  # existing rpc_session_rom SA-110 idle instruction
        (roms/'full.rom').write_bytes(struct.pack('<%dI'%len(words),*words)+struct.pack('<I',0xeafffffe)*(524288-len(words)))
        profile='sanitizer-idle'
    subprocess.run([exe,str(system),str(roms/'full.rom'),fixtures,profile],check=True,timeout=240)
