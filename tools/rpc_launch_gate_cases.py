#!/usr/bin/env python3
"""Private R3c-3a qualification legs; GPL-2.0-only.

Requires caller-supplied private firmware, boot packages and base save tree.
Never reads a title library. Output contains private evidence: keep it excluded
from source control. --prepare changes Floppies only in the new private copy;
acceptance legs compare its resulting CMOS payloads without printing them.
"""
from pathlib import Path
import argparse,hashlib,json,re,shutil,struct,subprocess,time,sys,zlib
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tests"))
from rpc_controller_stub import controller,replace_carrier
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
p.add_argument('--controller',action='store_true');p.add_argument('--release-fail',action='store_true')
p.add_argument('--reuse',action='store_true');p.add_argument('--interactive',action='store_true')
p.add_argument('--reset',action='store_true');p.add_argument('--cells',type=int)
p.add_argument('--request',choices=('arcnone','arcdroid'),default='arcnone')
p.add_argument('--floppies',type=int,choices=(1,2),default=2)
p.add_argument('--field',type=int,choices=range(0,64,4));p.add_argument('--value',type=lambda value:int(value,0))
p.add_argument('--start',type=int);p.add_argument('--frames',type=int);p.add_argument('--content-id');p.add_argument('--timeout',type=int,default=900)
a=p.parse_args();model,layout=PAIRS[a.pair]
base=Path(a.base).resolve();out=Path(a.out).resolve();system=Path(a.system).resolve()
evidence=Path(__file__).resolve().parents[1]/'.evidence'
if evidence not in out.parents:p.error("--out must be under this clone's .evidence directory")
if out==base or base in out.parents or out.exists():p.error('--out must be a new directory outside --base')
if (a.controller or a.release_fail or a.reuse) and (a.prepare or a.revision1):p.error('run-B fixtures require a populated revision-2 leg')
if a.controller and a.case!='valid':p.error('controller fixture requires --case valid')
if a.reuse and a.case!='valid':p.error('reuse fixture requires --case valid')
if a.field is not None and (a.controller or a.reuse):p.error('descriptor mutation cannot be replaced by a controller/reuse fixture')
if a.reuse and not a.release_fail:p.error('reuse fixture requires --release-fail')
if a.prepare and a.revision1:p.error('preparation and revision-1 admission are separate legs')
if (a.field is None)!=(a.value is None):p.error('--field and --value must be supplied together')
if a.field is not None and (a.prepare or a.revision1 or a.case not in ('valid','unknown-action')):p.error('descriptor mutation requires an explicit valid helper leg')
ids=[d.name for d in (base/'arcdroid/hd-riscpc').glob('*') if d.is_dir() and re.fullmatch('[0-9a-f]{40}',d.name)]
identity=a.content_id or (ids[0] if len(ids)==1 else None)
if not identity or not re.fullmatch('[0-9a-f]{40}',identity):p.error('provide --content-id for a base without exactly one HD identity')
shutil.copytree(base,out)
# A seed may itself be a previous qualification leg. Its diagnostics are not
# evidence for this invocation; remove only this tool's outputs in the copy.
for name in ('behaviour.json','gate.jsonl','gate.ppm','request.ppm','reuse.ppm',
             'return.ppm','runner.out','runner.log','frames.csv','preparation.csv'):
 (out/name).unlink(missing_ok=True)
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
 if a.controller:data=replace_carrier(data,controller())
 if a.case=='truncated-media':
  # Put the authored file beyond any map/identity stdio read-ahead. A live
  # truncation must fault the medium, rather than reuse a buffered first track.
  data=replace_carrier(data,data[1792:1936],2000)
 (out/'helper.adl').write_bytes(data)
start=a.start or (3500 if model=='arm610' else 1800)
# A later unrelated request still scans the deliberately truncated floppy.
# Allow that normal scan to reach the Wimp before snapshotting/cancelling it.
slow_request=a.case=='truncated-media'
request_delay=1050 if slow_request else 820
cancel_delay=1250 if slow_request else 900
return_delay=1450 if slow_request else 1100
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
  shifts={'$':'4','*':'8',':':';','"':'2','%':'5','&':'7','+':'=','(':'9',')':'0','!':'1','<':','}
  shifted=c in shifts or c.isupper();value=ord(shifts.get(c,c.lower()))
  if shifted:event(304,True);f+=1
  key(value)
  if shifted:event(304,False);f+=1
key(293);f+=15
text(f'configure floppies {a.floppies}' if a.prepare else 'show arcgate$e*');key(13)
if not a.prepare:text('show arcgate$r*');key(13)
if not a.prepare:
 if a.reuse:
  text('basic');key(13);f+=30
  text('DIM reuse% 32768:FOR i%=0 TO 32764 STEP 4:reuse%!i%=&deadbeef:NEXT');key(13)
  text('SYS &20033,256 TO answer%:PRINT answer%:PRINT 12345678:QUIT');key(13)
  text('basic -quit adfs::4.$.arcgate');key(13)
 f=max(f,start+2100 if a.reuse else start+360);text(f'wimptask cat adfs::{a.request}.$');key(13)
 f=max(f,start+2400 if a.reuse else start+cancel_delay);key(27);f=max(f,start+2600 if a.reuse else start+return_delay);key(27)
(out/'input').write_text('\n'.join(events)+'\n')
plan=f'version=2\nmodel={model}\nfirmware={Path(a.firmware).resolve()}\ncmos=persist\ncmos_layout={layout}\ncontent_id={identity}\nrtc_epoch=946684800\nhd_populate=on\n'
if a.prepare or a.revision1 or a.case=='empty':plan+='drive1=\n'
elif a.case!='generated':plan+='drive1='+str(out/'helper.adl')+'\n'
if a.cells is not None:plan+=f'observer_cells={a.cells}\n'
if a.release_fail:plan+='gate_release_fail=once\n'
if a.trace:plan+='observation_trace=gate.jsonl\nobservation_classes=swi\nobservation_status=authored\nobservation_swis=0x20006,0x600de,0x20033\n'
if not a.trace or a.reuse or a.interactive:plan+='input='+str(out/'input')+'\n'
(out/'case.arcplan').write_text(plan)
def cmos():
 # Compare the newest valid payload per key. Comparing both slots as a
 # multiset can miss a change that rotates back to the older slot's value.
 active={}
 for file in (out/'arcdroid/cmos-riscpc').glob('*'):
  if not file.is_file() or file.suffix not in ('.0','.1'):continue
  data=file.read_bytes()
  if len(data)!=512 or data[:8] not in (b'ARCRPCM1',b'ARCRPCD1') or zlib.crc32(data[:508])!=struct.unpack_from('<I',data,508)[0]:continue
  key=file.stem;generation=struct.unpack_from('<Q',data,24)[0]
  if generation and (key not in active or generation>active[key][0]):active[key]=(generation,data[104:344])
 return {key:value[1] for key,value in active.items()}
before=cmos();frames=a.frames or (start+(2850 if a.reuse else 1650 if slow_request and a.interactive else 1300) if not a.prepare else start+500)
if a.reset:frames+=start
command=[a.runner,'--core',a.core,'--plan',str(out/'case.arcplan'),'--system',str(system),'--save',str(out),'--frames',str(frames),'--disk-writes','off','--vfs-flush','retroarch','--preparation-timing',str(out/'preparation.csv'),'--timing',str(out/'frames.csv')]
if a.reset:command+=['--reset-at',str(start-50)]
if not a.trace or a.reuse or a.interactive:
 for frame,name in ((start+300,'gate'),(start+1900 if a.reuse else start+request_delay,'reuse' if a.reuse else 'request'),(frames-1,'return')):
  if frame<frames:command+=['--dump-frame',str(frame),str(out/(name+'.ppm'))]
if a.trace and not (a.reuse or a.interactive):command+=['--dump-frame',str(frames-1),str(out/'return.ppm')]
truncated=False
with (out/'runner.out').open('w') as stdout,(out/'runner.log').open('w') as stderr:
 child=subprocess.Popen(command,stdout=stdout,stderr=stderr)
 try:
  if a.case=='truncated-media' and not a.prepare:
   for n in range(20000):
    if 'Input resolution:' in (out/'runner.log').read_text():
     staged=out/'arcdroid/media'/hashlib.sha1(data).hexdigest()/'disc-readonly.adl'
     if not staged.is_file():raise RuntimeError('authored mounted carrier was not staged')
     for carrier in (out/'helper.adl',staged):
      with carrier.open('r+b') as medium:medium.truncate(1024)
     truncated=True
     break
    if child.poll() is not None:break
    time.sleep(.01)
  code=child.wait(timeout=a.timeout)
 except BaseException:
  if child.poll() is None:
   child.terminate()
   try:child.wait(timeout=3)
   except subprocess.TimeoutExpired:child.kill();child.wait()
  raise
result={'pair':a.pair,'case':a.case,'preparation':a.prepare,'returncode':code,'cmos_records':len(before),'cmos_unchanged':before==cmos()}
if not a.prepare and not result['cmos_unchanged']:code=1
if a.case=='truncated-media' and not a.prepare and not truncated:code=1
if a.trace:
 rows=[]
 with (out/'gate.jsonl').open() as source:
  for line in source:
   row=json.loads(line)
   if row.get('incomplete'):code=1
   if row.get('status_valid') or row.get('swi')==0x600de or (row.get('swi')==0x20033 and row['registers'][0]==256):rows.append(row)
 if 'incomplete' in (out/'runner.log').read_text():code=1
 markers=[r for r in rows if r.get('status_valid') and r['status']['role']==0]
 entered=[r for r in markers if r['status']['event']==1];returned=[r for r in markers if r['status']['event']==2]
 result.update(entries=len(entered),returns=len(returned),elapsed_ns=[end['ns']-begin['ns'] for begin,end in zip(entered,returned)],
  elapsed_centiseconds=[(end['status']['detail0']-begin['status']['detail0'])&0xffffffff for begin,end in zip(entered,returned)],
  helper_status=[r['status']['detail0'] for r in markers if r['status']['event']==3],
  release_failures=[r['status']['detail1'] for r in returned])
 if not entered or len(entered)!=len(returned) or any(value>120000000000 for value in result['elapsed_ns']):code=1
 expected=[] if a.field is not None or a.release_fail else ([2] if a.case in ('generated','valid','replacement') else [3] if a.case=='unknown-action' else [])
 if a.reuse:expected=[2] if a.case=='valid' else []
 if result['helper_status']!=expected*(2 if a.reset else 1):code=1
 if a.release_fail and result['release_failures']!=([1,0] if a.reuse else [1]):code=1
 if a.reuse and len(entered)!=2:code=1
 if a.controller:
  events=[r['status']['event'] for r in markers if 4<=r['status']['event']<=7]
  result['controller_events']=events
  result['bytev_calls']=[r['status']['detail1'] for r in markers if r['status']['event']==7]
  registrations=[r for r in markers if r['status']['event']==4]
  start_returns=[r for r in markers if r['status']['event']==5]
  starts=[r for r in rows if r.get('swi')==0x600de and any(lo['ns']<=r['ns']<=hi['ns'] for lo,hi in zip(registrations,start_returns))]
  result['starts_in_usr']=bool(starts) and all(r['mode'] in (0,16) for r in starts)
  if events!=[4,5,6,7] or not result['starts_in_usr'] or not result['bytev_calls'] or min(result['bytev_calls'])<3:code=1
 if a.reuse:
  result['reuse_vector_calls']=sum(r.get('swi') in (0x33,0x20033) and r['registers'][0]==256 and r['ns']>returned[0]['ns'] for r in rows) if returned else 0
  if not result['reuse_vector_calls']:code=1
(out/'behaviour.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
raise SystemExit(code)
