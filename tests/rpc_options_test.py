#!/usr/bin/env python3
"""R6 frontend selection, option meaning and storage contracts. GPL-2.0-only."""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from generate_fixtures import build_extended_jfd
from rpc_options_probe import ProbeBuilder
from rpc_ninja_commands import rebuild_commands, same_path
from rpc_save_snapshot import save_snapshot

build, root, exe, fixtures, legacy = map(lambda p: Path(p).resolve(),sys.argv[1:6])
mode = sys.argv[6]
closed_core = Path(sys.argv[7]).resolve()
MODEL = 'arcdroid_rpc_model'; OS = 'arcdroid_rpc_os'
ARCH = ('arcdroid_machine','arcdroid_cpu','arcdroid_os')
# Every metadata key belongs to one tested meaning row. R3d-5 must change the
# three marked M1 rows and production texts/metadata together (R6-10).
MEANING_MATRIX = {
    'arch-select/restart;rpc-reload': ('machine','cpu','os'),
    'arch-memory;rpc-ignored': ('memory',),
    'rpc-select/reload;arch-suppressed': ('rpc_model','rpc_os'),
    'arch-hardware;rpc-keyboard': ('joystick_if',),
    'arch-only;rpc-ignored/ide-always': ('hard_disc',),
    'arch-only;rpc-native': ('jfd_boot','adffs_package'),
    'M1-ignored:R3d-5-launch': ('launch',),
    'M1-ignored:R3d-5-chooser': ('launch_chooser',),
    'M1-ignored:R3d-5-ramdisc': ('launch_ramdisc',),
    'media-load/restart;runtime-frozen': ('disk_writes','jfd_write_protect','short_tail','jfd_omitted'),
    'live-qualified-disc-policy': ('disc_requests',),
    'live-pointer': ('pointer_mode','pointer_stick','pointer_speed','pointer_deadzone'),
    'live-keys': tuple(f'pad{port}_{key}' for port in (1,2) for key in ('up','down','left','right','a','b','x','y','start','select')),
    'live-left-stick': ('pad1_left_stick','pad2_left_stick'),
    'live-osk': ('osk_toggle',),
}


class Cases:
    def __init__(self,temp,library):
        self.temp=temp; self.library=library; self.index=0

    def fixture(self,name,cpu=0,os=0):
        work=self.temp/(str(self.index)+'-'+name);self.index+=1;work.mkdir()
        system=work/'system';shutil.copytree(fixtures/'system',system)
        shutil.copyfile(legacy/'arcdroid-test.rom',system/'arcdroid/firmware/control.rom')
        save=work/'save';save.mkdir()
        image=bytearray(build_extended_jfd(bytes(256),b'ROUTE-PRIVATE-SENTINEL'))
        struct.pack_into('<I',image,304,(cpu<<8)|(os<<16))
        disc=work/'ROUTE-PRIVATE-SENTINEL.jfd';disc.write_bytes(image)
        raw=work/'ROUTE-PRIVATE-SENTINEL.adf';shutil.copyfile(legacy/'arcdroid-test.adf',raw)
        return work,system,save,disc,raw

    def run(self,work,system,save,commands,expected=True):
        script=work/'commands';script.write_text('\n'.join(commands)+'\n',encoding='utf-8')
        run=subprocess.run([exe,self.library,system,save,'unused','options',script],capture_output=True,text=True,timeout=300)
        if expected and run.returncode:
            raise RuntimeError(run.stdout[-16000:]+'\n'+run.stderr)
        if expected and 'options-complete:' not in run.stdout:
            raise RuntimeError('missing complete script result')
        return run


def options(values):
    return [f'set {key}={value}' for key,value in values.items()]


def pair(model,os):
    return [f'probe 1 {model}',f'probe 2 {os}',f'probe 3 {model-1}',f'probe 4 {1 if os==1 else 2 if os==2 else 3}']


def selection(c):
    # Both axes independently, all literal tokens, Auto ranking and both axes.
    for values,expected in [({MODEL:'arm610'},(1,1)),({MODEL:'arm710'},(2,4)),
            ({MODEL:'sa110'},(3,4)),({MODEL:'arm7500'},(4,4)),
            ({OS:'350'},(1,1)),({OS:'360'},(2,2)),({OS:'370'},(3,3)),({OS:'371'},(2,4)),
            ({MODEL:'sa110',OS:'370'},(3,3))]:
        w,s,v,d,r=c.fixture('axis')
        c.run(w,s,v,options(values)+[f'load {d}','probe 0 2']+pair(*expected)+['check arc 0','check private 0','unload'])
    for kind in ('adf','adl','legacy'):
        w,s,v,d,r=c.fixture(kind)
        if kind=='adl':r=w/'raw.adl';shutil.copyfile(legacy/'arcdroid-chooser-l.adl',r)
        if kind=='legacy':
            r=w/'legacy.jfd';r.write_bytes((legacy/'arcdroid-test.jfd').read_bytes())
        c.run(w,s,v,options({MODEL:'arm610'})+[f'load {r}']+pair(1,1)+['check private 0','unload'])
    for container in ('zip','playlist','mixed'):
        w,s,v,d,r=c.fixture('manual-'+container)
        selected=w/('selected.zip' if container=='zip' else 'selected.m3u')
        if container=='zip':
            with zipfile.ZipFile(selected,'w') as archive:archive.write(r,'ROUTE-PRIVATE-SENTINEL.adf')
        else:selected.write_text(str(r)+'\n'+(str(d)+'\n' if container=='mixed' else ''))
        c.run(w,s,v,options({MODEL:'arm610'})+[f'load {selected}']+pair(1,1)+['check private 0','unload'])
    # Each Archimedes axis suppresses even an unqualified manual request.
    for key,value in zip(ARCH,('A3000','ARM2','RISC OS 3.11')):
        w,s,v,d,r=c.fixture('precedence',16,8)
        if key==ARCH[2]:shutil.copyfile(legacy/'arcdroid-unsupported-launch-test.rom',s/'arcdroid/firmware/control.rom');value='RISC OS 2.00'
        run=c.run(w,s,v,options({key:value,MODEL:'arm610',OS:'371'})+[f'load {d}','probe 0 1','check conflict 1','check seen6 1','run 1','reset','check conflict 1','check seen6 1','unload'])
        assert 'N-9:' in run.stdout and 'N-2:' not in run.stdout
    # Independent hint axes: a model choice cannot erase an Archimedes OS hint.
    for cpu,os,values,reason in [(4,2,{MODEL:'arm610'},'N-3'),
            (1,8,{OS:'371'},'N-3'),
            (16,8,{MODEL:'arm610',OS:'371'},'N-2'),
            (16,8,{MODEL:'sa110',OS:'350'},'N-2')]:
        w,s,v,d,r=c.fixture('refusal',cpu,os)
        run=c.run(w,s,v,options(values)+[f'load-fail {d}','check arc 0','check private 0'])
        assert reason+':' in run.stdout
    for kind in ('missing','unrecognised'):
        w,s,v,d,r=c.fixture(kind)
        for rom in (s/'arcdroid/firmware').glob('ro*.rom'):rom.unlink()
        if kind!='missing':(s/'arcdroid/firmware/fake.rom').write_bytes(bytes(2097152))
        run=c.run(w,s,v,options({MODEL:'arm610'})+[f'load-fail {r}','check arc 0'])
        assert 'N-1:' in run.stdout
        assert not (v/'arcdroid/hd-riscpc').exists()
    w,s,v,d,r=c.fixture('manual-locator-change')
    run=c.run(w,s,v,options({MODEL:'arm710',OS:'371'})+['locator-fault',f'load-fail {r}','check arc 0'])
    assert 'N-B firmware:' in run.stdout and not (v/'arcdroid/hd-riscpc').exists()
    # No new inventory I/O for all-Auto, unflagged raw or extended JFD loads.
    for media in ('raw','jfd'):
        w,s,v,d,r=c.fixture('no-scan')
        c.run(w,s,v,[f'load {r if media=="raw" else d}','check scan 0','probe 0 1','check conflict 0','unload'])
    print('options selection: per-axis tokens, precedence, hint boundaries, refusals, raw/legacy media and no-scan pass',flush=True)


def privacy(c):
    for kind in ('raw','legacy','zip','playlist'):
        for override in (False,True):
            w,s,v,d,r=c.fixture('privacy-'+kind)
            selected=w/('ROUTE-PRIVATE-SENTINEL.'+{'raw':'adf','legacy':'jfd','zip':'zip','playlist':'m3u'}[kind])
            if kind=='playlist':selected.write_text(str(d)+'\n'+str(w/'ROUTE-PRIVATE-SENTINEL-missing.adf')+'\n')
            else:selected.write_bytes(b'ROUTE-PRIVATE-SENTINEL malformed authored media')
            values={MODEL:'arm610'}
            if override:values[ARCH[0]]='A3000'
            run=c.run(w,s,v,options(values)+[f'load-fail {selected}',f'check conflict {int(override)}']+([] if override else ['check private 0']))
            if override and kind=='playlist':assert any('ROUTE-PRIVATE-SENTINEL' in line for line in run.stdout.splitlines() if line.startswith('options-log:'))
            assert 'N-22:' in run.stdout if override else 'N-22:' not in run.stdout
    print('options privacy: malformed raw/JFD/ZIP/playlist with request and suppressed request pass',flush=True)


def compatibility(c):
    for values in ({MODEL:'stale'}, {OS:'stale'}, {MODEL:'stale',OS:'stale'}):
        w,s,v,d,r=c.fixture('unknown')
        c.run(w,s,v,options(values)+[f'load {r}','check unknown 1','check seen5 1','probe 0 1','run 1','reset','check unknown 1','check seen5 1','unload',f'load {r}','check unknown 2','unload'])
    for values,expected in (({MODEL:'arm610',OS:'stale'},(1,1)),({MODEL:'stale',OS:'360'},(2,2))):
        w,s,v,d,r=c.fixture('one-stale-axis')
        c.run(w,s,v,options(values)+[f'load {r}']+pair(*expected)+['check unknown 1','unload'])
    w,s,v,d,r=c.fixture('legacy-advisories')
    c.run(w,s,v,['legacy']+options({ARCH[0]:'A3000',MODEL:'stale',OS:'350'})+
          [f'load {r}','check unknown 1','check seen5 1','check conflict 1','check seen6 1',f'set {MODEL}=arm710','reset','check reload0 1','check seen0 1','unload'])
    for values in ({},{MODEL:'',OS:''},{MODEL:'auto',OS:'auto'}):
        w,s,v,d,r=c.fixture('defaults')
        c.run(w,s,v,options(values)+[f'load {r}','check unknown 0','check scan 0','unload'])
    # Existing keys retain refusal, including valid-but-inert Memory on RPC.
    for key in ('arcdroid_memory','arcdroid_machine','arcdroid_disk_writes','arcdroid_pad1_up'):
        w,s,v,d,r=c.fixture('existing-invalid')
        c.run(w,s,v,options({MODEL:'arm610',key:'stale'})+[f'load-fail {r}','check unknown 0'])
    # Source-verified RetroArch boundary model, then public core entry points.
    # Save only declared values over the retained loaded configuration. A newly
    # created override starts empty. Per-game replaces global; it never overlays.
    global_file={MODEL:'arm610',OS:'350','arcdroid_pointer_speed':'900'}
    def save_file(existing,declared,delivered):
        result=dict(existing)
        result.update({key:delivered.get(key,'auto') for key in declared})
        return result
    old_declared={'arcdroid_pointer_speed'}
    closed=save_file(global_file,old_declared,global_file)
    assert closed[MODEL]=='arm610' and closed[OS]=='350'
    created=save_file({},old_declared,closed)
    assert MODEL not in created and OS not in created
    for stored,expected in ((closed,(1,1)),(created,None),({'arcdroid_pointer_speed':'450'},None)):
        w,s,v,d,r=c.fixture('option-file')
        opt=w/'authored.opt';opt.write_text(''.join(f'{k} = "{value}"\n' for k,value in stored.items()))
        reread=dict(line.split(' = ',1) for line in opt.read_text().splitlines())
        reread={k:value.strip('"') for k,value in reread.items()}
        c.run(w,s,v,options(reread)+[f'load {r}']+(pair(*expected) if expected else ['probe 0 1','check scan 0'])+['unload'])
        # The actual closed core sees the retained keys, accepts the option
        # schema and ignores those keys. Open restores the exact choice above.
        old=c.library;c.library=closed_core
        c.run(w,s,v,options(reread)+[f'load {r}','check unknown 0','check conflict 0','run 1','unload'])
        c.library=old
        # Save while those keys are undeclared, then actually reopen in the
        # open core. The frontend model retains unknown entries in this file.
        saved_closed=save_file(reread,old_declared,reread)
        opt.write_text(''.join(f'{key} = "{value}"\n' for key,value in saved_closed.items()),encoding='utf-8')
        restored={key:value.strip('"') for key,value in
                  (line.split(' = ',1) for line in opt.read_text(encoding='utf-8').splitlines())}
        c.run(w,s,v,options(restored)+[f'load {r}']+(pair(*expected) if expected else ['probe 0 1','check scan 0'])+['unload'])
    print('options compatibility: delivered stale/default/absent values, existing refusals and option-file retention/replacement model pass',flush=True)


def meaning(c):
    metadata=subprocess.run([exe,c.library,c.temp,c.temp,'unused','metadata-fault','unused'],capture_output=True,text=True,check=True)
    keys=[line.split('|')[1] for line in metadata.stdout.splitlines() if line.startswith('option|')]
    matrix=[f'arcdroid_{key}' for group in MEANING_MATRIX.values() for key in group]
    assert len(matrix)==len(set(matrix))==45 and set(matrix)==set(keys)
    # Selection changes while pending, in isolated generation, RUNNING and FAILED.
    traces=[]
    for target in (0,5,10,11):
        w,s,v,d,r=c.fixture('lifecycle')
        values={MODEL:'arm610',OS:'350'}
        commands=(['legacy'] if target==0 else [])+options(values)+[f'load {r}']+pair(1,1)
        if target==11:
            # Remove after the read-only preflight; recovery must stop safely.
            # A script-local path move is intentionally supplied by a command.
            commands += [f'remove {s}/arcdroid/riscpc/boot/HardDisc4.zip']
        commands += [f'until {target}']
        commands += options({MODEL:'arm710',OS:'371',ARCH[0]:'A3010',ARCH[1]:'ARM250',ARCH[2]:'RISC OS 3.11'})
        # No retro_run between the selection changes and Restart.
        commands += ['reset']+pair(1,1)+[f'check reload{i} 1' for i in range(5)]
        if target==0:commands += [f'check seen{i} 1' for i in range(5)]
        commands += ['run 1','reset']+pair(1,1)+[f'check reload{i} 1' for i in range(5)]+['check conflict 0','unload','clear','counts',f'load {r}','probe 0 1','check scan 0','trace 8','unload']
        result=c.run(w,s,v,commands)
        traces.append(next(line for line in result.stdout.splitlines() if line.startswith('options-trace:')))
    w,s,v,d,r=c.fixture('fresh-arculator')
    result=c.run(w,s,v,[f'load {r}','trace 8','unload'])
    fresh=next(line for line in result.stdout.splitlines() if line.startswith('options-trace:'))
    assert all(trace==fresh for trace in traces),'RPC cleanup changed Arculator output'
    # Frozen media resamples on Restart; input settings update at runtime.
    w,s,v,d,r=c.fixture('media-input')
    commands=options({MODEL:'arm610',OS:'350'})+[f'load {r}','until 10']
    commands+=options({'arcdroid_disk_writes':'off','arcdroid_jfd_write_protect':'off','arcdroid_short_tail':'zero-filled','arcdroid_jfd_omitted':'strict'})
    commands+=['run 1','probe 8 0','probe 9 0','probe 10 0','probe 11 0','reset','probe 8 2','probe 9 1','probe 10 1','probe 11 1']
    commands+=options({'arcdroid_pointer_mode':'disabled','arcdroid_pointer_stick':'left','arcdroid_pointer_speed':'900','arcdroid_pointer_deadzone':'25','arcdroid_pad1_left_stick':'off','arcdroid_pad2_left_stick':'off','arcdroid_osk_toggle':'R3','arcdroid_pad1_up':'Unbound'})
    commands+=['run 1','probe 19 0','probe 20 1','probe 21 900','probe 22 25','probe 23 1','probe 24 0','probe 25 0','probe 26 0']
    commands+=options({f'arcdroid_{key}':'Unbound' for key in MEANING_MATRIX['live-keys']})
    commands+=['run 1']+[f'probe {index} 0' for index in range(32,52)]+['reset','run 1']+[f'probe {index} 0' for index in range(32,52)]
    # M1 rows: R3d-5 changes Launch/Chooser/RAM disc together with M1 texts.
    commands+=options({'arcdroid_launch':'app8','arcdroid_launch_chooser':'ask','arcdroid_launch_ramdisc':'Disabled','arcdroid_hard_disc':'off','arcdroid_adffs_package':'untested','arcdroid_jfd_boot':'adffs','arcdroid_joystick_if':'A3010','arcdroid_memory':'4 MB','arcdroid_disc_requests':'off'})
    commands+=['run 1','probe 27 2','reset','until 10','probe 28 0','probe 29 0','probe 30 1','probe 18 0']+pair(1,1)+['unload']
    run=c.run(w,s,v,commands)
    for label in ('Disk Writes','Write-protect Tab','Short image tail','JFD omitted sectors'):
        assert run.stdout.count('N-11: '+label+' takes effect after Restart.')==1
    # Arculator Restart continues to resample its selectors. RPC choices never
    # turn this Restart into a backend switch; both RPC reload notices survive.
    w,s,v,d,r=c.fixture('arch-restart')
    c.run(w,s,v,[f'load {r}']+options({ARCH[0]:'A3010',ARCH[1]:'ARM250',MODEL:'arm610',OS:'350'})+
          ['reset','probe 0 1','probe 5 1','probe 6 1','check reload0 1','check reload1 1','check reload2 0','check reload3 0','check conflict 0','unload'])
    print('options meaning: pending/generating/running/failed, Restart-before-run, selection freeze, media freeze, live inputs and M1 rows pass',flush=True)


def persistence(c):
    w,s,v,d,r=c.fixture('pairs')
    history=[]
    # A→B→A, OS-only change, same resolved pair after changing to Auto.
    for index,(model,os,m,o) in enumerate((('arm710','360',2,2),('arm7500','360',4,2),('arm710','360',2,2),('arm710','371',2,4),('auto','371',2,4))):
        commands=options({MODEL:model,OS:os})+[f'load {r}']+pair(m,o)+['until 10',f'check drift {int(index in (1,2,3))}','unload']
        result=c.run(w,s,v,commands)
        if index in (1,2,3):
            assert "pair-changed: The RiscPC model or operating system changed. This combination has separate RiscPC settings; the content's hard disc is kept." in result.stdout
        snapshot=save_snapshot(v)
        records={name:data for name,data in snapshot.items() if name.startswith('arcdroid/cmos-riscpc/') and '/defaults/' not in name}
        if index==0:history.append(records)
        if index==2:
            assert all(records[name]==data for name,data in history[0].items()),'A CMOS record changed across B'
        hd=[p for p in (v/'arcdroid/hd-riscpc').iterdir() if p.is_dir()];assert len(hd)==1
        sidecars=list((v/'arcdroid/rpc-pairs').glob('*.pair.[01]'))
        wire=max((p.read_bytes() for p in sidecars),key=lambda b:struct.unpack_from('<Q',b,8)[0])
        assert wire[:8]==b'ARCPAIR\1' and wire[16:28]==bytes([m-1,o])+bytes(10)
        assert struct.unpack_from('<I',wire,28)[0]==zlib.crc32(wire[:28])
        for name,data in records.items():
            assert data[:16]==b'ARCRPCM1'+struct.pack('<II',1,512)
            tag=struct.unpack_from('<I',data,16)[0]
            assert tag in (2,4) and f'-m{tag}-' in name
            assert data[20:24] in (b'\2\0\1\0',b'\3\0\1\0')
    before=save_snapshot(v)
    c.run(w,s,v,options({MODEL:'arm610',OS:'371'})+[f'load-fail {r}'])
    assert save_snapshot(v)==before,'failed admission changed persistent bytes'
    # Missing and corrupt advisory records never invalidate the HD/CMOS.
    for corrupt in (False,True):
        for p in (v/'arcdroid/rpc-pairs').glob('*'):
            if corrupt:p.write_bytes(b'authored corrupt sidecar')
            else:p.unlink()
        c.run(w,s,v,options({MODEL:'arm710',OS:'371'})+[f'load {r}','until 10','check drift 0','unload'])
    print('options persistence: A/B/A, OS-only and unchanged pair, failed admission, corrupt/missing sidecar and wire/path goldens pass',flush=True)


def mutants(c,builder):
    front='libretro/arcdroid_libretro.c'
    specifications=[
        ('archimedes-precedence','if(rpc_auto && (flagged || rpc_manual)){','if(flagged || rpc_manual){',
         {ARCH[0]:'A3000',MODEL:'arm610',OS:'371'},'arc'),
        ('manual-unflagged-route','if(rpc_auto && (flagged || rpc_manual)){','if(rpc_auto && flagged){',
         {MODEL:'arm610'},'pair'),
        ('model-axis-forwarding','manual_model=arc_core_options.rpc_model;','manual_model=ARC_RPC_SELECT_AUTO;',
         {MODEL:'arm7500',OS:'360'},'arm7500'),
        ('os-axis-forwarding','manual_os=arc_core_options.rpc_os;','manual_os=ARC_RPC_OS_AUTO;',
         {MODEL:'arm710',OS:'360'},'os'),
        ('unknown-fallback','if(i<5)parsed[axis]=i;','parsed[axis]=i<5?i:4; if(i<5)parsed[axis]=i;',
         {MODEL:'stale'},'unknown'),
        ('unknown-notice-dedup','else if(!arc_rpc_selection_options.invalid_noticed){','else if(true){',
         {MODEL:'stale'},'unknown'),
        ('pinned-model-token','"auto","arm610","arm710","sa110","arm7500"','"auto","arm710","arm610","sa110","arm7500"',
         {MODEL:'arm610',OS:'350'},'pair'),
        ('pinned-os-token','"auto","350","360","370","371"','"auto","350","360","371","370"',
         {MODEL:'sa110',OS:'370'},'os370'),
        ('stored-model-offset','request.key.model=(unsigned)rpc_selection.model-1;','request.key.model=(unsigned)rpc_selection.model;',
         {MODEL:'arm610',OS:'350'},'pair'),
        ('stored-os-layout','rpc_selection.os==ARC_RPC_OS_360?2:3','rpc_selection.os==ARC_RPC_OS_360?2:2',
         {MODEL:'sa110',OS:'370'},'os370'),
        ('privacy-before-staging','if(rpc_manual)arc_rpc_staging_flag();','if(false)arc_rpc_staging_flag();',
         {MODEL:'arm610'},'privacy'),
        ('reload-baseline','if(!arc_rpc_selection_options.sampled){','if(true){',
         {MODEL:'arm610',OS:'350'},'reload'),
    ]
    for name,before,after,values,scenario in specifications:
        w,s,v,d,r=c.fixture('mutant-'+name)
        if scenario=='privacy':
            r=w/'ROUTE-PRIVATE-SENTINEL.m3u';r.write_text(str(d)+'\n'+str(w/'ROUTE-PRIVATE-SENTINEL-missing.adf')+'\n')
            body=[f'load-fail {r}','check private 0']
        elif scenario=='arc':body=[f'load {r}','probe 0 1','check conflict 1','unload']
        elif scenario=='unknown':body=[f'load {r}','probe 0 1','check unknown 1','run 1','reset','check unknown 1','unload']
        elif scenario=='reload':body=[f'load {r}',f'set {MODEL}=arm710','reset','check reload0 1','unload']
        else:
            expected={'pair':(1,1),'os':(2,2),'os370':(3,3),'arm7500':(4,2)}[scenario]
            body=[f'load {r}']+pair(*expected)+['unload']
        # A passing baseline is mandatory for every mutant's exact script.
        commands=options(values)+body
        c.run(w,s,v,commands)
        shutil.rmtree(v);v.mkdir()
        mutation=(before,after)
        if name=='privacy-before-staging':
            # Early kind and the forced-safe arena independently protect this
            # failure. Removing just one is equivalent here; recreate the old
            # observed-flags-only boundary by removing both together.
            mutation=[mutation,('if(rpc_manual)arc_kind=ARC_SESSION_KIND_RISCPC;','if(false)arc_kind=ARC_SESSION_KIND_RISCPC;')]
        builder.compile(front,mutation);c.library=builder.library()
        run=c.run(w,s,v,commands,expected=False)
        if run.returncode==0:raise RuntimeError('surviving R6 mutant: '+name)
        print('R6 mutant killed: '+name,flush=True)
        builder.compile(front);c.library=builder.library()
    # Full golden bytes and paths must also detect wire-only changes which
    # preserve the in-memory selection. Use actual configured unit binaries.
    for relative,target,changes in (
        ('libretro/rpc_route.c','rpc_route_policy_test',[
            ('sidecar-model','record[16]=(unsigned char)model;','record[16]=(unsigned char)(model+1);'),
            ('sidecar-os','record[17]=(unsigned char)os;','record[17]=(unsigned char)(os-1);'),
            ('sidecar-version',"unsigned char record[32]={'A','R','C','P','A','I','R',1}","unsigned char record[32]={'A','R','C','P','A','I','R',2}")]),
        ('libretro/rpc_cmos_store.c','rpc_cmos_store_test',[
            ('cmos-model','put(p+16,k->model+1,4);','put(p+16,k->model,4);'),
            ('cmos-layout','put(p+20,0x10000u|k->layout,4);','put(p+20,0x10000u,4);'),
            ('cmos-model-path','s->directory, content, k->model + 1, firmware','s->directory, content, k->model, firmware')])):
        original=root/relative;text=original.read_text(encoding='utf-8')
        for name,before,after in [('baseline',None,None),*changes]:
            source=c.temp/original.name
            if before is not None:assert text.count(before)==1
            source.write_text(text if before is None else text.replace(before,after),encoding='utf-8')
            cc,link=rebuild_commands(builder.commands,original,target,build,os.name=='nt')
            old_obj=cc[cc.index('-o')+1];obj=c.temp/(target+'.o')
            cc[cc.index('-o')+1]=str(obj);cc[cc.index('-c')+1]=str(source)
            if '-MF' in cc:cc[cc.index('-MF')+1]=str(c.temp/(target+'.d'))
            compiled=subprocess.run(cc,cwd=build,capture_output=True,text=True,timeout=120)
            if compiled.returncode:raise RuntimeError(compiled.stderr)
            binary=c.temp/(target+('.exe' if os.name=='nt' else ''))
            link=[str(obj) if arg.lower().endswith(('.o','.obj')) and same_path(arg,old_obj,build,os.name=='nt') else arg for arg in link]
            link[link.index('-o')+1]=str(binary)
            link=['-Wl,--out-implib,'+str(c.temp/(target+'.dll.a')) if arg.startswith('-Wl,--out-implib,') else arg for arg in link]
            linked=subprocess.run(link,cwd=build,capture_output=True,text=True,timeout=120)
            if linked.returncode:raise RuntimeError(linked.stderr)
            args=[str(binary)]
            if target=='rpc_route_policy_test':
                work=c.temp/(target+'-'+name);work.mkdir();args.append(str(work))
            run=subprocess.run(args,capture_output=True,text=True,timeout=30)
            if name=='baseline':assert run.returncode==0,run.stderr
            elif run.returncode==0:raise RuntimeError('surviving wire mutant: '+name)
            else:print('R6 mutant killed: '+name,flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='rpc-options-') as temporary:
        temp=Path(temporary);builder=ProbeBuilder(build,root,temp)
        builder.compile('libretro/rpc_preparation.c');builder.compile('libretro/arcdroid_libretro.c')
        c=Cases(temp,builder.library())
        if mode=='mutants':mutants(c,builder)
        else:{'selection':selection,'privacy':privacy,'compatibility':compatibility,'meaning':meaning,'persistence':persistence}[mode](c)


if __name__=='__main__':main()
