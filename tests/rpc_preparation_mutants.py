#!/usr/bin/env python3
"""R3b-2a classification, reconciliation and byte-budget mutants. GPL-2.0-only."""
from pathlib import Path
import re,subprocess,sys,tempfile
compiler,core,root=sys.argv[1:4];root=Path(root).resolve()
mutants=[
 ('receipt-future','hd','get(r+8,4)!=1','false'),
 ('populator-future','hd','get(r+12,4)!=1','false'),
 ('authored-future','hd','get(r+16,4)!=1','false'),
 ('envelope-future','hd','if((revision!=1&&revision!=2)||n!=manifest_length(m))return ARC_RPC_HD_UNSUPPORTED;','if(revision!=1&&revision!=2)return ARC_RPC_HD_CORRUPT;'),
 ('crc-before-revision','hd','if(n<ENVELOPE_MIN||n!=get(m+12,4)||memcmp(m+16,s->content,40)||get(m+n-4,4)!=crc(s,m,n-4))','if(n!=get(m+12,4)||memcmp(m+16,s->content,40))'),
 ('io-is-unavailable','hd','if(!read_leaf(s,leaf,m,n))return open_failure(s,ARC_RPC_HD_UNAVAILABLE);','if(!read_leaf(s,leaf,m,n))return open_failure(s,ARC_RPC_HD_CORRUPT);'),
 ('reconstruction-budget','hd','if(transferred+bytes>526336)break;',''),
 ('reconstruction-close-io','hd','if(!ok){close_file(s,f,false);return failure(s,false);}', 'if(!ok){return failure(s,false);}'),
 ('reconstruction-close-integrity','hd','if(!ok){close_file(s,f,false);return failure(s,true);}for(unsigned i=lo;i<hi;++i)', 'if(!ok){return failure(s,true);}for(unsigned i=lo;i<hi;++i)'),
 ('future-length-damaged','hd','||n!=manifest_length(m)', ''),
 ('journal-future-length','hd','header_bytes!=256||', ''),
 ('journal-auth-crc','hd','if(get(h+header_bytes-4,4)!=crc(s,h,header_bytes-4))', 'if(false)'),
 ('torn-open-unavailable','hd','if(!s->rotation_writer)return recovered_read_only(s);','if(!s->rotation_writer)return open_failure(s,ARC_RPC_HD_UNAVAILABLE);'),
 ('listing-cap-unavailable','cmos','if(r<0)', 'if(r<0||s->slot>=4095)'),
 ('listing-no-retry','cmos','if(++s->attempts>=3)s->exhausted=true;','s->exhausted=true;'),
 ('listing-unbounded-retry','cmos','if(++s->attempts>=3)s->exhausted=true;','if(++s->attempts>=4)s->exhausted=true;'),
 ('inspection-all-slots','cmos','if(++s->slot<2)s->phase=4;','if(++s->slot<1)s->phase=4;'),
 ('seed-reconciliation','cmos','if(!s->reconciled||s->present||s->refused||s->publication_pending)','if(s->present||s->refused||s->publication_pending)'),
]
with tempfile.TemporaryDirectory(prefix='arc-r3b2a-mutants-') as tmp:
    tmp=Path(tmp)
    for kind in ('hd','cmos'):
        name='rpc_hd_store' if kind=='hd' else 'rpc_cmos_store'
        original=(root/f'libretro/{name}.c').read_text()
        cases=[('baseline',None,None)]+[(n,o,v) for n,k,o,v in mutants if k==kind]
        for label,old,new in cases:
            text=original
            if old is not None:
                pattern=r'\s*'.join(re.escape(c) for c in old if not c.isspace())
                matches=list(re.finditer(pattern,text))
                if label.startswith('reconstruction-close-'):
                    start=text.index('static bool reconstruction_step(')
                    matches=[m for m in matches if m.start()>=start]
                assert len(matches)==1,(label,'anchor changed')
                match=matches[0];text=text[:match.start()]+new+text[match.end():]
            source=tmp/(name+'.c');source.write_text(text);binary=tmp/'test'
            test='rpc_preparation_test' if kind=='hd' else 'rpc_cmos_store_test'
            extra=[str(root/'libretro/install_store.c'),core] if kind=='hd' else [str(root/'core/hash.c')]
            cmd=[compiler,'-std=c11','-O2','-UNDEBUG',*[f'-I{root/p}' for p in ('core','engine','libretro','libretro/include','tests')],str(root/f'tests/{test}.c'),str(source),str(root/'libretro/arc_io.c'),*extra,'-o',str(binary)]
            compiled=subprocess.run(cmd,capture_output=True,text=True,timeout=90)
            assert compiled.returncode==0,(label,compiled.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
            if label=='baseline':assert run.returncode==0,run.stderr
            else:assert run.returncode!=0,(label,'survived');print('R3b-2a mutant killed: '+label,flush=True)
