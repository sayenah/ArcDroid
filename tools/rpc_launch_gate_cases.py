#!/usr/bin/env python3
"""Private R3c-3a qualification legs; GPL-2.0-only.

Requires caller-supplied private firmware, boot packages and base save tree.
Never reads a title library. Output contains private evidence: keep it excluded
from source control. --prepare changes Floppies only in the new private copy;
acceptance legs compare its resulting CMOS payloads without printing them.
"""
from pathlib import Path
import argparse,json,re,shutil,struct,subprocess,time
PAIRS={'350-arm610':('arm610','ro350'), '360-arm710':('arm710','ro360'),
       '360-arm7500':('arm7500','ro360'), '370-sa110':('sa110','ro370'),
       '371-arm710':('arm710','ro370'), '371-arm7500':('arm7500','ro370'),
       '371-sa110':('sa110','ro370')}
CASES=('generated','empty','unrelated','damaged-filesystem','truncated-media',
       'truncated-helper','bad-length','unknown-version','missing-entry','valid',
       'unknown-action','replacement')
p=argparse.ArgumentParser(description=__doc__)
for key in ('pair','base','system','firmware','out','runner','core','builder'):p.add_argument('--'+key,required=True)
p.add_argument('--case',choices=CASES,default='generated');p.add_argument('--prepare',action='store_true')
p.add_argument('--revision1',action='store_true');p.add_argument('--trace',action='store_true')
p.add_argument('--reset',action='store_true');p.add_argument('--cells',type=int)
p.add_argument('--request',choices=('arcnone','arcdroid'),default='arcnone')
p.add_argument('--floppies',type=int,choices=(1,2),default=2)
p.add_argument('--field',type=int,choices=range(0,64,4));p.add_argument('--value',type=lambda value:int(value,0))
p.add_argument('--start',type=int);p.add_argument('--frames',type=int);p.add_argument('--content-id');p.add_argument('--timeout',type=int,default=900)
a=p.parse_args();model,layout=PAIRS[a.pair]
base=Path(a.base).resolve();out=Path(a.out).resolve();system=Path(a.system).resolve()
if out==base or base in out.parents or out.exists():p.error('--out must be a new directory outside --base')
if a.prepare and a.revision1:p.error('preparation and revision-1 admission are separate legs')
if (a.field is None)!=(a.value is None):p.error('--field and --value must be supplied together')
if a.field is not None and (a.prepare or a.revision1 or a.case not in ('valid','unknown-action')):p.error('descriptor mutation requires an explicit valid helper leg')
ids=[d.name for d in (base/'arcdroid/hd-riscpc').glob('*') if d.is_dir() and re.fullmatch('[0-9a-f]{40}',d.name)]
identity=a.content_id or (ids[0] if len(ids)==1 else None)
if not identity or not re.fullmatch('[0-9a-f]{40}',identity):p.error('provide --content-id for a base without exactly one HD identity')
shutil.copytree(base,out)
if a.prepare:shutil.rmtree(out/'arcdroid/hd-riscpc')
if not a.revision1:
 subprocess.run([a.builder,'--write',str(out/'helper.adl')],check=True)
 data=bytearray((out/'helper.adl').read_bytes());h=1792
 mutations={'truncated-helper':('size',32),'bad-length':(16,148),'unknown-version':(8,2),
            'missing-entry':(28,144),'unknown-action':(140,1)}
 if a.case in mutations:
  at,value=mutations[a.case];struct.pack_into('<I',data,512+5+18 if at=='size' else h+at,value)
 elif a.case=='unrelated':data[517:527]=b'Unrelated\r';data[517]|=128
 elif a.case=='damaged-filesystem':data[513]^=1
 elif a.case=='replacement':struct.pack_into('<6I',data,h+64,0xe3a00002,0xe582000c,0xe10f3000,0xe3130010,0x01b0f00e,0x11a0f00e)
 if a.field is not None:struct.pack_into('<I',data,h+a.field,a.value)
 (out/'helper.adl').write_bytes(data)
start=a.start or (3500 if model=='arm610' else 1800)
# Chronological keyboard input: show diagnostics; then an unrelated request,
# wait for its normal Wimp handling, cancel twice (media wait then task error).
f=start;events=[]
def event(key,down):events.append(f'{f} key {key} {int(down)}')
def key(value):
 global f
 event(value,True);f+=3;event(value,False);f+=3
def text(value):
 global f
 for c in value:
  shifted=c in '$*:';value=ord({'$':'4','*':'8',':':';'}.get(c,c))
  if shifted:event(304,True);f+=1
  key(value)
  if shifted:event(304,False);f+=1
key(293);f+=15
text(f'configure floppies {a.floppies}' if a.prepare else 'show arcgate$*');key(13)
if not a.prepare:
 f=start+180;text(f'wimptask cat adfs::{a.request}.$');key(13)
 f=start+1100;key(27);f=start+1400;key(27)
(out/'input').write_text('\n'.join(events)+'\n')
plan=f'version=2\nmodel={model}\nfirmware={Path(a.firmware).resolve()}\ncmos=persist\ncmos_layout={layout}\ncontent_id={identity}\nrtc_epoch=946684800\nhd_populate=on\n'
if a.prepare or a.revision1 or a.case=='empty':plan+='drive1=\n'
elif a.case!='generated':plan+='drive1='+str(out/'helper.adl')+'\n'
if a.cells is not None:plan+=f'observer_cells={a.cells}\n'
if a.trace:plan+='observation_trace=gate.jsonl\nobservation_classes=swi\nobservation_swis=0x33,0x20033\n'
else:plan+='input='+str(out/'input')+'\n'
(out/'case.arcplan').write_text(plan)
def cmos():
 return sorted(file.read_bytes()[104:344] for file in (out/'arcdroid/cmos-riscpc').glob('*') if file.is_file() and file.stat().st_size==512)
before=cmos();frames=a.frames or (start+1650 if not a.prepare else start+500)
if a.reset:frames+=start
command=[a.runner,'--core',a.core,'--plan',str(out/'case.arcplan'),'--system',str(system),'--save',str(out),'--frames',str(frames),'--disk-writes','off','--vfs-flush','retroarch','--preparation-timing',str(out/'preparation.csv'),'--timing',str(out/'frames.csv')]
if a.reset:command+=['--reset-at',str(start-50)]
if not a.trace:
 for frame,name in ((start+140,'gate'),(start+1000,'request'),(frames-1,'return')):
  if frame<frames:command+=['--dump-frame',str(frame),str(out/(name+'.ppm'))]
truncated=False
with (out/'runner.out').open('w') as stdout,(out/'runner.log').open('w') as stderr:
 if a.case=='truncated-media' and not a.prepare:
  child=subprocess.Popen(command,stdout=stdout,stderr=stderr)
  for n in range(20000):
   if 'Input resolution:' in (out/'runner.log').read_text():
    with (out/'helper.adl').open('r+b') as medium:medium.truncate(1024)
    truncated=True
    break
   if child.poll() is not None:break
   time.sleep(.01)
  code=child.wait(timeout=a.timeout)
 else:code=subprocess.run(command,stdout=stdout,stderr=stderr,timeout=a.timeout).returncode
result={'pair':a.pair,'case':a.case,'preparation':a.prepare,'returncode':code,'cmos_records':len(before),'cmos_unchanged':before==cmos()}
if not a.prepare and not result['cmos_unchanged']:code=1
if a.case=='truncated-media' and not a.prepare and not truncated:code=1
if a.trace:
 rows=[json.loads(line) for line in (out/'gate.jsonl').read_text().splitlines()]
 markers=[r for r in rows if 0xa3c300<=r.get('registers',[0])[0]<=0xa3c317]
 entered=[r['ns'] for r in markers if r['registers'][0]==0xa3c301];returned=[r['ns'] for r in markers if r['registers'][0]==0xa3c302]
 result.update(entries=len(entered),returns=len(returned),elapsed_ns=[end-begin for begin,end in zip(entered,returned)],helper_status=[r['registers'][0]-0xa3c310 for r in markers if 0xa3c310<=r['registers'][0]<=0xa3c317])
 if not entered or len(entered)!=len(returned) or any(end-begin>120000000000 for begin,end in zip(entered,returned)):code=1
 expected=[] if a.field is not None else ([2] if a.case in ('generated','valid','replacement') else [3] if a.case=='unknown-action' else [])
 if result['helper_status']!=expected*(2 if a.reset else 1):code=1
(out/'behaviour.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
raise SystemExit(code)
