#!/usr/bin/env python3
"""Writer mutation checks using isolated source copies, never private evidence.

GPL-2.0-only. Links the current core/zlib archives; only the small population
modules are recompiled. The working tree is never mutated.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

compiler, core, zlib, root_arg = sys.argv[1:5]
root=Path(root_arg)
sanitized='--sanitized' in sys.argv[5:]
modules=['libretro/rpc_boot.c','libretro/rpc_boot_zip.c','core/rpc_boot_filecore.c','libretro/rpc_hd_store.c']
mutants=[
 ('merge-bytes',modules[0],'memcmp(a->hash,z->hash,32)','0'),
 ('merge-attributes',modules[0],'a->attr!=z->attr','false'),
 ('merge-type',modules[0],'((a->load>>8)&4095)==((z->load>>8)&4095)','true'),
 ('merge-date',modules[0],'newer>old','false'),
 ('exclude-500',modules[0],'return value>=500','return value>=501'),
 ('exclude-neighbour',modules[0],'numeric(parts[3],parts[3]+strlen(parts[3]))','strtoul(parts[3],NULL,10)>=500'),
 ('inactive-startup',modules[0],'strcpy(parts[1],"Inactive")','strcpy(parts[1],"Active")'),
 ('arc0-validation',modules[1],'memcmp(cm,lm,12)||memcmp(z->central_digest,z->local_digest,32)','false'),
 ('snapshot-pinning',modules[1],'memcmp(digest,z->pins[index],32)','false'),
 ('crc-validation',modules[1],'z->crc!=a->crc','false'),
 ('deflate-sector-buffer',modules[0],'done ? b->pending : b->pending & ~511u','b->pending'),
 ('sharing-offset',modules[2],'(used[bin]+1)','used[bin]'),
 ('sharing-id',modules[2],'zone*254+l->serial[zone]++','zone*254+3'),
 ('zone-resolution',modules[2],'zone=id==2?2:id/254','zone=0'),
 ('fragment-minimum',modules[2],'if(take<16)take=16;',''),
 ('boot-option',modules[2],'out[455]=2','out[455]=0'),
 ('boot-option-exec',modules[2],'out[455]=2','out[455]=3'),
 ('attribute-lock',modules[2],'((a->attr & 8u) >> 1)','0'),
 ('receipt-persist',modules[3],'size_t n=manifest_length(m);unsigned version=n==MANIFEST_BYTES?2:1;', 'size_t n=MANIFEST_V1_BYTES;unsigned version=1;'),
 ('receipt-unknown',modules[3],'get(r+8,4)!=1','false'),
 ('receipt-forgery',modules[3],'get(s->manifests[s->selected]+8,4)!=2','false'),
 ('receipt-noop',modules[0],'b->phase=ARC_RPC_BOOT_DONE;\n            memcpy(b->stats.packages','b->phase=ARC_RPC_BOOT_OPEN;\n            memcpy(b->stats.packages'),
]
original={p:(root/p).read_text() for p in modules}
with tempfile.TemporaryDirectory(prefix='arcdroid-boot-mutants-') as temp:
    temp=Path(temp);binary=temp/('mutant.exe' if os.name=='nt' else 'mutant')
    def compile_sources(changed=None):
        paths=[]
        for name,text in original.items():
            path=temp/name;path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text(changed.get(name,text) if changed else text);paths.append(str(path))
        flags=['-O1' if sanitized else '-O2','-std=c11','-UNDEBUG']
        if sanitized:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        includes=sum((['-I',str(root/p)] for p in ('core','engine','libretro','libretro/include','third_party/zlib')),[])
        command=[compiler,*flags,*includes,str(root/'tests/rpc_boot_test.c'),*paths,
                 str(root/'libretro/arc_io.c'),str(root/'libretro/install_store.c'),core,zlib,'-o',str(binary)]
        result=subprocess.run(command,capture_output=True,text=True,timeout=120)
        if result.returncode:raise RuntimeError('invalid mutant compilation: '+result.stderr[-3000:])
    def suite():
        return subprocess.run([sys.executable,'-B',str(root/'tests/rpc_boot_packages_test.py'),str(binary)],
                              capture_output=True,text=True,timeout=600)
    compile_sources();baseline=suite()
    if baseline.returncode:raise RuntimeError('mutation baseline failed: '+baseline.stderr[-3000:])
    for name,path,old,new in mutants:
        pattern=r'\s*'.join(re.escape(c) for c in old if not c.isspace())
        matches=list(re.finditer(pattern,original[path]))
        if len(matches)!=1:raise RuntimeError(f'mutant anchor changed: {name}')
        match=matches[0]
        changed=original[path][:match.start()]+new+original[path][match.end():]
        compile_sources({path:changed})
        result=suite()
        if result.returncode==0:raise RuntimeError(f'writer mutant survived: {name}')
        if 'SyntaxError' in result.stderr or 'ImportError' in result.stderr:raise RuntimeError(f'invalid suite: {name}')
        print(f'writer mutant killed: {name}',flush=True)
