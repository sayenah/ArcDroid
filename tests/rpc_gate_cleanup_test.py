#!/usr/bin/env python3
"""Execute native lookup/error cleanup using authored SWI substitutes. GPL-2.0-only."""
from pathlib import Path
import struct,subprocess,sys,tempfile
from rpc_video_rom import Guest
from rpc_gate_lookup import assemble
runner,core,gate=sys.argv[1:4];source=Path(gate).read_text()
# BASIC cannot own the claim/handle across an interpreter boundary. In
# particular the error handler stays armed until native cleanup has returned.
lines={int(line.split(' ',1)[0]):line.split(' ',1)[1] for line in source.splitlines()}
assert lines[300]=='CALL lookup%'
assert not any('XOS_Claim' in value or 'XOS_Find' in value or 'XOS_Release' in value for value in lines.values())
assert lines[705]=='IF workspace%<>0 THEN !workspace%=0'
assert lines[710]=='ON ERROR GOTO 700'
assert lines[715]=='SYS "XOS_Byte",124'
assert all(part in source for part in ('1630 MOV R0,#0\n1640 STR R0,[R5]','1690 SWI &20020','1780 SWI &2000D','1760 MOV R8,#0'))
assert source.index('1630 MOV R0,#0\n1640 STR R0,[R5]')<source.index('1690 SWI &20020')<source.index('1780 SWI &2000D')
assert source.index('1760 MOV R8,#0')<source.index('1780 SWI &2000D')
code,labels,swis=assemble(source)
BASE=0x10021000;WS=0x10023000;ACK=0x10020000;IMAGE=0x10024000;STATE=ACK+20
cases=['ok','claim','open-error','open-zero','args','small','large','unaligned','read-error','short-read','release','close']
with tempfile.TemporaryDirectory(prefix='arc-gate-cleanup-') as tmp:
    tmp=Path(tmp);(tmp/'seed').write_bytes(bytes(256))
    for model in ('arm610','sa110'):
        for case in cases:
            g=Guest();g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000,0xe161f000)
            body=bytearray(code);hooks={};native=Guest()
            for offset,swi in swis.items():
                start=0x10025000+len(native.w)*4;hooks[offset]=start
                # State: claim, open, violation. Preserve the native routine's
                # working registers; return only SWI results and V.
                native.emit(0xe92d4010);native.literal(4,STATE)
                def check(reg,value):
                    native.literal(12,value);native.emit(0xe150000c|reg<<16,0x13a0c001,0x1584c008)
                error=False
                if swi==0x2001f:
                    check(0,0x1d);check(1,WS-236);check(2,WS)
                    native.emit(0xe3a0c000 if case=='claim' else 0xe3a0c001,0xe584c000);error=case=='claim'
                elif swi==0x2000d and offset<labels['cleanup']:
                    native.literal(12,WS);native.emit(0xe59cc000,0xe35c0001,0x13a0c001,0x1584c008)
                    check(0,0x4f);check(1,BASE+labels['helperpath'])
                    opened=case not in ('open-error','open-zero')
                    native.emit(0xe3a00001 if opened else 0xe3a00000,0xe5840004);error=case=='open-error'
                elif swi==0x20009:
                    check(0,2);check(1,1)
                    native.literal(2,{'small':64,'large':8196,'unaligned':145}.get(case,144));error=case=='args'
                elif swi==0x2000c:
                    for reg,value in ((0,4),(1,1),(2,IMAGE),(3,144)):check(reg,value)
                    native.emit(0xe59dc000,0xe35c0000,0x13a0c001,0x1584c008)
                    native.emit(0xe3a03004 if case=='short-read' else 0xe3a03000);error=case=='read-error'
                elif swi==0x20020:
                    native.emit(0xe3a0c001,0xe504c008)  # Release attempt, including failed Claim.
                    native.literal(12,WS);native.emit(0xe59cc000,0xe35c0000,0x13a0c001,0x1584c008)
                    for reg,value in ((0,0x1d),(1,WS-236),(2,WS)):check(reg,value)
                    if case!='release':native.emit(0xe3a0c000,0xe584c000)
                    error=case=='release'
                else:
                    assert swi==0x2000d
                    native.literal(12,WS);native.emit(0xe59cc000,0xe35c0000,0x13a0c001,0x1584c008)
                    check(0,0);check(1,1);native.emit(0xe3a0c000,0xe584c004);error=case=='close'
                native.emit(0xe8bd4010)
                if error:native.literal(12,0x80000000);native.emit(0xe25cc001)
                else:native.emit(0xe15c000c)
                native.emit(0xe1a0f00e)
                struct.pack_into('<I',body,offset,0xeb000000|((start-(BASE+offset)-8)//4&0xffffff))
            for offset in range(0,len(body),4):g.write(BASE+offset,struct.unpack_from('<I',body,offset)[0])
            for offset,word in enumerate(native.w):g.write(0x10025000+offset*4,word)
            for address in (WS,ACK,ACK+4,ACK+8,ACK+12,ACK+16,STATE,STATE+4,STATE+8):g.write(address,0)
            g.literal(13,0x10028000);g.literal(0,IMAGE);g.literal(1,WS);g.literal(2,ACK)
            g.literal(4,BASE);g.literal(14,0);link=len(g.w)-1;g.emit(0xe1a0f004);g.w[link]=len(g.w)*4
            g.literal(0,WS);g.emit(0xe5900000);g.literal(1,ACK+16);g.emit(0xe5810000,0xeafffffe)
            rom=bytearray(struct.pack('<I',0xeafffffe)*524288);rom[:len(g.w)*4]=struct.pack('<'+'I'*len(g.w),*g.w)
            (tmp/'kernel.rom').write_bytes(rom)
            (tmp/'case.arcplan').write_text(f'version=1\nmodel={model}\nfirmware={tmp/"kernel.rom"}\ncmos=seed\nseed={tmp/"seed"}\nrtc_epoch=0\n')
            result=subprocess.run([runner,'--core',core,'--plan',str(tmp/'case.arcplan'),'--system',str(tmp),'--save',str(tmp),'--frames','1','--ram-offset','131072','--ram-words','8'],capture_output=True,text=True,timeout=15)
            read_size=0 if case in ('claim','open-error','open-zero','args') else {'small':64,'large':8196,'unaligned':145}.get(case,144)
            expected=[read_size,int(case=='ok'),int(case=='release'),1,0,int(case=='release'),0,0]
            wanted='ram='+','.join(f'{word:08x}' for word in expected)
            assert result.returncode==0 and wanted in result.stdout,(model,case,wanted,result.stdout,result.stderr)
print('Native gate: BASIC ordering, source-assembled cleanup, all X-SWI failures and bounded lookup pass')
