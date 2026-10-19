#!/usr/bin/env python3
"""Authored entry-point evidence; no private inputs. GPL-2.0-only."""
from pathlib import Path
import shutil
import subprocess
import sys
import struct
import zipfile
from generate_fixtures import build_extended_jfd
import tempfile

exe,test,shipping,fixtures,legacy_fixtures,gate_open,fault_test=sys.argv[1:8]
only_case=sys.argv[8] if len(sys.argv)>8 else None
fixtures=Path(fixtures)
with tempfile.TemporaryDirectory(prefix='arcdroid-route-') as temporary:
    root=Path(temporary)
    # RetroArch keeps the core basename inside retroarch_temp. Distinct paths
    # with equal basenames must still identify distinct loaded modules.
    (root/'retroarch_temp').mkdir()
    secondary=root/'retroarch_temp'/Path(test).name;shutil.copy2(test,secondary)
    identity=subprocess.run([exe,fault_test,str(fixtures/'system'),str(root),str(fixtures/'route.jfd'),'metadata-fault',str(secondary)],capture_output=True,text=True,timeout=30)
    assert identity.returncode==0,identity.stdout+identity.stderr
    for mode in ([] if only_case else ['extended','legacy','primary-menu','primary-cli','primary-arc','cycles','changed-packages','namespace-file','empty-after-probe','chooser-cycles']+(['missing'] if gate_open=='1' else [])):
        work=root/mode;work.mkdir();save=work/'save';save.mkdir()
        system=work/'system';shutil.copytree(fixtures/'system',system)
        library=shipping if mode=='missing' else fault_test if mode=='chooser-cycles' else test
        shutil.copy2(library,secondary)
        if mode in ('cycles','chooser-cycles'):
            shutil.copy2(Path(legacy_fixtures)/'arcdroid-test.rom',system/'arc-control.rom')
            shutil.copy2(Path(legacy_fixtures)/'arcdroid-test.adf',system/'arc-control.adf')
            shutil.copy2(Path(legacy_fixtures)/'arcdroid-chooser-l.adl',system/'arc-chooser.adl')
        disc=fixtures/'route.jfd'
        if mode=='primary-arc':
            shutil.copy2(Path(legacy_fixtures)/'arcdroid-test.rom',system/'arcdroid/firmware/control.rom')
            shutil.copy2(Path(legacy_fixtures)/'arcdroid-test.adf',system/'arc-control.adf')
            disc=system/'arc-control.adf'
        if mode=='missing':shutil.rmtree(system/'arcdroid/firmware')
        # The full 200-cycle ASan run measured 387 s under the PR-tier load.
        # Keep every cycle/assertion; its watchdog is not a frame-cost limit.
        subprocess.run([exe,library,str(system),str(save),str(disc),mode,str(secondary)],check=True,timeout=720 if mode=='cycles' else 360)
        if mode=='namespace-file':assert (save/'arcdroid/hd-riscpc').read_text()=='authored namespace change'
    shutil.copy2(test,secondary)
    # A late flag is never used to relabel a failure that occurred earlier.
    for mode in ([] if only_case else ['failure-raw','failure-safe','discarded']):
        work=root/mode;work.mkdir();save=work/'save';save.mkdir();system=work/'system';shutil.copytree(fixtures/'system',system)
        image=bytearray(build_extended_jfd(bytes(256),b'ROUTE-PRIVATE-SENTINEL'))
        struct.pack_into('<I',image,304,(16<<8)|(8<<16) if mode=='failure-safe' else 0)
        selected=work/'selected.jfd';selected.write_bytes(image)
        if mode=='discarded':
            directory=work/'set.jfd';directory.mkdir();selected=directory/'set.jfd';selected.write_bytes(image)
            image=bytearray(build_extended_jfd(bytes([1])*256,b'ROUTE-PRIVATE-SENTINEL'));struct.pack_into('<I',image,304,(16<<8)|(8<<16));(directory/'peer.jfd').write_bytes(image)
            shutil.copy2(Path(legacy_fixtures)/'arcdroid-test.rom',system/'arcdroid/firmware/control.rom')
        else:
            playlist=work/'failure.m3u';playlist.write_text(str(selected)+'\n'+str(work/'missing.jfd')+'\n'+str(fixtures/'route.jfd')+'\n');selected=playlist
        subprocess.run([exe,test,str(system),str(save),str(selected),mode,str(secondary)],check=True,timeout=360)
    # Structured class-A failures through loose, ZIP, playlist and mixed sets.
    for case,reason in [('empty','N-1'),('missing-folder','N-5'),('unsupported','N-3'),('unqualified','N-2'),('conflict','N-4'),('bounded','N-5 incomplete-scan'),('archive-hd','N-6'),('archive-system','N-6'),('archive-unreadable','N-20'),('preflight-unavailable','N-19'),('malformed','N-1'),('unreadable-unrelated','N-1'),('unreadable-candidate','N-5'),('locator-change','N-B firmware'),('system-unavailable','N-5')]:
        if only_case and case!=only_case:continue
        for container in ['loose','zip','playlist','mixed']:
            work=root/(case+'-'+container);work.mkdir();save=work/'save';save.mkdir();system=work/'system';shutil.copytree(fixtures/'system',system)
            firmware=system/'arcdroid/firmware'
            if case in ('empty','missing-folder','malformed','unreadable-unrelated','unreadable-candidate'):
                for rom in firmware.glob('*'):rom.unlink()
                if case=='missing-folder':firmware.rmdir()
                if case=='malformed':(firmware/'malformed.rom').write_bytes(b'authored invalid firmware')
            if case.startswith('unreadable-'):(firmware/'unreadable.rom').write_bytes(b'x'*(2097152 if case=='unreadable-candidate' else 1))
            if case=='bounded':
                for i in range(1050):(firmware/f'entry-{i}').write_bytes(b'')
            if case.startswith('archive-'):(system/'arcdroid/riscpc/boot'/('HardDisc4.zip' if case=='archive-hd' else 'PlingSystem.zip')).unlink()
            image=bytearray((fixtures/'route.jfd').read_bytes())
            if case=='unsupported':struct.pack_into('<I',image,304,(32<<8)|(64<<16))
            if case=='unqualified':struct.pack_into('<I',image,304,(16<<8)|(4<<16))
            disc=work/'ROUTE-PRIVATE-SENTINEL.jfd';disc.write_bytes(image);selected=disc
            if container=='zip':
                selected=work/'ROUTE-PRIVATE-SENTINEL.zip'
                with zipfile.ZipFile(selected,'w') as archive:archive.writestr('ROUTE-PRIVATE-SENTINEL.jfd',image)
            if case=='conflict':
                other=work/'other.jfd';struct.pack_into('<I',image,304,(4<<8)|(4<<16));other.write_bytes(image)
                if container=='zip':
                    selected=work/'set.zip'
                    with zipfile.ZipFile(selected,'w') as archive:
                        archive.writestr('disc.jfd',disc.read_bytes());archive.writestr('other.jfd',image)
                elif container=='loose':
                    directory=work/'set.jfd';directory.mkdir();selected=directory/'set.jfd';selected.write_bytes(disc.read_bytes());(directory/'other.jfd').write_bytes(image)
                else:
                    selected=work/'set.m3u';selected.write_text(str(disc)+'\n'+str(other)+'\n')
            elif container=='playlist':
                selected=work/'set.m3u';selected.write_text(str(disc)+'\n')
            if container=='mixed':
                raw=work/'raw.adf';shutil.copy2(Path(legacy_fixtures)/'arcdroid-test.adf',raw)
                lines=selected.read_text() if selected.suffix=='.m3u' else str(selected)+'\n'
                selected=work/'mixed.m3u';selected.write_text(str(raw)+'\n'+lines)
            result=subprocess.run([exe,test,str(system),str(save),str(selected),('system-unavailable' if case=='system-unavailable' else 'refuse:'+reason),str(secondary)],check=True,capture_output=True,text=True,timeout=360)
            if reason=='N-6':
                missing='HardDisc4.zip' if case=='archive-hd' else 'PlingSystem.zip'
                expected=f'N-6 text: First use of a RiscPC title needs {missing} in <system>/arcdroid/riscpc/boot/. Add the missing files, then close and reload this content.'
                assert result.stdout.splitlines().count(expected)==1,result.stdout+result.stderr
            assert not (save/'arcdroid/hd-riscpc').exists() and not (save/'arcdroid/cmos-riscpc').exists()
    if only_case:
        print('rpc-route: targeted ordinary refusal carriers complete');sys.exit(0)
    # Every qualified pair; no production qualification cells are enabled.
    pairs=[(4,4,1),(4,4,2),(4,8,4),(16,8,3),(16,8,4),(8,4,2),(8,8,4)]
    for cpu,os_bits,os in pairs:
        work=root/f'pair-{cpu}-{os}';work.mkdir();save=work/'save';save.mkdir();system=work/'system';shutil.copytree(fixtures/'system',system)
        for rom in (system/'arcdroid/firmware').glob('*'):
            if rom.name!=['','ro350.rom','ro360.rom','ro370.rom','ro371.rom'][os]:rom.unlink()
        image=bytearray((fixtures/'route.jfd').read_bytes());struct.pack_into('<I',image,304,(cpu<<8)|(os_bits<<16));disc=work/'ROUTE-PRIVATE-SENTINEL.jfd';disc.write_bytes(image)
        subprocess.run([exe,test,str(system),str(save),str(disc),'admit',str(secondary)],check=True,timeout=360)
    # Loose/single-ZIP/folder keys match; playlist identity stays domain-separated.
    work=root/'identity';work.mkdir();save=work/'save';save.mkdir();system=work/'system';shutil.copytree(fixtures/'system',system)
    image=(fixtures/'route.jfd').read_bytes();loose=work/'disc.jfd';loose.write_bytes(image)
    zipped=work/'disc.zip'
    with zipfile.ZipFile(zipped,'w') as archive:archive.writestr('disc.jfd',image)
    playlist=work/'set.m3u';playlist.write_text(str(loose)+'\n')
    folder=work/'folder.jfd';folder.mkdir();(folder/'folder.jfd').write_bytes(image)
    for selected in [loose,zipped,folder/'folder.jfd']:
        subprocess.run([exe,test,str(system),str(save),str(selected),'admit',str(secondary)],check=True,timeout=360)
        assert len(list((save/'arcdroid/hd-riscpc').iterdir()))==1
    subprocess.run([exe,test,str(system),str(save),str(playlist),'admit',str(secondary)],check=True,timeout=360)
    assert len(list((save/'arcdroid/hd-riscpc').iterdir()))==2
    # Same content-only HD with a changing ranked pair; CMOS remains per pair.
    work=root/'drift';work.mkdir();save=work/'save';save.mkdir();system=work/'system';shutil.copytree(fixtures/'system',system)
    image=bytearray((fixtures/'route.jfd').read_bytes());struct.pack_into('<I',image,304,(28<<8)|(12<<16));disc=work/'drift.jfd';disc.write_bytes(image)
    firmware=system/'arcdroid/firmware'
    for rom in firmware.glob('*'):rom.unlink()
    for os,expected in [(1,'admit'),(2,'drift'),(1,'drift'),(2,'drift')]:
        for rom in firmware.glob('*'):rom.unlink()
        name=['','ro350.rom','ro360.rom'][os];shutil.copy2(fixtures/'system/arcdroid/firmware'/name,firmware/name)
        subprocess.run([exe,test,str(system),str(save),str(disc),expected,str(secondary)],check=True,timeout=360)
    assert len(list((save/'arcdroid/hd-riscpc').iterdir()))==1
print('rpc-route: test-core ordinary path and shipping no-firmware refusal complete')
