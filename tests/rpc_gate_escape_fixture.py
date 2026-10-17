#!/usr/bin/env python3
"""Author a private Escape-injection gate from public source; GPL-2.0-only.

No firmware is read. Each fixture issues genuine OS_Byte 125 once per gate
invocation, identifies the injection with an ARCS diagnostic (event 4), and
probes for a residual claim (event 7: handler entry count delta, UpCall result).
The counter lives in unused space in the existing RMA allocation. A passive
but still installed guard is observable too. Never ship these fixture bytes.
"""
from pathlib import Path
import argparse
WINDOWS=('before-claim','claimed','active','opened','sized','read','cleanup',
         'deactivated','released','closed','native-return','call','returned',
         'basic-cleanup','acknowledge','acknowledged')
def fixture(source,window):
    identifier=WINDOWS.index(window)+1
    source=source.replace('sequence%=0','sequence%=0:injected%=0')
    source=source.replace('48 !guard%=', '48 guard%!248=0:!guard%=')
    source=source.replace('84 LDR R0,[R12]',
        '83 LDR R0,[R12,#-4]:ADD R0,R0,#1:STR R0,[R12,#-4]\n84 LDR R0,[R12]')
    source=source.replace('290 A%=image%',
        f'288 diag%!32=4:diag%!36=2:diag%!40={identifier}:diag%!44=125:guard%!244=diag%\n290 A%=image%')
    source=source.replace('954 PROCdiag(2,now%,releasebad%)',
        '954 PROCdiag(2,now%,releasebad%)\n955 before%=guard%!248:SYS &20033,256 TO answer%:PROCdiag(7,guard%!248-before%,answer%)')
    if identifier<=11:
        anchor={'before-claim':'1230 MOV R0,#&1D','native-return':'1850 LDMFD R13!,{R4-R12,R14}'}.get(window)
        instructions='STMFD R13!,{R0-R7,R14}:LDR R3,[R5,#-8]:MOV R0,#0:MOV R1,#1:MOV R2,#0:LDR R4,[R3]:MOV R5,#64:MOV R6,#0:SWI &20006:MOV R0,#125:SWI &20006:LDMFD R13!,{R0-R7,R14}'
        if anchor:
            source=source.replace(anchor,str(int(anchor.split()[0])-1)+' '+instructions+'\n'+anchor)
        else:
            label='1590 .read' if window=='read' else next(line for line in source.splitlines() if line.endswith(' .'+window))
            source=source.replace(label,label+'\n'+str(int(label.split()[0])+1)+' '+instructions)
    else:
        anchor={'call':'300 CALL lookup%','returned':'310 bytes%=!ack%:valid%=ack%!4:releasebad%=ack%!8',
                'basic-cleanup':'705 IF workspace%<>0 THEN !workspace%=0',
                'acknowledge':'715 SYS "XOS_Byte",124','acknowledged':'735 IF guard%<>0 AND published%=0 THEN oldguard%=guard%:guard%=0:workspace%=0:SYS "XOS_Module",7,,oldguard%'}[window]
        source=source.replace(anchor,str(int(anchor.split()[0])-1)+f' IF injected%=0 THEN injected%=1:PROCdiag(4,{identifier},125):SYS "XOS_Byte",125\n'+anchor)
    return source
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path);parser.add_argument('window',choices=WINDOWS)
    args=parser.parse_args();args.output.write_text(fixture(args.source.read_text(),args.window))
