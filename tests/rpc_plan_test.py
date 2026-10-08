#!/usr/bin/env python3
"""Plan admission, mechanism observations and canonical pacing replays. GPL-2.0-only."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
from rpc_plan_matrix_rom import matrix
from rpc_plan_goldens import matrix_golden
from rpc_video_goldens import expected as video_expected
from rpc_d2_goldens import expected as audio_expected, fnv
from rpc_video_paths import CASES as path_cases, expected as path_expected

runner,core,shipping,directory,mode=sys.argv[1:6]
directory=Path(directory).resolve()
def run(name,frames=2,pacing='flat',extra=(),library=core,success=True):
    command=[runner,'--core',library,'--plan',str(directory/(name+'.arcplan')),'--system',str(directory),'--frames',str(frames),'--pacing',pacing,*extra]
    result=subprocess.run(command,capture_output=True)
    assert b'\r' not in result.stdout,'canonical stdout must contain LF bytes on every platform'
    result.stdout=result.stdout.decode('utf-8');result.stderr=result.stderr.decode('utf-8')
    if success:assert result.returncode==0,result.stderr+result.stdout
    else:assert result.returncode!=0,'invalid plan accepted'
    return result.stdout

if mode=='admission':
    run('audio-0',library=shipping,extra=['--reject'])
    original=(directory/'audio-0.arcplan').read_text()
    with tempfile.TemporaryDirectory(prefix='arc-plan-admission-') as temp:
        # All named files remain external; the core never writes capture state.
        for bad in [original+'version=1\n',original+'unknown=1\n',original.replace('model=arm610','model=arm810'),
                    original+'rate_mips=0\n',original+'rate_mips=101\n',original+'rate_mips=-1\n',
                    original.replace('rtc_epoch=0','rtc_epoch=13569465600'),
                    original.replace('firmware=audio-0.rom','firmware=../audio-0.rom'),
                    original.replace('seed=authored.seed','seed=missing.seed'),
                    original.replace('cmos=seed','cmos=factory'),original+'input=missing.input\n',
                    original+'input=invalid.input\n','x'*16385,original+'\0']:
            p=Path(temp)/'invalid.arcplan';p.write_text(bad)
            command=[runner,'--core',core,'--plan',str(p),'--system',str(directory)]
            r=subprocess.run(command,capture_output=True,text=True);assert r.returncode!=0,'invalid plan admitted'
        # Absolute paths and explicit boundary rates are accepted.
        for rate in [1,100]:
            p=Path(temp)/'valid.ARcPLAN'
            p.write_text(original.replace('firmware=audio-0.rom',f'firmware={directory / "audio-0.rom"}').replace('seed=authored.seed',f'seed={directory / "authored.seed"}')+f'rate_mips={rate}\n')
            r=subprocess.run([runner,'--core',core,'--plan',str(p)],capture_output=True,text=True)
            assert r.returncode==0,r.stderr
        # Independently reject each non-contract ROM/seed size through VFS.
        for key,sizes in [('seed',[0,255,257]),('firmware',[0,2097151,6291456])]:
            for size in sizes:
                bad=Path(temp)/'wrong-size.bin';bad.write_bytes(bytes(size))
                p=Path(temp)/'invalid.arcplan'
                text=re.sub(rf'^{key}=.*$',lambda _:f'{key}={bad}',original,flags=re.M)
                p.write_text(text)
                r=subprocess.run([runner,'--core',core,'--plan',str(p),'--system',str(directory)],capture_output=True,text=True)
                assert r.returncode!=0,(key,size,'wrong-sized file admitted')
        # Strict script order, no unsupported events or numerical overflow.
        p=Path(temp)/'invalid.input'
        for text in ['1 key 97 1\n0 key 97 0\n','0 key 9999 1\n','0 key 97 2\n','0 mouse 99999 0 1\n','0 mouse 0 0 8\n','0 wat 0 0\n','0 key 97 1 extra\n']:
            p.write_text(text);plan=Path(temp)/'invalid.arcplan';plan.write_text(original+f'input={p}\n')
            r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(directory)],capture_output=True,text=True)
            assert r.returncode!=0,'invalid script admitted'
    for m in range(4):run(f'bad-mode-{m}',success=False)
    print('plan: shipping rejection, strict admission, absolute paths, rate bounds, scripts, guest containment passed')
elif mode.startswith('matrix-'):
    model=int(mode[-1]);_,expected=matrix(model)
    extra=['--ram-offset','262144','--ram-words',str(len(expected)*4)]
    # 4096 table clears and all 173 records include uncached instruction and
    # data service. Six frontend periods bound A7000 reset-clock setup.
    count=6 if model==3 else 2
    a=run(mode,frames=count,extra=extra);b=run(mode,frames=count,pacing='burst',extra=extra);assert a==b,'frontend pacing changed canonical trace'
    words=[int(w,16) for w in re.findall(r'^ram=(.*)$',a,re.M)[-1].split(',')]
    observed='\n'.join(f'{i:03d}: '+','.join(f'{v:08x}' for v in words[i*4:i*4+4]) for i in range(len(expected)))+'\n'
    golden=Path(__file__).with_name('fixtures').joinpath(f'rpc-plan-matrix-{model}.trace').read_text()
    assert golden==matrix_golden(model),'committed golden differs from declared independent expectations'
    if observed!=golden:
        import difflib
        raise AssertionError(''.join(difflib.unified_diff(golden.splitlines(True),observed.splitlines(True))))
    print(f'{mode}: {len(expected)} integrated guest records match golden; full CPU/device/AV traces identical across pacing')
elif mode.startswith('av-'):
    m=int(mode[-1]);summaries=[]
    for case in path_cases:
        a=run(f'video-paths-{m}-{case[0]}',frames=3)
        b=run(f'video-paths-{m}-{case[0]}',frames=3,pacing='burst');assert a==b
        observed=re.findall(r'^video=(\S+) hash=(\S+)',a,re.M)[-1]
        line=path_expected(m,case)
        assert observed==(line.split()[1],re.search(r'hash=(\S+)',line)[1]),(case[0],observed)
    # Every D1 case is admitted through a plan, with unchanged analytic oracle.
    for line in video_expected(m).splitlines():
        fields=line.split();name=fields[0]
        count=2 if m==3 else 1 # first complete field follows reset-clock setup
        a=run(f'video-{m}-{name}',frames=count);b=run(f'video-{m}-{name}',frames=count,pacing='burst');assert a==b
        observed=re.findall(r'^video=(\S+) hash=(\S+)',a,re.M)[-1]
        expected_hash=re.search(r'hash=(\S+)',line)[1]
        assert observed==(fields[1],expected_hash),(name,observed,fields[1],expected_hash)
        summaries.append(f'raster {name} {observed[0]} {observed[1]}')
    a=run(f'audio-{m}',frames=3);b=run(f'audio-{m}',frames=3,pacing='burst');assert a==b
    pcm,_,_=audio_expected(m,end=50000001)
    observed=re.findall(r'^pcm=(\S+) frames=(\d+) generated=(\d+) accepted=(\d+) discarded=(\d+) queued=(\d+)',a,re.M)[-1]
    assert observed==(f'{fnv(pcm):016x}','2400','2400','2400','0','0'),observed
    summaries.append(f'pcm {observed[0]} frames=2400 accepted=2400 discarded=0')
    count=6 if m==3 else 3
    a=run(f'mixed-av-{m}',frames=count);b=run(f'mixed-av-{m}',frames=count,pacing='burst');assert a==b
    first,prefix,n=re.findall(r'^first-raster=(\S+) pcm-prefix=(\S+) prefix-frames=(\d+)',a,re.M)[-1]
    palette_line=next(line for line in video_expected(m).splitlines() if line.startswith('palette '))
    pcm,_,_=audio_expected(m,end=10000000)
    assert (first,prefix,n)==(re.search(r'hash=(\S+)',palette_line)[1],f'{fnv(pcm):016x}','480'),(first,prefix,n)
    summaries.append(f'mixed first-raster={first} pcm480={prefix}')
    a=run(f'input-{m}',frames=7,extra=['--ram-offset','131072','--ram-words','140']);b=run(f'input-{m}',frames=7,pacing='burst',extra=['--ram-offset','131072','--ram-words','140']);assert a==b
    words=[int(w,16) for w in re.findall(r'^ram=(.*)$',a,re.M)[-1].split(',')]
    assert words[0]==3 and words[64:67]==[0x1c,0xf0,0x1c],words[:4]+words[64:70]
    # Discrete button event is retired before accumulated relative motion;
    # the guest's F4 ACK precedes both complete packets.
    if m==3:assert words[1]==10 and words[128:138]==[0xfa,0x09,0,0,0x09,12,7,0x08,0,0],words[128:138]
    else:assert words[2:4]==[12,7] and words[5]==0x70,words[:6]
    summaries.append('input keyboard=1c,f0,1c mouse='+('fa,09,00,00,09,0c,07,08,00,00' if m==3 else '12,7,70'))
    golden=Path(__file__).with_name('fixtures').joinpath(f'rpc-plan-av-{m}.trace').read_text()
    assert '\n'.join(summaries)+'\n'==golden
    print(f'{mode}: plan raster/actual libretro PCM/input goldens and two pacing replays passed')
else:raise AssertionError(mode)
