#!/usr/bin/env python3
"""Recreate QA mutants in temporary objects; never modify repository/build objects.
GPL-2.0-only. Every mutant must compile; only a failed observable check kills it.
Ninja supplies the current compiler/link flags, including sanitizer/platform flags.
"""
from pathlib import Path
import argparse,json,os,subprocess,sys,tempfile
sys.dont_write_bytecode=True
from rpc_ninja_commands import split_command
ROOT=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser()
for key in ('build','core','timing','unit','video','store'):p.add_argument('--'+key,required=True)
p.add_argument('--only',default='');a=p.parse_args();build=Path(a.build).resolve()
cache=(build/'CMakeCache.txt').read_text()
ninja=next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands={};database=None
def run(argv):
    return subprocess.run([str(x) for x in argv],cwd=build,capture_output=True,text=True,timeout=180)
def must_pass(argv):
    r=run(argv);assert r.returncode==0,r.stdout+r.stderr

def compiler_commands(target):
    global database
    if target not in commands:
        # -x expands response files from Ninja's graph, even when Ninja has
        # removed them after a successful Windows link. No rebuild is needed.
        if database is None:
            database=json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))
        def parts(command):
            rows=[]
            for segment in command.split('&&'):
                if ' -c ' not in segment and ' -o ' not in segment:continue
                rows.append(split_command(segment,os.name=='nt'))
            return rows
        entry=next(r for r in database if (build/r['output']).resolve()==Path(target).resolve())
        link=next(row for row in parts(entry['command']) if '-o' in row and '-c' not in row)
        objects={(build/x).resolve() for x in link if x.endswith(('.o','.obj'))}
        rows=[row for r in database if (build/r['output']).resolve() in objects for row in parts(r['command'])]
        commands[target]=rows+[link]
    return commands[target]

def mutated_binary(target,relative,old,new,directory,label):
    source=ROOT/relative;text=source.read_text();assert old in text,(label,'missing recipe')
    # M23 changes both the allocation fast check and the full access check.
    text=text.replace(old,new,1 if label=='V6a' else text.count(old))
    mutated=directory/(label+'-'+source.name);mutated.write_text(text)
    rows=compiler_commands(target)
    compile=next(row[:] for row in rows if '-c' in row and Path(row[row.index('-c')+1]).resolve()==source)
    original_obj=compile[compile.index('-o')+1];obj=directory/(label+'.o')
    for option,value in (('-c',mutated),('-o',obj),('-MF',directory/(label+'.d')),('-MT',obj)):
        if option in compile:compile[compile.index(option)+1]=str(value)
    result=run(compile);assert result.returncode==0,(label,'compile failed',result.stdout+result.stderr)
    def original(x):
        return x.endswith(('.o','.obj')) and (build/x).resolve()==(build/original_obj).resolve()
    link=next(row[:] for row in rows if '-c' not in row and '-o' in row and any(original(x) for x in row))
    output=directory/(label+Path(target).suffix)
    link[next(i for i,x in enumerate(link) if original(x))]=str(obj);link[link.index('-o')+1]=str(output)
    # MinGW import-library/soname side products must also stay in scratch.
    link=['-Wl,--out-implib,'+str(directory/(label+'.dll.a')) if x.startswith('-Wl,--out-implib,') else x for x in link]
    result=run(link);assert result.returncode==0,(label,'link failed',result.stdout+result.stderr)
    return output

TIMING='engine/rpc/rpc_mem_timing.c';VIDEO='engine/rpcemu/src/vidc20.c';STORE='libretro/rpc_cmos_store.c'
GEOMETRY='    m.instruction.sets = model == 0 ? 4 : model == 1 ? 128 : model == 2 ? 16 : 64;\n    m.instruction.ways = model == 0 ? 64 : model == 2 ? 32 : 4;'
# (label, source, old, replacement, link target, observable check)
mutants=[
 ('G2',TIMING,GEOMETRY,'    m.instruction.sets = model == 0 ? 4 : model == 1 ? 128 : model == 2 ? 16 : 128;\n    m.instruction.ways = model == 0 ? 64 : model == 2 ? 32 : model == 1 ? 4 : 2;',a.core,('timing',3,'cache-geometry-fewer-ways')),
 ('G8',TIMING,'m.data.shift = 5; m.data.sets = 16; m.data.ways = 32;','m.data.shift = 5; m.data.sets = 32; m.data.ways = 16;',a.core,('timing',2,'cache-geometry-sa-data')),
 ('G9',TIMING,'model == 2 ? 16 : 64;\n    m.instruction.ways = model == 0 ? 64 : model == 2 ? 32 : 4;','model == 2 ? 32 : 64;\n    m.instruction.ways = model == 0 ? 64 : model == 2 ? 16 : 4;',a.core,('timing',2,'cache-geometry-sa-instruction')),
 ('M39',TIMING,GEOMETRY,'    m.instruction.sets = model == 0 ? 4 : model == 1 ? 128 : model == 2 ? 16 : 4;\n    m.instruction.ways = model == 0 ? 64 : model == 2 ? 32 : model == 3 ? 64 : 4;',a.core,('timing',3,'cache-geometry')),
 ('M40',TIMING,GEOMETRY,'    m.instruction.sets = model == 0 ? 4 : model == 1 ? 8 : model == 2 ? 16 : 64;\n    m.instruction.ways = model == 0 ? 64 : model == 2 ? 32 : model == 1 ? 64 : 4;',a.core,('timing',1,'cache-geometry')),
 ('M02',TIMING,'#define BUFFER 8u','#define BUFFER 7u',a.core,('timing',2,'buffer-9-retire')),
 ('M03',TIMING,'m.model == 0 ? 2 : 4','m.model == 0 ? 3 : 4',a.core,('timing',0,'buffer-3-retire')),
 ('M04',TIMING,'m.model == 0 ? 2 : 4','m.model == 0 ? 2 : 5',a.core,('timing',1,'buffer-5-retire')),
 ('M05',TIMING,'m.bwords + p->count <= 8;','m.bwords + p->count <= 9;',a.core,('timing',0,'word-slots-retire')),
 ('M38',TIMING,'if (m.bcount == BUFFER) return false;','if (m.bcount == BUFFER + (m.active_valid && m.active_kind == BUFFERED)) return false;',a.core,('timing',2,'buffer-9-retire')),
 ('M08',TIMING,'m.fast_clock = false; arc_rpc_mem_control(m.control);','arc_rpc_mem_control(m.control);',a.core,('timing',2,'clock-disable-miss')),
 ('M10',TIMING,'if (enable) m.fast_clock = true;','m.fast_clock = enable;',a.core,('timing',2,'clock-disable-hot')),
 ('M21',TIMING,'if (!m.idle && !arc_rpc_mem_fast.pending) {','if (0) {',a.unit,('unit',)),
 ('M21-ROM',TIMING,'if (!m.idle && !arc_rpc_mem_fast.pending) {','if (0) {',a.core,('timing',2,'hot-dma')),
 ('M14',TIMING,'(p->address[i] & 1023u) != 0;','true;',a.core,('timing',0,'bank-local-restart-ldm')),
 ('M23',TIMING,'(m.control & 0x1000) && (!(m.control & 1) || (attributes & 8))','(m.control & 0x1000) && (attributes & 8)',a.core,('timing',2,'icache-m0')),
 ('V1',VIDEO,'dest[3] = 0xff000000u | vidc.rgb_palette[word >> 24];','dest[3] = 0xff000000u | vidc.rgb_palette[(word >> 16) & 255u];',a.core,('video',)),
 ('V2',VIDEO,'palette_word(out + origin + pixel + lane * 4u, words ? words[lane] : UINT32_MAX);','palette_word(out + origin + pixel + (3u - lane) * 4u, words ? words[lane] : UINT32_MAX);',a.core,('video',)),
 ('V4',VIDEO,'unsigned bit = (byte_address & 3u) * 8u + (pixel & 3u) * 2u;','unsigned bit = (pixel & 3u) * 2u;',a.core,('video',)),
 ('V6a',VIDEO,'if (offset == 128u) { offset = 0; quad = next_quad(quad, &transfers); }','if (offset == 128u) { offset = 0; quad += 16u; }',a.core,('video',)),
 ('V9',VIDEO,'bool full = out && origin >= 0 && (unsigned)origin + width <= ARC_RPC_VIDEO_LIMIT;','bool full = out && origin >= 0;',a.core,('video',)),
 ('C05',STORE,'if(valid && slot<2 && (get((uint8_t *)bytes+24,8)&1)!=slot)valid=false;','',a.store,('store',)),
 ('C07',STORE,'put(p+16,k->model+1,4);','put(p+16,1,4);',a.store,('store',)),
 ('C10',STORE,'}else for(unsigned i=344;i<508;++i)if(p[i])return false;','}',a.store,('store',)),
 ('C11',STORE,'if(!valid)return fail(error,n,"candidate readback verification failed");','',a.store,('store',)),
 ('C14',STORE,'|| e->epoch>UINT64_C(13569465599) ','',a.store,('store',)),
 ('C01',STORE,'|| !arc_riscpc_cmos_checksum(p))return false;',')return false;',a.store,('store',)),
]
def check(binary,case):
    if case[0]=='timing':return [a.timing,binary,str(case[1]),case[2]]
    if case[0]=='unit':return [binary,'--hot-video']
    if case[0]=='store':return [binary]
    return [sys.executable,'-B',ROOT/'tests/rpc_video_trace_test.py',a.video,binary,'2',ROOT/'tests/fixtures/rpc-video-paths-2.trace','--paths']

selected=[m for m in mutants if not a.only or m[0] in a.only.split(',')];assert selected
for byte in range(16):
    for remaining in range(1,16385):
        n=min(4-byte%4,remaining)
        assert n==min(n,16-byte)
print('V6: EQUIVALENT in all 262144 aligned-quad byte-boundary cases; Fable ruling',flush=True)
# Prove each corresponding unmutated check passes first. A broken baseline,
# compilation/link failure or timeout is never counted as a mutation kill.
seen=set()
for label,relative,old,new,target,case in selected:
    key=(target,case)
    if key not in seen:must_pass(check(target,case));seen.add(key)
with tempfile.TemporaryDirectory(prefix='arc-rpc-qa-mutants-') as tmp:
    directory=Path(tmp)
    for label,relative,old,new,target,case in selected:
        binary=mutated_binary(target,relative,old,new,directory,label)
        result=run(check(binary,case))
        assert result.returncode!=0,(label,'SURVIVED',result.stdout+result.stderr)
        # Report the assertion/duration/golden/ASan diagnostic, keeping no CMOS bytes.
        lines=(result.stderr+result.stdout).splitlines()
        # Prefer the failure itself or a changed golden row over unchanged
        # context lines: cursor/tail mutants can preserve the first field.
        evidence=next((line for line in lines if any(s in line for s in ('AddressSanitizer','actual=','Assertion','raster guest exited'))),
                      next((line for line in lines if line.startswith(('+','-')) and 'hash=' in line),
                           lines[0] if lines else f'exit {result.returncode}'))
        print(f'{label}: KILLED by {case}: {evidence}',flush=True)
print(f'{len(selected)} QA mutants compiled and killed by passing-baseline checks')
