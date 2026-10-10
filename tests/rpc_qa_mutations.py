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
for key in ('build','core','timing','unit','video','store','fdc','hd','ide'):p.add_argument('--'+key,required=True)
p.add_argument('--only',default='');a=p.parse_args();build=Path(a.build).resolve()
for key in ('core','timing','unit','video','store','fdc','hd','ide'):setattr(a,key,str(Path(getattr(a,key)).resolve()))
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
# R2b independently asserted controller/drive mutations, linked into the real
# FDC component executable (same scheduler, FIFO and neutral media service).
FDC='engine/rpcemu/src/fdc.c';DRIVE='engine/rpc/rpc_floppy.c'
mutants += [
 ('F-CHRN',FDC,'++fdc.r;return false;','fdc.r=fdc.p[3]+1;return false;',a.fdc,('fdc',)),
 ('F-SIDE',FDC,'r.head=fdc.head;','r.head=fdc.h;',a.fdc,('fdc',)),
 ('F-HEAD',FDC,'r.cylinder=r.drive<2?media.drives[r.drive].cylinder:0;','r.cylinder=fdc.pcn[fdc.unit];',a.fdc,('fdc',)),
 ('F-TC',DRIVE,'if(s->jfd && consumed!=s->bytes)','if(s->jfd && (tc || consumed!=s->bytes))',a.fdc,('fdc',)),
 ('F-MT',FDC,'if(fdc.head==1)return false;','if(false)return false;',a.fdc,('fdc',)),
 ('F-SELECT',FDC,'fdc.physical_drive=fdc.dor&3;','fdc.physical_drive=fdc.unit;',a.fdc,('fdc',)),
 ('F-CHANGE',DRIVE,'if(d->present)d->changed=false;','if(false)d->changed=false;',a.fdc,('fdc',)),
 ('F-GENERATION',DRIVE,'g.mount==f->drives[drive].mount_generation','true',a.fdc,('fdc',)),
 ('F-FORMAT-ORDER',DRIVE,'r>=d->sectors || ids[i*4+3]!=n','r!=i || r>=d->sectors || ids[i*4+3]!=n',a.fdc,('fdc',)),
 # QA LOW-5 recipes, re-reviewed against the typed boundary. Q22 is
 # reversed under D-144: the mutant restores the incorrect 80-pulse limit.
 ('Q27',FDC,'if((fdc.dor&3)==d){iomd.irqa.status','if(true){iomd.irqa.status',a.fdc,('fdc',)),
 ('Q31',FDC,'case 0x3f7:return arc_rpc_floppy_signals(&media,fdc.dor&3,','case 0x3f7:return arc_rpc_floppy_signals(&media,0,',a.fdc,('fdc',)),
 ('Q15',FDC,'unsigned trigger=capacity==1?1:(16-threshold?16-threshold:1);','unsigned trigger=capacity==1?1:threshold;',a.fdc,('fdc',)),
 ('Q16',FDC,'(fdc.service||fdc.fifo_count<=threshold))desired=true;','(fdc.service||fdc.fifo_count<threshold))desired=true;',a.fdc,('fdc',)),
 ('Q18',FDC,'if(addr==0x302a000){fdc.tc=true;fdc.fifo_count=0;}','if(addr==0x302a000){fdc.tc=true;}',a.fdc,('fdc',)),
 ('Q20',FDC,'bool count_end=fdc.verify && (fdc.p[0]&0x80) && !--fdc.verify_count;','bool count_end=fdc.verify && !--fdc.verify_count;',a.fdc,('fdc',)),
 ('Q07',DRIVE,'if(write&&r->deleted)return ARC_RPC_FLOPPY_WRITE_PROTECT;','',a.fdc,('fdc',)),
 ('Q08',DRIVE,'if(!any && r->n!=out->n)return ARC_RPC_FLOPPY_NOT_FOUND;','',a.fdc,('fdc',)),
 ('Q24',FDC,'fdc.config=lock?(config&0x2f):0x20','fdc.config=lock?(config&0x0f):0x20',a.fdc,('fdc',)),
 ('Q22',FDC,'>79?79:media.drives[drive].cylinder','>80?80:media.drives[drive].cylinder',a.fdc,('fdc',)),
 ('Q23',FDC,'fdc.pending_st0[unit]|=0x10;',';',a.fdc,('fdc',)),
 ('Q26',FDC,'|(signals.write_protected?0x40:0)','',a.fdc,('fdc',)),
 ('Q03',DRIVE,'if(!ok){d->writable=false;refused','if(!ok){refused',a.fdc,('fdc',)),
 ('Q10',DRIVE,'d->writable=false;return ARC_RPC_FLOPPY_WRITE_PROTECT;}','return ARC_RPC_FLOPPY_WRITE_PROTECT;}',a.fdc,('fdc',)),
 ('Q29',FDC,'delay=load+2*ARC_RPC_FLOPPY_REV_NS-phase;','delay=load+ARC_RPC_FLOPPY_REV_NS-phase;',a.fdc,('fdc',)),
 ('Q40',FDC,'if(fdc.physical_drive<2 && !media.drives[fdc.physical_drive].loaded)load=head_load();','',a.fdc,('fdc',)),
 ('Q13',DRIVE,'d->present&&d->motor&&arc_rpc_floppy_phase(f,drive,now)<UINT64_C(2000000)','d->present&&arc_rpc_floppy_phase(f,drive,now)<UINT64_C(2000000)',a.fdc,('fdc',)),
 ('F-SEEK-MOUNT',FDC,'else if(i>=SEEK0&&i<=SEEK3){unsigned unit=i-SEEK0,drive=fdc.seek_drive[unit];','else if(i>=SEEK0&&i<=SEEK3){unsigned unit=i-SEEK0,drive=fdc.seek_drive[unit];if(drive<2&&fdc.token[i].mount!=media.drives[drive].mount_generation)continue;',a.fdc,('fdc',)),
 ('F-CANCEL-IRQ',FDC,'irq(fdc.pending!=0);','irq(false);',a.fdc,('fdc',)),
 ('F-DSR-RESET',FDC,'fdc.config=0x20;fdc.rate=2;','fdc.config=0x20;fdc.rate=0;',a.fdc,('fdc',)),
 ('F-RECAL-EC',FDC,'fdc.base==7&&(drive>=2||media.drives[drive].cylinder>79)','fdc.base==7&&(drive>=2||media.drives[drive].cylinder>80)',a.fdc,('fdc',)),
]
# Q33 is equivalent: software reset clears POLL (mask 0x2f, even when
# locked), hardware reset clears it too, and FIFO commands cannot execute
# during the RESET window. Its callback therefore always sees POLL=0.
# R2c store and IDE boundary mutations use independently asserted component
# checks; original build objects remain untouched.
HD='libretro/rpc_hd_store.c';IDE='engine/rpcemu/src/ide.c';SCHEDULER='engine/rpc/rpc_scheduler.c'
mutants += [
 ('HD-SEEK',HD,'if(arc_io_seek_file(f,at,RETRO_VFS_SEEK_POSITION_START)<0)return false;',
  'if(arc_io_seek_file(f,at,RETRO_VFS_SEEK_POSITION_START)!=at)return false;',a.hd,('hd','vfs')),
 ('HD-TELL',HD,'return arc_io_tell_file(f)==at;','return arc_io_tell_file(f)>=0;',a.hd,('hd','vfs')),
 ('HD-DIAGNOSTIC',HD,'if(previous_ok)s->io_operation="close";','s->io_operation="close";',a.hd,('hd','vfs')),
 ('HD-ORDER',HD,'uint32_t tail=s->lengths[s->current];bool ok=seek(s,f,tail)&&',
  'uint32_t tail=s->lengths[s->current];for(unsigned i=0;i<8;++i)if(s->tags[i].valid){unsigned first=s->tags[i].index*512;unsigned lo=lba>first?lba:first,hi=lba+count<first+512?lba+count:first+512;if(lo<hi)copy(s,s->cache[i]+(lo-first)*512,in+(lo-lba)*512,(hi-lo)*512);}bool ok=seek(s,f,tail)&&',a.hd,('hd','command')),
 ('HD-BIND',HD,'return !memcmp(digest,h+120,32);','return true;',a.hd,('hd','binding')),
 ('HD-GC',HD,'if(get(e,4)!=1||(get(live,4)==1&&get(live+8,8)==get(e+8,8)))continue;',
  'if(get(e,4)!=1)continue;',a.hd,('hd',)),
 ('HD-HOLE',HD,'if(!f)return failure(s,damage);\n    uint8_t h[128]',
  'if(!f){memset(s->scratch,0,ARC_RPC_HD_CHUNK);return true;}\n    uint8_t h[128]',a.hd,('hd',)),
 ('HD-CAP',HD,'aggregate+bytes>ARC_RPC_HD_JOURNAL_CAP||','',a.hd,('hd','command')),
 ('IDE-LATCH',IDE,'!ide.backend.write(ide.backend.opaque,ide.lba,ide.total,ide.transaction)',
  '(!ide.backend.write(ide.backend.opaque,ide.lba,ide.total,ide.transaction)||!ide.backend.write(ide.backend.opaque,ide.lba,ide.total,ide.transaction))',a.ide,('ide',)),
 ('IDE-NIEN',IDE,'master()&&ide.irq&&!(ide.control&2)','master()&&ide.irq',a.ide,('ide',)),
 ('IDE-TIE',SCHEDULER,'s->deadlines[i] < s->next_ns','s->deadlines[i] <= s->next_ns',a.ide,('ide',)),
]
mutants += [
 ('Q04-CRC-ONLY', 'libretro/rpc_hd_store.c', 'ok=get(tail,4)==crc(s,s->scratch,ARC_RPC_HD_CHUNK)&&!memcmp(digest,e+16,32);', 'ok=get(tail,4)==crc(s,s->scratch,ARC_RPC_HD_CHUNK);', a.hd, ('hd','qa-integrity')),
 ('Q05-SEQ-GAP', 'libretro/rpc_hd_store.c', 'sequence==*last+1&&', 'sequence>*last&&', a.hd, ('hd','qa-integrity')),
 ('Q11-PREFIX-DIGEST', 'libretro/rpc_hd_store.c', 'arc_sha256_final(&s->digest[parent],digest);if(memcmp(digest,h+160,32))return false;', '', a.hd, ('hd','qa-integrity')),
 ('Q12-CHUNK-GEN', 'libretro/rpc_hd_store.c', '||\n           (get(e,4)==1&&get(e+8,8)>get(m+88,8)))return false;', ')return false;', a.hd, ('hd','qa-integrity')),
 ('Q16-DEBRIS-PROBE', 'libretro/rpc_hd_store.c', 'char name[23];chunk_leaf(i,candidate,name);\n        if(size(s,name,&n)){if(!remove_leaf(s,name))return failure(s,false);untrack_chunk(s,name);}', 'char name[23];chunk_leaf(i,candidate,name);(void)name;', a.hd, ('hd','qa-integrity')),
 ('I08-RESET-GATE', 'engine/rpcemu/src/ide.c', 'if(!master()||(ide.control&4))return;', 'if(!master())return;', a.ide, ('ide',)),
 ('I09-WRITE-IRQ', 'engine/rpcemu/src/ide.c', 'ide.active=ide.writing=false;ide.status=READY;\n            }else{ide.position=0;ide.status=READY|DRQ;}', 'ide.active=ide.writing=false;ide.status=READY;return;\n            }else{ide.position=0;ide.status=READY|DRQ;}', a.ide, ('ide',)),
 ('I10-CHS-SECTOR0', 'engine/rpcemu/src/ide.c', 'if(!ide.sector||ide.sector>ide.spt||', 'if(ide.sector>ide.spt||', a.ide, ('ide',)),
]
# Q19 is equivalent for representable media: raw has cylinders 0..79;
# JFD has <=166 track sides. A physical C=255 is rejected before an ID
# can be selected. Every WRONG_CYLINDER sector C is therefore !=255.
# Q04/Q30 are the equivalents already accepted by Fable.
def check(binary,case):
    if case[0]=='timing':return [a.timing,binary,str(case[1]),case[2]]
    if case[0]=='unit':return [binary,'--hot-video']
    if case[0] in ('store','fdc','hd','ide'):return [binary,*case[1:]]
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
