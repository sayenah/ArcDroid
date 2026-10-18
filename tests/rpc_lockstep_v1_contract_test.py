#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Independent v1 contract witnesses and QA's surviving comparator mutants."""
import copy
import io
from pathlib import Path
import struct
import sys
import types
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import rpc_lockstep_compare as reference
import rpc_lockstep_stream as w


def check(ok, message):
    if not ok:raise AssertionError(message)


def checks(c=reference):
    schema,sh=c.load_schema();v=w.defaults(schema)
    def key(k=0,p=0,site=0):return (1,1,k,p,site,0)
    def cp(k=0,p=0,site=0,value=None,edge=0,compact=False):
        return w.checkpoint(key(k,p,site),schema,v if value is None else value,edge=edge,compact=compact)
    def reject(records,reason,wire=None):
        reader=c.Reader(io.BytesIO(w.stream(sh,records) if wire is None else wire),schema,sh,'strict')
        try:
            while reader.next() is not None:pass
        except c.Invalid as error:check(reason in str(error),'wrong rejection: '+str(error))
        else:raise AssertionError('accepted '+reason)
    def compare(a,b,mode='strict',**kw):
        return c.compare(io.BytesIO(w.stream(sh,a,True,mode=c.MODES.index(mode))),
            io.BytesIO(w.stream(sh,b,False,mode=c.MODES.index(mode),**kw)),schema,sh,mode,True)
    # Two exits close the spurious access opened by the mutant. Candidate
    # duplicate ordering is legal, so no other validation masks this witness.
    exit=cp(p=1,site=2,edge=2)
    reject([exit,exit],'data access exit without entry')
    entry=cp(p=1,site=2,edge=1)
    wrong=w.event(key(p=1,site=3),0)
    reject([entry,wrong,w.fatal(key(p=5,site=2),1,2)],'event outside open data access')
    reject([w.page(key(),boundary=1,allocation=2)],'missing page-boundary marker')
    short=w.frame(5,struct.pack('<7Q',*key(p=5),1))
    reject([short],'fatal reason width')
    check(not compare([w.fatal(key(p=5),1,2)],[w.fatal(key(p=5),1,3)])['ok'],
          'fatal text hash disappeared')
    wrong_end=w.header(sh)+w.end(0,opened=1,closed=1)
    reject([],'online span accounting mismatch',wire=wrong_end)
    baseline=[cp(),cp(p=1,site=4),cp(p=2)]
    check(compare(baseline,baseline,spans=[(1,0,0)])['ok'],'valid compiled span')
    result=compare(baseline,[cp()],spans=[(1,0,0)])
    check(not result['ok'] and result['field']=='density','span close discarded mandatory evidence')
    # A changed common prefix must fail reproduction even if the compact
    # divergence, key and earliest leaf are reproduced exactly.
    changed=copy.deepcopy(v);changed['cp15.ctrl']=7
    prefix=copy.deepcopy(v);prefix['cpu.reg'][0]=42
    def compact(k,value):
        return w.checkpoint(key(k),schema,c.compact_groups(c.Record(1,key(k),value),schema,'strict'),compact=True)
    original=compare([compact(0,v),compact(1,v)],[compact(0,v),compact(1,changed)])
    good=compare([cp(),cp(1)],[cp(),cp(1,value=changed)])
    bad=compare([cp(value=prefix),cp(1)],[cp(value=prefix),cp(1,value=changed)])
    check(c.reproduced(original,good),'valid compact/detail reproduction')
    check(not c.reproduced(original,bad),'reproduction accepted a different matching prefix')
    quota=copy.deepcopy(v);quota['rpc_session.arc_rpc_test_quota']=200
    a=c.Record(1,key(),v);b=c.Record(1,key(),quota)
    check(c.compact_groups(a,schema,'cross-quota')==c.compact_groups(b,schema,'cross-quota'),
          'compact recurrence hashed a mode-excluded field')
    check(c.compact_groups(a,schema,'strict')!=c.compact_groups(b,schema,'strict'),
          'compact recurrence lost compared-field sensitivity')
    # Exercise each P3 encoding, both unmatched and matched with differing
    # payloads. Span close precedes global P3 skipping and needs Reader.optional.
    for mode in ('guest-output','trace-toggle'):
        for record,other in ((cp(p=3),cp(p=3,value=changed)),
                (w.checkpoint(key(p=3),schema,[0]*len(schema['groups']),compact=True),
                 w.checkpoint(key(p=3),schema,[1]*len(schema['groups']),compact=True)),
                (w.page(key(p=3),allocation=0,boundary=0),
                 w.page(key(p=3),allocation=0,index=1,boundary=0))):
            check(compare([cp(),cp(p=2),record],[cp(),cp(p=2),other],mode)['ok'],mode+' P3 payload optional')
            check(compare([cp(),cp(p=2),record],[cp(),cp(p=2)],mode,spans=[(1,0,0)])['ok'],
                  mode+' P3 optional at span close')
            check(compare([cp(),cp(p=2)],[cp(),cp(p=2),record],mode)['ok'],mode+' candidate P3 optional')
        check(not compare([cp()],[cp(value=changed)],mode)['ok'],mode+' compared state insensitive')
    print('v1 QA witnesses: AS-1/3/4/5, fatal width/text, mode exclusions, optional P3 (c)/(d) PASS')


def mutations():
    yield 'exit-without-entry-accepted', [("            require(edge==1, 'data access exit without entry')",'            pass')]
    yield 'event-outside-access-accepted', [("                require(key == self.open_access, 'event outside open data access')",'                pass')]
    yield 'page-marker-unchecked', [("                    require(alloc==0 and page<=1,'missing page-boundary marker')",'                    pass')]
    yield 'reproduce-ignores-prefix', [("    if original.get('prefix')!=rerun.get('prefix'):return False",'    pass')]
    yield 'compact-mode-exclusion-hashed', [("        if mode in f['mode_exclusions']:", '        if False:')]
    yield 'fatal-width-v0', [("                require(len(payload) == 16, 'fatal reason width')\n                value = struct.unpack('<2Q', payload); self.fatal = True", "                require(len(payload) >= 8, 'fatal reason width')\n                value = struct.unpack_from('<Q', payload); self.fatal = True")]
    yield 'span-accounting-unchecked', [("            require(not self.spans and totals[6:] == tuple(self.span_counts), 'online span accounting mismatch')",'            pass')]
    yield 'span-close-density-unchecked', [("                return failure(ar.key,'density',ar.key,None)\n            b.close_span(br)","                ar=a.next();continue\n            b.close_span(br)")]
    reader=("        if self.mode in ('cross-quota','guest-output','trace-toggle') and record.key[3] == 3 and record.kind in (1, 2, 4):", "        if self.mode == 'cross-quota' and record.key[3] == 3 and record.kind in (1, 2, 4):")
    skips=[]
    for var in ('ar','br'):
        old=f"        if mode in ('cross-quota','guest-output','trace-toggle') and {var} and {var}.key[3] == 3 and {var}.kind in (1, 2, 4):"
        skips.append((old,old.replace("mode in ('cross-quota','guest-output','trace-toggle')", "mode == 'cross-quota'")))
    yield 'H4-reader-only-cross-quota',[reader]
    yield 'H4-compare-only-cross-quota',skips
    yield 'H4-both',[reader,*skips]


def main():
    checks()
    source=(ROOT/'tools/rpc_lockstep_compare.py').read_text(encoding='utf-8')
    for label,changes in mutations():
        text=source
        for old,new in changes:
            check(text.count(old)==1,'mutant anchor: '+label);text=text.replace(old,new)
        engine=types.ModuleType('v1_contract_mutant');engine.__file__=str(ROOT/'tools/rpc_lockstep_compare.py')
        sys.modules[engine.__name__]=engine
        exec(compile(text,engine.__file__,'exec'),engine.__dict__)
        try:checks(engine)
        except AssertionError as error:print('KILLED',label,str(error),flush=True)
        else:raise AssertionError('SURVIVED '+label)


if __name__=='__main__':main()
