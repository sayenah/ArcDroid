#!/usr/bin/env python3
"""Enumerated predicate mutations for the launch slice. GPL-2.0-only.

Each lexical predicate has stable source/line/text in the JSON inventory.
Mutations preserve evaluation (including I/O), changing only its truth value.
Compilation failures are errors, never kills. Equivalence is reviewed explicitly.
"""
import argparse,hashlib,json,os,re,subprocess,sys,tempfile
from pathlib import Path
from rpc_ninja_commands import rebuild_commands,split_command,same_path

# Whole new functions plus changed predicates in mixed legacy functions.
SCOPE={
 'core/rpc_launch_attempt.c':('attempt',None),
 'core/rpc_boot_record.c':('record',None),
 'core/rpc_launch_controller.c':('controller',None),
 'libretro/rpc_launch_record_store.c':('publisher',None),
 'libretro/rpc_cmos_store.c':('cmos',{'arc_riscpc_cmos_two_floppies'}),
 'engine/arc_boot_watch.c':('boot',set()),
 'engine/arc_launch_outcome.c':('outcome',set()),
 'engine/rpc/rpc_observers.c':('adapter',{'ram','authored','status_event','attempt_event','prompt_end','arc_rpc_observers_boundary','arc_rpc_observers_attempt','arc_rpc_observers_attempt_input','arc_rpc_observers_attempt_interrupt'}),
 'core/launch_discovery.c':('product',{'arc_rpc_directory_identity','arc_rpc_run_valid','arc_rpc_discovery_read','arc_launch_discover_rpc'}),
 'libretro/rpc_preparation.c':('preparation',{'product_read','product_step','launch_aborted','launch_boundary','arc_riscpc_preparation_reset','arc_riscpc_preparation_helper_override','arc_riscpc_preparation_helper_write_policy','arc_riscpc_preparation_execution_allowed','arc_riscpc_preparation_interrupt'}),
 'libretro/arcdroid_libretro.c':('frontend',{'arc_rpc_plan_media_identity'}),
 'core/rpc_boot_filecore.c':('filecore',{'read_bits','arc_boot_layout_accept'}),
 'core/rpc_launch_helper.c':('carrier',{'arc_riscpc_helper_carrier'}),
 'engine/arcdroid_engine.c':('engine',{'arc_engine_rpc_launch_bind','arc_engine_rpc_launch_input','arc_engine_rpc_launch_interrupt','arc_engine_rpc_launch_media_changed'}),
}
SCOPE['libretro/arcdroid_libretro.c'][1].update({'arc_disk_rpc_pending','arc_rpc_plan_restage_drive0','arc_rpc_plan_drive1_view','arc_rpc_plan_drive1_writable'})

def masked(text):
 return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:''.join('\n' if c=='\n' else 'x' if m[0][0] in '\"\'' else ' ' for c in m.group()),text,flags=re.S)
def end_at(code,start,opening='(',closing=')'):
 depth=0
 for i in range(start,len(code)):
  if code[i]==opening:depth+=1
  elif code[i]==closing:
   depth-=1
   if not depth:return i
 raise ValueError('unbalanced C')
def atoms(code,lo,hi):
 while lo<hi and code[lo].isspace():lo+=1
 while lo<hi and code[hi-1].isspace():hi-=1
 if lo==hi:return []
 if code[lo]=='(' and end_at(code,lo)==hi-1:return atoms(code,lo+1,hi-1)
 if code[lo]=='!' and lo+1<hi and code[lo+1]=='(' and end_at(code,lo+1)==hi-1:return atoms(code,lo+2,hi-1)
 depth=0;question=None;nest=0
 for i in range(lo,hi):
  if code[i]=='(':depth+=1
  elif code[i]==')':depth-=1
  elif not depth and code[i]=='?':
   if question is None:question=i
   nest+=1
  elif not depth and code[i]==':' and question is not None:
   nest-=1
   if not nest:return atoms(code,lo,question)+atoms(code,question+1,i)+atoms(code,i+1,hi)
 depth=0;parts=[];last=lo;i=lo
 while i<hi:
  if code[i]=='(':depth+=1
  elif code[i]==')':depth-=1
  elif depth==0 and code[i:i+2] in ('&&','||'):
   parts+=atoms(code,last,i);last=i+2;i+=1
  i+=1
 return parts+atoms(code,last,hi) if parts else [(lo,hi)]
def inventory(root):
 rows=[]
 for relative,(group,whole) in SCOPE.items():
  text=(root/relative).read_text(encoding='utf-8');code=masked(text)
  diff=subprocess.check_output(['git','diff','--unified=0','efafff9','--',relative],cwd=root,text=True)
  changed=set()
  for h in re.finditer(r'^@@ .*?\+(\d+)(?:,(\d+))? @@',diff,re.M):
   start=int(h[1]);changed.update(range(start,start+int(h[2] or 1)))
  functions=[]
  for m in re.finditer(r'^(?:static\s+)?[\w *]+\b(\w+)\([^;{}]*\)\s*\{',code,re.M):
   begin=code.index('{',m.start());functions.append((begin,end_at(code,begin,'{','}'),m[1]))
  seen=set()
  candidates=[]
  for m in re.finditer(r'\b(if|while|for)\s*\(',code):
   lo=code.index('(',m.start())+1;hi=end_at(code,lo-1);line=text.count('\n',0,lo)+1
   function=next((name for a,b,name in functions if a<lo<b),'')
   if whole is not None and function not in whole and not any(i in changed for i in range(line,text.count('\n',0,hi)+2)):continue
   # Loop bounds are included; init/increment are not predicates.
   if m[1]=='for':
    fields=[];depth=0
    for i in range(lo,hi):
     if code[i]=='(':depth+=1
     elif code[i]==')':depth-=1
     elif code[i]==';' and not depth:fields.append(i)
    if len(fields)!=2:continue
    lo,hi=fields[0]+1,fields[1]
   candidates.append((lo,hi,function))
  # Boolean return expressions are guards too (token/key identity, profiles).
  for m in re.finditer(r'\breturn\s+',code):
   lo=m.end();hi=code.index(';',lo);line=text.count('\n',0,lo)+1
   owner=next(((x,y,name) for x,y,name in functions if x<lo<y),None)
   if not owner:continue
   function=owner[2];header=code[max(code.rfind('}',0,owner[0]),code.rfind(';',0,owner[0]))+1:owner[0]]
   if not re.search(r'\bbool\b',header[:header.rfind(function)]):continue
   if whole is not None and function not in whole and not any(i in changed for i in range(line,text.count('\n',0,hi)+2)):continue
   if code[lo:hi].strip() in ('true','false'):continue
   candidates.append((lo,hi,function))
  for lo,hi,function in candidates:
   for a,b in atoms(code,lo,hi):
    if (a,b) in seen:continue
    seen.add((a,b));expr=text[a:b]
    actual_group='binding' if relative=='libretro/rpc_preparation.c' and function in ('launch_bindings','launch_record_valid','launch_revalidate','launch_media_changed','arc_riscpc_preparation_maintenance','arc_riscpc_preparation_observation') else group
    rows.append(dict(source=relative,group=actual_group,function=function,line=text.count('\n',0,a)+1,start=a,end=b,expression=expr))
 return rows

def transitions(root):
 rows=[]
 for relative,group,patterns in (
  ('core/rpc_launch_attempt.c','attempt',[(r'a->(?:delivery|outcome|live|resume_allowed)\s*=(?!=)[^;]+;', '(void)0;'),(r'case ARC_RPC_EVENT_\w+:','case -1:')]),
  ('libretro/rpc_launch_record_store.c','publisher',[(r's->phase\s*=(?!=)[^;]+;','(void)0;')]),
  ('core/rpc_launch_controller.c','controller',[(r'notify\(&a,ARC_RPC_STATUS_\w+\);','(void)0;')])):
  source=(root/relative).read_text(encoding='utf-8')
  for pattern,replacement in patterns:
   for m in re.finditer(pattern,source):rows.append(dict(source=relative,group=group,function='state transition',line=source.count('\n',0,m.start())+1,start=m.start(),end=m.end(),expression=m[0],replacement=replacement))
 return rows

def source_hash(path):
 # Git's Windows checkout conversion must not invalidate a text-source pin.
 # Preserve every other byte, including whitespace and standalone CR bytes.
 return hashlib.sha256(path.read_bytes().replace(b'\r\n',b'\n')).hexdigest()

def write_inventory(root,path,entries):
 path.write_text(json.dumps({'license':'GPL-2.0-only','sources':{s:source_hash(root/s) for s in SCOPE},'predicates':entries,'equivalent':{}},indent=2)+'\n',encoding='utf-8')

def checked_inventory(root):
 audited=json.loads((root/'tests/rpc_launch_predicates.json').read_text(encoding='utf-8'));entries=audited['predicates']
 for source,digest in audited['sources'].items():
  if source_hash(root/source)!=digest:raise RuntimeError('refresh mutation audit after source change: '+source)
 for e in entries:
  if (root/e['source']).read_text(encoding='utf-8')[e['start']:e['end']]!=e['expression']:raise RuntimeError('stale mutation offset: '+e['source'])
 return entries

def main():
 p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('root',type=Path);p.add_argument('fixtures',type=Path)
 p.add_argument('--inventory',type=Path);p.add_argument('--results',type=Path);p.add_argument('--group');p.add_argument('--start',type=int,default=0)
 p.add_argument('--operator',choices=['negation','both'],default='negation')
 p.add_argument('--only',help='comma-separated inventory IDs for audit reruns')
 p.add_argument('--baseline',action='store_true',help='run the original supplements without mutations')
 p.add_argument('--policy-only',action='store_true',help='use the frontend policy boundary supplement directly')
 a=p.parse_args();root=a.root.resolve();build=a.build.resolve();fixtures=a.fixtures.resolve()
 if a.inventory:
  entries=inventory(root)
  entries+=transitions(root)
  write_inventory(root,a.inventory,entries)
 else:
  entries=checked_inventory(root)
  audited=json.loads((root/'tests/rpc_launch_predicates.json').read_text(encoding='utf-8'))
 if not a.results:return
 audit_files=[Path(__file__).resolve(),*sorted((root/'tests').glob('rpc_*audit.inc')),
  *[root/'tests'/name for name in ('rpc_launch_publication_test.py','rpc_launch_controller_guest.py','rpc_orchestration_test.c','core_tests.c','rpc_observers_frontend_test.c','rpc_launch_product_test.c','rpc_launch_test.c','rpc_launch_record_store_test.c')]]
 audit_hashes={str(f):source_hash(f) for f in audit_files}
 cache=(build/'CMakeCache.txt').read_text(encoding='utf-8');ninja=next(l.split('=',1)[1] for l in cache.splitlines() if l.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
 commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-C',str(build),'-t','compdb','-x'],text=True))]
 parsed=[split_command(s,os.name=='nt') for c in commands for s in c.split('&&') if ' -o ' in s]
 suffix='.dll' if os.name=='nt' else '.dylib' if sys.platform=='darwin' else '.so'
 exe_suffix='.exe' if os.name=='nt' else ''
 targets={'attempt':'rpc_launch_test','record':'rpc_launch_test','controller':'rpc_launch_controller_test','carrier':'rpc_launch_controller_test','publisher':'rpc_launch_record_store_test','cmos':'rpc_cmos_store_test','boot':'boot_watch_test','outcome':'launch_outcome_test','adapter':'rpc_observers_test','product':'rpc_launch_product_test','filecore':'rpc_launch_product_test','preparation':'rpc_orchestration_test','frontend':'arcdroid_rpcplan_libretro','binding':'arcdroid_rpcplan_libretro','engine':'arcdroid_rpcplan_libretro','generation':'arcdroid_rpcplan_libretro'}
 results=[];baselines=set()
 with tempfile.TemporaryDirectory(prefix='arc-predicate-') as tmp:
  tmp=Path(tmp)
  def binding_supplement(source_text,engine_source=None):
   test=root/'tests/rpc_orchestration_test.c';hc,hl=rebuild_commands(commands,test,'rpc_orchestration_test',build,os.name=='nt')
   prep=root/'libretro/rpc_preparation.c'
   pc=next(r.copy() for r in parsed if '-c' in r and same_path(r[r.index('-c')+1],prep,build,os.name=='nt') and any(same_path(x,r[r.index('-o')+1],build,os.name=='nt') for x in hl if x.endswith(('.o','.obj'))))
   original_p=pc[pc.index('-o')+1];original_h=hc[hc.index('-o')+1]
   ps=tmp/'audit_preparation.c';hs=tmp/'audit_orchestration.c';po=tmp/'audit_preparation.o';ho=tmp/'audit_orchestration.o';binary=tmp/('audit'+exe_suffix)
   ps.write_text(source_text+'\n#include "'+str(root/'tests/rpc_launch_binding_audit.inc')+'"\n',encoding='utf-8')
   text=test.read_text(encoding='utf-8');anchor='ArcRpcLaunchAttempt a;arc_riscpc_preparation_attempt(p,&a);assert(a.delivery==ARC_RPC_DELIVERY_PREPARED);'
   assert text.count(anchor)==1
   text=text.replace('static unsigned notices','extern void arc_predicate_binding_audit(ArcRpcPreparation *);\nextern void arc_predicate_reset_before(ArcRpcPreparation *);\nextern void arc_predicate_reset_after(ArcRpcPreparation *);\nstatic unsigned notices',1).replace(anchor,anchor+'arc_predicate_binding_audit(p);')
   for name,hook in (('reset_drive0','arc_predicate_reset_before'),('reset_rebind','arc_predicate_reset_after')):
    marker='static bool '+name+'(void *opaque,char *error,size_t n)\n{'
    assert text.count(marker)==1;text=text.replace(marker,marker+hook+'(current);')
   marker='assert(arc_riscpc_preparation_execution_allowed(p)==!failure);'
   assert text.count(marker)==1;text=text.replace(marker,marker+'assert(a.resume_allowed==!failure);')
   marker='assert(arc_engine_active() && arc_engine_is_rpc());'
   assert text.count(marker)==1;text=text.replace(marker,marker+'assert(!arc_riscpc_preparation_execution_allowed(current));')
   hs.write_text(text,encoding='utf-8')
   for command,source,obj in ((pc,ps,po),(hc,hs,ho)):
    command[command.index('-c')+1]=str(source);command[command.index('-o')+1]=str(obj)
    command+=['-I',str(prep.parent),'-I',str(test.parent)]
    if '-MF' in command:command[command.index('-MF')+1]=str(obj)+'.d'
    subprocess.run(command,cwd=build,check=True,capture_output=True)
   engine=root/'engine/arcdroid_engine.c'
   ec=next(r.copy() for r in parsed if '-c' in r and same_path(r[r.index('-c')+1],engine,build,os.name=='nt') and any(same_path(x,r[r.index('-o')+1],build,os.name=='nt') for x in hl if x.endswith(('.o','.obj'))))
   original_engine=ec[ec.index('-o')+1];es=tmp/'audit_engine.c';eo=tmp/'audit_engine.o'
   proxy='''
#undef arc_rpc_observers_attempt_input
extern void arc_rpc_observers_attempt_input(void);
static unsigned audit_input_count;
void arc_predicate_input_proxy(void){++audit_input_count;arc_rpc_observers_attempt_input();}
unsigned arc_predicate_input_calls(void){return audit_input_count;}
'''
   es.write_text('#define arc_rpc_observers_attempt_input arc_predicate_input_proxy\n'+(engine.read_text(encoding='utf-8') if engine_source is None else engine_source)+proxy,encoding='utf-8')
   ec[ec.index('-c')+1]=str(es);ec[ec.index('-o')+1]=str(eo);ec+=['-I',str(engine.parent)]
   if '-MF' in ec:ec[ec.index('-MF')+1]=str(eo)+'.d'
   subprocess.run(ec,cwd=build,check=True,capture_output=True)
   hl=[str(eo) if same_path(x,original_engine,build,os.name=='nt') else x for x in hl]
   hl=[str(po) if same_path(x,original_p,build,os.name=='nt') else str(ho) if same_path(x,original_h,build,os.name=='nt') else x for x in hl];hl[hl.index('-o')+1]=str(binary)
   subprocess.run(hl,cwd=build,check=True,capture_output=True)
   return subprocess.run([sys.executable,'-B',str(root/'tests/rpc_orchestration_test.py'),str(binary),str(fixtures),'launch'],cwd=build,capture_output=True,timeout=180)
  def carrier_supplement(obj):
   source=root/'tests/rpc_launch_controller_test.c';hc,hl=rebuild_commands(commands,source,'rpc_launch_controller_test',build,os.name=='nt')
   old=hc[hc.index('-o')+1];hs=tmp/'carrier_audit.c';ho=tmp/'carrier_audit.o'
   text=source.read_text(encoding='utf-8').replace('int main(int argc,char **argv)','\n#include "'+str(root/'tests/rpc_carrier_audit.inc')+'"\nint main(int argc,char **argv)')
   text=text.replace('    ArcRpcLaunchPlan p=','    arc_carrier_audit();\n    ArcRpcLaunchPlan p=',1);hs.write_text(text,encoding='utf-8')
   hc[hc.index('-c')+1]=str(hs);hc[hc.index('-o')+1]=str(ho);hc+=['-I',str(source.parent)]
   if '-MF' in hc:hc[hc.index('-MF')+1]=str(ho)+'.d'
   subprocess.run(hc,cwd=build,check=True,capture_output=True)
   binary=tmp/('carrier'+exe_suffix);hl=[str(ho) if same_path(x,old,build,os.name=='nt') else x for x in hl];hl.insert(hl.index('-o'),str(obj));hl[hl.index('-o')+1]=str(binary)
   subprocess.run(hl,cwd=build,check=True,capture_output=True)
   return subprocess.run([str(binary)],cwd=build,capture_output=True,timeout=180)
  def filecore_supplement(source_text):
   source=root/'tests/rpc_launch_product_test.c';hc,hl=rebuild_commands(commands,source,'rpc_launch_product_test',build,os.name=='nt')
   filecore=root/'core/rpc_boot_filecore.c';fc=next(r.copy() for r in parsed if '-c' in r and same_path(r[r.index('-c')+1],filecore,build,os.name=='nt'))
   fs=tmp/'filecore_audit.c';fo=tmp/'filecore_audit.o';hs=tmp/'product_audit.c';ho=tmp/'product_audit.o';old=hc[hc.index('-o')+1]
   fs.write_text(source_text+'\n#include "'+str(root/'tests/rpc_filecore_audit.inc')+'"\n',encoding='utf-8')
   hs.write_text(source.read_text(encoding='utf-8').replace('int main(void)','extern void arc_predicate_filecore_audit(const uint8_t *,const uint8_t *);\nint main(void)').replace('    prepare();ArcLaunchDiscovery d;','    prepare();arc_predicate_filecore_audit(boot,maps);ArcLaunchDiscovery d;',1),encoding='utf-8')
   for command,copy,obj in ((hc,hs,ho),(fc,fs,fo)):
    command[command.index('-c')+1]=str(copy);command[command.index('-o')+1]=str(obj);command+=['-I',str(filecore.parent),'-I',str(source.parent)]
    if '-MF' in command:command[command.index('-MF')+1]=str(obj)+'.d'
    subprocess.run(command,cwd=build,check=True,capture_output=True)
   binary=tmp/('filecore'+exe_suffix);hl=[str(ho) if same_path(x,old,build,os.name=='nt') else x for x in hl];hl.insert(hl.index('-o'),str(fo));hl[hl.index('-o')+1]=str(binary)
   subprocess.run(hl,cwd=build,check=True,capture_output=True)
   return subprocess.run([str(binary)],cwd=build,capture_output=True,timeout=180)
  def frontend_supplement(source_text):
   source=root/'libretro/arcdroid_libretro.c';fc,fl=rebuild_commands(commands,source,'arcdroid_rpcplan_libretro',build,os.name=='nt')
   old=fc[fc.index('-o')+1];fs=tmp/'frontend_audit.c';fo=tmp/'frontend_audit.o';core=tmp/('frontend_audit'+suffix)
   proxies={'arc_engine_is_rpc':'is_rpc','arc_engine_rpc_launch_input':'input','arc_engine_rpc_launch_media_changed':'media','arc_engine_rpc_launch_interrupt':'interrupt','arc_engine_launch_outcome_cancel':'launch_cancel','arc_engine_boot_watch_cancel':'boot_cancel','arc_engine_media_eject':'eject','arc_engine_media_insert':'insert','arc_engine_stop':'stop','arc_engine_disc_controller_idle':'idle','arc_engine_rpc_observers_replace':'replace','arc_test_plan_pending':'pending'}
   prefix=''.join('#define '+name+' arc_predicate_'+proxy+'\n' for name,proxy in proxies.items())
   at=source_text.index('static bool arc_rpc_plan_restage_drive0(');begin=source_text.index('{',at);end=end_at(masked(source_text),begin,'{','}')+1
   body=source_text[at:end]
   for before,after in (('arc_read_options','arc_predicate_options'),('arc_reprepare_zip_media','arc_predicate_zip'),('arc_reprepare_folder_media','arc_predicate_folder'),('arc_prepare_source','arc_predicate_prepare'),('arc_entry_tail_presentation','arc_predicate_tail'),('arc_refresh_jfd','arc_predicate_refresh')):body=body.replace(before+'(',after+'(')
   prototypes='''
static bool arc_predicate_options(ArcCoreOptions *,char *,size_t);
static bool arc_predicate_zip(ArcDiskWriteMode,unsigned *,char *,size_t),arc_predicate_folder(ArcDiskWriteMode,unsigned *,char *,size_t);
static bool arc_predicate_prepare(const char *,const char *,ArcDiskWriteMode,bool,bool *,ArcMediaEntry *,char *,size_t);
static void arc_predicate_tail(ArcMediaEntry *,bool);
static bool arc_predicate_refresh(ArcMediaEntry *,ArcDiskWriteMode,ArcJfdWritePolicy,char *,size_t);
'''
   source_text=source_text[:at]+prototypes+body+source_text[end:]
   fs.write_text(prefix+source_text+'\n#include "'+str(root/'tests/rpc_frontend_audit.inc')+'"\n',encoding='utf-8')
   fc[fc.index('-c')+1]=str(fs);fc[fc.index('-o')+1]=str(fo);fc+=['-I',str(source.parent)]
   if '-MF' in fc:fc[fc.index('-MF')+1]=str(fo)+'.d'
   subprocess.run(fc,cwd=build,check=True,capture_output=True)
   fl=[str(fo) if same_path(x,old,build,os.name=='nt') else x for x in fl];fl[fl.index('-o')+1]=str(core)
   fl=['-Wl,--out-implib,'+str(tmp/'frontend_audit.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in fl]
   subprocess.run(fl,cwd=build,check=True,capture_output=True)
   test=root/'tests/rpc_observers_frontend_test.c';hc,hl=rebuild_commands(commands,test,'rpc_observers_frontend_test',build,os.name=='nt')
   old=hc[hc.index('-o')+1];hs=tmp/'frontend_harness.c';ho=tmp/'frontend_harness.o';binary=tmp/('frontend_harness'+exe_suffix)
   text=test.read_text(encoding='utf-8');marker='    vfs.read=counted_read;'
   assert text.count(marker)==1
   text=text.replace(marker,'    void (*predicate_audit)(void);LOAD(predicate_audit,"arc_predicate_frontend_audit");\n'+marker)
   marker='        unload();vfs_assert_no_open_handles();';assert text.count(marker)>=1
   hs.write_text(text.replace(marker,'        predicate_audit();unload();vfs_assert_no_open_handles();'),encoding='utf-8')
   hc[hc.index('-c')+1]=str(hs);hc[hc.index('-o')+1]=str(ho);hc+=['-I',str(test.parent)]
   if '-MF' in hc:hc[hc.index('-MF')+1]=str(ho)+'.d'
   subprocess.run(hc,cwd=build,check=True,capture_output=True)
   hl=[str(ho) if same_path(x,old,build,os.name=='nt') else x for x in hl];hl[hl.index('-o')+1]=str(binary)
   subprocess.run(hl,cwd=build,check=True,capture_output=True)
   return subprocess.run([sys.executable,'-B',str(root/'tests/rpc_observers_frontend_test.py'),str(binary),str(core),str(fixtures),'full'],cwd=build,capture_output=True,timeout=300)
  def product_supplement(obj):
   source=root/'tests/core_tests.c';compile,link=rebuild_commands(commands,source,'core_tests',build,os.name=='nt')
   old=compile[compile.index('-o')+1];harness=tmp/'core_audit.c';ho=tmp/'core_audit.o'
   if not ho.exists():
    text=source.read_text(encoding='utf-8');anchor='int main(int argc, char **argv)\n{'
    supplement='''
   assert(argc>=2);
   size_t audit_size;uint8_t *audit_data=read_fixture(argv[1],"arcdroid-application-l.adl",&audit_size);
   ArcMediaInfo audit_info;ArcLaunchDiscovery audit_disc;char audit_error[128];
   assert(arc_media_inspect("authored.adl",audit_data,audit_size,&audit_info,audit_error,sizeof(audit_error)));
   audit_data[512+5]=0x81; /* Nonempty raw entry, empty decoded name. Legacy discovery skips it. */
   assert(arc_launch_discover(audit_data,audit_size,&audit_info,NULL,&audit_disc) && audit_disc.readable && !audit_disc.application_count);
   assert(!arc_launch_discover(NULL,0,NULL,NULL,&audit_disc));free(audit_data);
'''
    assert text.count(anchor)==1;harness.write_text(text.replace(anchor,anchor+supplement),encoding='utf-8')
    compile[compile.index('-c')+1]=str(harness);compile[compile.index('-o')+1]=str(ho);compile+=['-I',str(source.parent)]
    if '-MF' in compile:compile[compile.index('-MF')+1]=str(ho)+'.d'
    subprocess.run(compile,cwd=build,check=True,capture_output=True)
   link=[str(ho) if same_path(x,old,build,os.name=='nt') else x for x in link]
   binary=tmp/('core_audit'+exe_suffix);link.insert(link.index('-o'),str(obj));link[link.index('-o')+1]=str(binary)
   subprocess.run(link,cwd=build,check=True,capture_output=True)
   return subprocess.run([str(binary),str(fixtures)],cwd=build,capture_output=True,timeout=180)
  def controller_transport(obj,builder):
   _,link=rebuild_commands(commands,root/'tests/arc_test_plan.c','arcdroid_rpcplan_libretro',build,os.name=='nt')
   core=tmp/('transport'+suffix);link.insert(link.index('-o'),str(obj));link[link.index('-o')+1]=str(core)
   link=['-Wl,--out-implib,'+str(tmp/'transport.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in link]
   subprocess.run(link,cwd=build,check=True,capture_output=True)
   return subprocess.run([sys.executable,'-B',str(root/'tests/rpc_launch_publication_test.py'),str(build/('rpc_runner'+exe_suffix)),str(core),str(builder),str(fixtures)],cwd=build,capture_output=True,timeout=180)
  for index,e in enumerate(entries):
   if index<a.start or (a.group and e['group']!=a.group) or (a.only and str(index) not in a.only.split(',')):continue
   group=e['group'];target=targets[group];source=root/e['source'];test_source=source if group=='frontend' else root/'tests/arc_test_plan.c' if group in ('binding','generation','engine') else root/'tests'/(target+'.c')
   _,link=rebuild_commands(commands,test_source,target,build,os.name=='nt')
   compile=next(r.copy() for r in parsed if '-c' in r and same_path(r[r.index('-c')+1],source,build,os.name=='nt') and
      (e['source'].startswith('core/') or any(same_path(x,r[r.index('-o')+1],build,os.name=='nt') for x in link if x.endswith(('.o','.obj')))))
   old_obj=compile[compile.index('-o')+1];obj=tmp/'mutant.o';copy=tmp/source.name;binary=tmp/('mutant'+(suffix if group in ('frontend','binding','generation','engine') else exe_suffix))
   compile[compile.index('-c')+1]=str(copy);compile[compile.index('-o')+1]=str(obj);compile+=['-I',str(source.parent)]
   if '-MF' in compile:compile[compile.index('-MF')+1]=str(tmp/'mutant.d')
   direct=any(same_path(x,old_obj,build,os.name=='nt') for x in link if x.endswith(('.o','.obj')))
   if direct:link=[str(obj) if same_path(x,old_obj,build,os.name=='nt') else x for x in link]
   else:link.insert(link.index('-o'),str(obj))
   link[link.index('-o')+1]=str(binary)
   link=['-Wl,--out-implib,'+str(tmp/'mutant.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in link]
   command=[str(binary)]
   if group in ('attempt','record'):command += [group,str(fixtures)]
   if group=='product':command += []
   if group=='boot':command += [str(fixtures)]
   if group=='preparation':command=[sys.executable,'-B',str(root/'tests/rpc_orchestration_test.py'),str(binary),str(fixtures),'launch']
   if group in ('frontend','binding','engine'):command=[sys.executable,'-B',str(root/'tests/rpc_launch_publication_test.py'),str(build/('rpc_runner'+exe_suffix)),str(binary),str(build/('rpc_launch_controller_test'+exe_suffix)),str(fixtures)]
   if group=='generation':command=[sys.executable,'-B',str(root/'tests/rpc_cmos_plan_test.py'),str(build/('rpc_cmos_plan_test'+exe_suffix)),str(binary),str(build/('rpc_runner'+exe_suffix)),str(build/('arcdroid_libretro'+suffix)),str(fixtures),'0','full','full']
   original=source.read_text(encoding='utf-8')
   if group not in baselines:
    copy.write_text(original,encoding='utf-8');subprocess.run(compile,cwd=build,check=True,capture_output=True);subprocess.run(link,cwd=build,check=True,capture_output=True)
    r=subprocess.run(command,cwd=build,capture_output=True,timeout=180)
    if r.returncode:raise RuntimeError('baseline '+group+': '+r.stderr.decode(errors='replace')[-2000:])
    if group=='binding':
     r=binding_supplement(original)
     if r.returncode:raise RuntimeError('binding supplement baseline: '+r.stderr.decode(errors='replace')[-2000:])
    if group=='engine':
     r=binding_supplement((root/'libretro/rpc_preparation.c').read_text(encoding='utf-8'),original)
     if r.returncode:raise RuntimeError('engine supplement baseline: '+r.stderr.decode(errors='replace')[-2000:])
    if group=='carrier':
     r=carrier_supplement(obj)
     if r.returncode:raise RuntimeError('carrier supplement baseline: '+r.stderr.decode(errors='replace')[-2000:])
    if group=='filecore':
     r=filecore_supplement(original)
     if r.returncode:raise RuntimeError('FileCore supplement baseline: '+r.stderr.decode(errors='replace')[-2000:])
    if group=='frontend':
     r=subprocess.run([sys.executable,'-B',str(root/'tests/rpc_observers_frontend_test.py'),str(build/('rpc_observers_frontend_test'+exe_suffix)),str(binary),str(fixtures),'full'],cwd=build,capture_output=True,timeout=300)
     if r.returncode:raise RuntimeError('frontend supplement baseline: '+r.stderr.decode(errors='replace')[-2000:])
     r=frontend_supplement(original)
     if r.returncode:raise RuntimeError('frontend policy baseline: '+r.stderr.decode(errors='replace')[-2000:])
    if group=='product':
     r=product_supplement(obj)
     if r.returncode:raise RuntimeError('product supplement baseline: '+r.stderr.decode(errors='replace')[-2000:])
    if group=='preparation':
     r=subprocess.run(command[:-1]+['mutant-idle'],cwd=build,capture_output=True,timeout=900)
     if r.returncode:raise RuntimeError('secondary baseline preparation: '+r.stderr.decode(errors='replace')[-2000:])
    baselines.add(group)
   if a.baseline:continue
   for truth in (('negate',) if a.operator=='negation' else ('false','true')):
    if any(source_hash(Path(f))!=digest for f,digest in audit_hashes.items()):raise RuntimeError('test inputs changed during mutation run; repeat on a frozen audit')
    replacement=e.get('replacement') or ('!('+e['expression']+')' if truth=='negate' else '('+e['expression']+','+truth+')')
    copy.write_text(original[:e['start']]+replacement+original[e['end']:],encoding='utf-8')
    subprocess.run(compile,cwd=build,check=True,capture_output=True);subprocess.run(link,cwd=build,check=True,capture_output=True)
    try:
     if a.policy_only and group=='frontend':r=frontend_supplement(original[:e['start']]+replacement+original[e['end']:])
     else:r=subprocess.run(command,cwd=build,capture_output=True,timeout=60)
     status='killed' if r.returncode else 'SURVIVED'
     if status=='SURVIVED' and group=='attempt':
      r=subprocess.run([str(binary),'record',str(fixtures)],cwd=build,capture_output=True,timeout=60);status='killed' if r.returncode else 'SURVIVED'
     if status=='SURVIVED' and group=='controller':
      r=subprocess.run([sys.executable,'-B',str(root/'tests/rpc_launch_controller_guest.py'),str(build/('rpc_runner'+exe_suffix)),str(build/('arcdroid_rpcplan_libretro'+suffix)),str(binary)],cwd=build,capture_output=True,timeout=180)
      status='killed' if r.returncode else 'SURVIVED'
      if status=='SURVIVED':
       r=controller_transport(obj,binary);status='killed' if r.returncode else 'SURVIVED'
     if status=='SURVIVED' and group=='preparation':
      r=subprocess.run(command[:-1]+['mutant-idle'],cwd=build,capture_output=True,timeout=900)
      status='killed' if r.returncode else 'SURVIVED'
      if status=='SURVIVED':
       r=binding_supplement(original[:e['start']]+replacement+original[e['end']:]);status='killed' if r.returncode else 'SURVIVED'
     if status=='SURVIVED' and group=='binding':
      r=binding_supplement(original[:e['start']]+replacement+original[e['end']:])
      status='killed' if r.returncode else 'SURVIVED'
     if status=='SURVIVED' and group=='product':
      r=product_supplement(obj);status='killed' if r.returncode else 'SURVIVED'
     if status=='SURVIVED' and group=='engine':
      r=binding_supplement((root/'libretro/rpc_preparation.c').read_text(encoding='utf-8'),original[:e['start']]+replacement+original[e['end']:]);status='killed' if r.returncode else 'SURVIVED'
     if status=='SURVIVED' and group=='carrier':
      r=carrier_supplement(obj);status='killed' if r.returncode else 'SURVIVED'
     if status=='SURVIVED' and group=='filecore':
      r=filecore_supplement(original[:e['start']]+replacement+original[e['end']:]);status='killed' if r.returncode else 'SURVIVED'
     if status=='SURVIVED' and group=='frontend' and not a.policy_only:
      r=subprocess.run([sys.executable,'-B',str(root/'tests/rpc_observers_frontend_test.py'),str(build/('rpc_observers_frontend_test'+exe_suffix)),str(binary),str(fixtures),'full'],cwd=build,capture_output=True,timeout=300)
      status='killed' if r.returncode else 'SURVIVED'
      if status=='SURVIVED':
       r=frontend_supplement(original[:e['start']]+replacement+original[e['end']:]);status='killed' if r.returncode else 'SURVIVED'
    except subprocess.TimeoutExpired:status='killed-timeout'
    reason=audited.get('equivalent',{}).get(str(index)) if not a.inventory else None
    if status=='SURVIVED' and reason:status='equivalent'
    row={**e,'id':index,'truth':truth,'status':status}
    if reason:row['reason']=reason
    results.append(row)
    with a.results.open('a') as f:f.write(json.dumps(row)+'\n')
    print(index,group,e['line'],truth,status,flush=True)
  survivors=[r for r in results if r['status']=='SURVIVED']
  if survivors:raise RuntimeError('surviving predicate mutants: '+','.join(str(r['id']) for r in survivors))
if __name__=='__main__':main()
