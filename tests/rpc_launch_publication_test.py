#!/usr/bin/env python3
"""Established publication through real frontend/preparation and authored ARM.
No private firmware, bytes or paths. GPL-2.0-only.
"""
from pathlib import Path
import hashlib, struct, subprocess, sys, tempfile, zlib
from rpc_boot_packages_test import inputs
from rpc_launch_qualification import disc
from rpc_launch_controller_guest import firmware
runner,core,builder,fixtures=sys.argv[1:]
fixtures=Path(fixtures).resolve()
with tempfile.TemporaryDirectory(prefix='arc-launch-publication-') as tmp:
    root=Path(tmp);system=inputs(root)
    raw=bytes(disc('returning'));media=system/'launch.adl';media.write_bytes(raw)
    def run(label,writes='off',cells=3,swap=False,ordered=False,jfd=False,one=False,seed_drives=2,child=0):
        folder=root/label;folder.mkdir(exist_ok=True);save=folder/'save';save.mkdir(exist_ok=True)
        members=[raw];source=media;action=1;target=b'ADFS::0.$.!Boot'
        if ordered:
            second=system/'second.adl';second.write_bytes(bytes(disc('blocked')))
            source=system/'set.m3u';source.write_text(str(media)+'\n'+str(second)+'\n');members.append(second.read_bytes())
        if jfd:
            source=Path(fixtures)/'launch-family/single-d-hugo-extended.jfd'
            members=[source.read_bytes()];action=2;target=b'ADFS::0.$.!Demo'
        h=hashlib.sha256(b'ArcDroid RPC ordered media 1\0')
        for b in members:h.update(struct.pack('<Q',len(b)));h.update(b)
        wire=struct.pack('<8sIIII32sII',b'ARCRLPLN',2,64+len(target),action,0,h.digest(),len(target),0)+target
        encoded=folder/'plan.bin';encoded.write_bytes(wire);image=folder/'helper.bin'
        subprocess.run([builder,'--bound',str(image),str(encoded)],check=True,capture_output=True)
        rom=folder/'authored.rom';rom.write_bytes(firmware(image.read_bytes(),child=child))
        seed=bytearray(256);seed[199]=seed_drives;seed[63]=seed_drives+1;seed_path=folder/'seed';seed_path.write_bytes(seed)
        script=folder/'swap';script.write_text('10 eject\n11 insert\n')
        plan=folder/'case.arcplan';plan.write_text(f'version=2\nmodel=arm610\nfirmware={rom}\ncmos=persist\nseed={seed_path}\ncmos_layout=ro370\ncontent_id={"a"*40}\nrtc_epoch=946684800\nhd_populate=on\nmedia={source}\nlaunch_action={"application" if action==2 else "boot"}\nlaunch_session={0x123456789abcdef}\nobserver_cells={cells}\n'+(f'disk_control={script}\n' if swap else ''))
        p=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(system),'--save',str(save),'--disk-writes',writes,'--frames','20'],capture_output=True,text=True,timeout=180)
        assert p.returncode==0,p.stderr[-2000:]
        if one:
            assert 'unconfigured' in p.stderr and 'delivery=0 outcome=0' in p.stderr
            record=next((save/'arcdroid/cmos-riscpc').glob('*.1')).read_bytes()
            assert record[104+183]==1,'per-content configuration was repaired'
        elif cells:assert f'delivery=4 outcome={5 if child else 3}' in p.stderr,p.stderr[-2000:]
        path=save/'arcdroid/hd-riscpc'/('a'*40)/'launch.observation'
        expected=writes=='off' and cells and not swap and not one
        assert path.exists()==bool(expected),(label,'publication',path.exists(),p.stderr[-1000:])
        if expected:
            b=path.read_bytes();assert len(b)==288 and b[:8]==b'ARCRBOOT'
            assert struct.unpack_from('<I',b,284)[0]==zlib.crc32(b[:284])
            return b
    first=run('readonly');assert first==run('readonly'),'clean reopen changed bindings'
    run('readonly',cells=0)  # Old record removed; unsupported outcome cannot republish.
    run('readonly');run('readonly','shadow')  # A writable reopen also retires the old observation.
    run('desktop',child=99)
    run('readonly-set',ordered=True);run('readonly-jfd',jfd=True)
    run('raw-shadow','shadow');run('raw-original','original');run('jfd-shadow','shadow',jfd=True)
    run('media-change',swap=True)
    run('one-drive',one=True,seed_drives=1);run('one-drive',one=True,seed_drives=2)
print('publication: actual controller, readonly raw/JFD/ordered sets and clean reopen; writable/unsupported/media-change refuse')
