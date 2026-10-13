#!/usr/bin/env python3
"""HD timing is observational; every measured frame and phase is reported. GPL-2.0-only."""
import csv
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from rpc_hd_retroarch_test import pixels, store_check


def main():
    runner, core, fixture, checker = map(lambda p: Path(p).resolve(), sys.argv[1:5])
    sanitizer = "--sanitizer" in sys.argv[5:]
    with tempfile.TemporaryDirectory(prefix='arc-hd-cost-') as temp:
        root = Path(temp)
        traces = []
        stores = []
        for measured in (False, True):
            save = root / str(measured)
            shutil.copytree(fixture / 'save', save)
            shot = root / f'{measured}.ppm'
            report = root / 'hd.csv'
            args = [str(runner), '--core', str(core), '--plan', str(fixture / 'hd.arcplan'),
                    '--system', str(fixture), '--save', str(save), '--vfs-flush', 'retroarch', '--frames', '700',
                    '--dump-frame', '699', str(shot)]
            if measured:
                args += ['--hd-cost', str(report)]
            result = subprocess.run(args, capture_output=True, text=True, timeout=180)
            assert result.returncode == 0, result.stderr
            traces.append(result.stdout)
            pixels(shot, 1)
            store_check(save, 2)
            stores.append({p.relative_to(save): p.read_bytes() for p in save.rglob('*') if p.is_file()})
        assert traces[0] == traces[1], 'timing changed virtual execution'
        assert stores[0] == stores[1], 'timing changed persistent bytes'
        rows = list(csv.DictReader(report.open()))
        assert len(rows) == 700 and all(r['attached'] == '1' for r in rows)
        assert sum(int(r['write_commands']) for r in rows) >= 17
        assert any(int(r['acknowledgement_ns']) > 0 for r in rows)
        assert any(int(r['read_commands']) > 0 and int(r['read_ns']) > 0 for r in rows)
        assert {'ROTATE', 'MATERIALIZE', 'MANIFEST', 'PUBLISH', 'RETIRE'} <= {r['phase'] for r in rows}
        assert {r['substep'] for r in rows if r['phase']=='RETIRE'} == {
            'manifest', 'header', 'length-binding', 'remove-manifest', 'remove-journal'}
        assert any(r['namespace_operations']=='1' for r in rows)
        assert all(r['namespace_operations'] in ('0','1') and r['substep'] for r in rows)
        assert all(r['namespace_operations']=='0' for r in rows if r['substep']=='reconstruct')
        assert int(rows[-1]['checkpoints']) >= 2
        # The runner summary and CSV must agree, even on a loaded host.
        summary = re.search(r'hd-cost frames=700 max_ns=(\d+) over_D161=(\d+) budget=(exceeded|passed)', result.stderr)
        assert summary, result.stderr
        over = sum(int(r['total_ns']) > int(r['T_ns']) // (2 if r['namespace_operations']=='1' else 5)
                   or int(r['acknowledgement_ns']) > int(r['T_ns']) // 5 for r in rows)
        assert int(summary[1]) == max(int(r['total_ns']) for r in rows)
        assert int(summary[2]) == over and summary[3] == ('exceeded' if over else 'passed')
        # Wall-clock qualification belongs to the Odin3; exercise the checker
        # deterministically without asserting a budget on a loaded CI host.
        # Both fractional limits are exact integer nanoseconds at this period.
        for row in rows:
            row['T_ns'] = '20000000'
            row['open_ns'] = row['read_ns'] = row['acknowledgement_ns'] = '0'
            row['maintenance_ns'] = row['total_ns'] = str(int(row['T_ns']) // (2 if row['namespace_operations']=='1' else 5))
        def write_rows():
            with report.open('w', newline='') as output:
                writer = csv.DictWriter(output, fieldnames=rows[0].keys())
                writer.writeheader(); writer.writerows(rows)
        write_rows()
        check_args = [sys.executable, '-B', str(checker), str(report), '--frames', '700']
        assert subprocess.run(check_args, capture_output=True).returncode == 0
        assert subprocess.run(check_args[:-1] + ['701'], capture_output=True).returncode == 2
        ordinary = next(r for r in rows if r['namespace_operations']=='0')
        namespaced = next(r for r in rows if r['namespace_operations']=='1')
        for row, divisor in ((ordinary,5),(namespaced,2)):
            boundary = int(row['T_ns']) // divisor
            row['total_ns'] = row['maintenance_ns'] = str(boundary)
            write_rows()
            checked = subprocess.run(check_args, capture_output=True, text=True)
            assert checked.returncode == 0 and 'over_D161=0' in checked.stdout, checked
            row['total_ns'] = row['maintenance_ns'] = str(boundary+1)
            write_rows()
            checked = subprocess.run(check_args, capture_output=True, text=True)
            assert checked.returncode == 1 and 'over_D161=1' in checked.stdout, checked
            row['total_ns'] = row['maintenance_ns'] = str(boundary)
        # The relaxed maintenance tick never relaxes acknowledgement.
        namespaced['acknowledgement_ns'] = str(int(namespaced['T_ns'])//5+1)
        namespaced['maintenance_ns'] = '0'; namespaced['total_ns'] = namespaced['acknowledgement_ns']
        write_rows()
        assert subprocess.run(check_args, capture_output=True).returncode == 1
        namespaced['acknowledgement_ns']='0'
        namespaced['total_ns']=namespaced['maintenance_ns']=str(int(namespaced['T_ns'])//2)
        for key,value in (('namespace_operations','2'),('substep','unknown')):
            previous=namespaced[key];namespaced[key]=value;write_rows()
            assert subprocess.run(check_args, capture_output=True).returncode == 2
            namespaced[key]=previous
        # Exercise the two independent shape rules without another rule
        # rejecting the row first (QA C3 and C6).
        original = namespaced.copy()
        namespaced.update(phase='MATERIALIZE', substep='verify', namespace_operations='0',
                          maintenance_ns='1', total_ns='1')
        write_rows()
        assert subprocess.run(check_args, capture_output=True).returncode == 0
        namespaced['namespace_operations']='1';write_rows()
        checked = subprocess.run(check_args, capture_output=True, text=True)
        assert checked.returncode == 2 and 'namespace call in read-only substep' in checked.stderr, checked
        namespaced.update(namespace_operations='0', substep='unknown');write_rows()
        checked = subprocess.run(check_args, capture_output=True, text=True)
        assert checked.returncode == 2 and 'unknown maintenance substep' in checked.stderr, checked
        namespaced.clear();namespaced.update(original)
        # Zero namespace calls cannot earn the larger allowance by phase alone.
        namespaced['namespace_operations']='0';write_rows()
        assert subprocess.run(check_args, capture_output=True).returncode == 1
        namespaced['namespace_operations']='1';write_rows()
        plan = root / 'off.arcplan'
        plan.write_text((fixture / 'hd.arcplan').read_text().replace('hard_disc=on', 'hard_disc=off'))
        # Make the relative ROM/media/input paths absolute after moving the plan.
        plan.write_text(plan.read_text().replace('firmware=hd.rom', f'firmware={fixture / "hd.rom"}')
                        .replace('media=floppy.adf', f'media={fixture / "floppy.adf"}')
                        .replace('disk_control=disk.script', f'disk_control={fixture / "disk.script"}'))
        result = subprocess.run([str(runner), '--core', str(core), '--plan', str(plan),
                                 '--system', str(fixture), '--save', str(root / 'True'),
                                 '--frames', '12', '--hd-cost', str(report)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert subprocess.run([sys.executable, '-B', str(checker), str(report)], capture_output=True).returncode == 0
        assert all(r['attached'] == '0' for r in csv.DictReader(report.open()))
        # First use is measured across complete preparation frames in its own
        # CSV. Running maintenance no longer includes synchronous open work.
        from rpc_cmos_factory_rom import generate, factory as factory_guest
        factory = generate(root / 'factory')
        cold_model = 'arm610'
        cold_frames = '345'
        if sanitizer:
            # The 700-frame live workload above stays exhaustive. Cold setup
            # retains every preparation state and the 45 s capture, using the
            # existing SA idle instruction after the complete CMOS write.
            import struct
            words=factory_guest('full').words
            idle=max(i for i,w in enumerate(words) if w==0xeafffffe)
            words[idle]=0xee0f0f58
            (factory/'full.rom').write_bytes(struct.pack('<%dI'%len(words),*words)+struct.pack('<I',0xeafffffe)*(524288-len(words)))
            cold_model='sa110';cold_frames='3'
        cold = root / 'cold'; cold.mkdir()
        plan.write_text(f'version=2\nmodel={cold_model}\nfirmware={factory / "full.rom"}\n'
                        f'cmos=persist\ncmos_layout=ro370\ncontent_id={"e" * 40}\n'
                        'rtc_epoch=946684800\nhard_disc=on\n')
        preparation = root / 'preparation.csv'
        result = subprocess.run([str(runner), '--core', str(core), '--plan', str(plan),
                                 '--system', str(factory), '--save', str(cold), '--frames', cold_frames,
                                 '--hd-cost', str(report), '--preparation-timing', str(preparation)], capture_output=True, text=True, timeout=180)
        assert result.returncode == 0, result.stderr
        cold_rows = list(csv.DictReader(report.open()))
        assert all(r['open_ns']=='0' and r['attached']=='1' for r in cold_rows)
        pending = list(csv.DictReader(preparation.open()))
        assert len(pending)>1 and pending[-1]['state']=='10'
        assert {0,1,3,4,5,6,7,8,9,10}<={int(r['state']) for r in pending}
        assert all(int(r['whole_ns'])>=int(r['core_ns']) for r in pending)
        assert all(int(r['steps'])<=32 and int(r['namespace_attempts'])<=1 for r in pending)
        assert all(int(r['steps'])==1 for r in pending if int(r['namespace_attempts']))
        assert any(int(r['generation_quanta']) for r in pending)
        assert any(int(r['namespace_attempts']) for r in pending)
        for r in cold_rows:
            assert int(r['total_ns']) == sum(int(r[k]) for k in ('open_ns','read_ns','acknowledgement_ns','maintenance_ns'))
        assert any(cold.glob('arcdroid/hd-riscpc/' + 'e' * 40 + '/manifest.?'))
        # An unsuccessful opening still counts its work without an attachment;
        # unattached read/write/maintenance work remains invalid.
        rows = [dict(cold_rows[0])]
        rows[0].update(frame='0', attached='0', open_ns='1', total_ns='1',
                       read_ns='0', acknowledgement_ns='0', maintenance_ns='0',
                       read_commands='0', write_commands='0', namespace_operations='0')
        write_rows()
        assert subprocess.run([sys.executable, '-B', str(checker), str(report)], capture_output=True).returncode == 0
        rows[0].update(open_ns='0', maintenance_ns='1')
        write_rows()
        assert subprocess.run([sys.executable, '-B', str(checker), str(report)], capture_output=True).returncode == 2
        print('rpc-hd-cost-report: measured/unmeasured execution and store identical; substeps, namespace census, off and D-161 boundary/acknowledgement checker pass')


if __name__ == '__main__':
    main()
