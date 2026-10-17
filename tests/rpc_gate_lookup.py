"""Encode the actual gate lookup assembly for authored-guest tests. GPL-2.0-only."""
import re,struct
CONDITIONS={'':14,'EQ':0,'NE':1,'CC':3,'HI':8,'VS':6}
def number(text):return int(text.replace('&','0x'),0)
def immediate(value):
    for rotate in range(16):
        shifted=((value<<(2*rotate))|(value>>(32-2*rotate if rotate else 32)))&0xffffffff
        if shifted<256:return rotate<<8|shifted
    raise AssertionError(('unencodable immediate',value))
def registers(text):
    mask=0
    for part in text.split(','):
        pair=part.split('-');a=int(pair[0][1:]);b=int(pair[-1][1:])
        for reg in range(a,b+1):mask|=1<<reg
    return mask

def assemble(source):
    text=source.split('1130 [OPT pass%\n',1)[1].split('1920 ]',1)[0]
    labels={};ops=[];at=0
    for line in text.splitlines():
        line=line.split(' ',1)[1]
        if line.startswith('.'):
            labels[line[1:]]=at;continue
        if line.startswith('EQUS '):
            assert line=='EQUS "ADFS::1.$.ArcHelper"+CHR$(0)'
            value=b'ADFS::1.$.ArcHelper\0';ops.append((at,line,value));at+=len(value)
        else:ops.append((at,line,None));at+=4
    assert (at+3)&~3<=512
    words=bytearray((at+3)&~3);swis={}
    for at,line,data in ops:
        if data is not None:words[at:at+len(data)]=data;continue
        if line.startswith('EQUD '):word=number(line[5:])
        elif line.startswith('SWI '):
            word=0xef000000|number(line[4:]);swis[at]=word&0xffffff
        elif line.startswith('ADR '):
            reg,label=line[4:].split(',');distance=labels[label]-at-8
            assert distance>=0
            word=0xe28f0000|int(reg[1:])<<12|immediate(distance)
        elif match:=re.fullmatch(r'B(EQ|NE|CC|HI|VS)? (\w+)',line):
            condition,label=match.groups();word=CONDITIONS[condition or '']<<28|0x0a000000|((labels[label]-at-8)//4&0xffffff)
        elif match:=re.fullmatch(r'(STMFD|LDMFD) R13!,\{([^}]+)\}',line):
            op,regs=match.groups();word=(0xe92d0000 if op=='STMFD' else 0xe8bd0000)|registers(regs)
        elif match:=re.fullmatch(r'STR R(\d+),\[R(\d+)(?:,#(\d+))?\]',line):
            reg,base,offset=match.groups();word=0xe5800000|int(base)<<16|int(reg)<<12|int(offset or 0)
        else:
            match=re.fullmatch(r'(MOV|CMP|TST|SUB)(EQ|NE|VS)?(S)? ([^,]+),(.+)',line)
            assert match,('unsupported source instruction',line)
            op,condition,sets,dest,operand=match.groups();condition=CONDITIONS[condition or '']
            reg=lambda r:15 if r=='PC' else int(r[1:])
            word=condition<<28
            if op=='SUB':
                left,operand=operand.split(',');word|=2<<21|reg(left)<<16|reg(dest)<<12
            elif op in ('CMP','TST'):word|=({'CMP':10,'TST':8}[op]<<21)|1<<20|reg(dest)<<16
            else:word|=13<<21|reg(dest)<<12|(bool(sets)<<20)
            if operand.startswith('#'):word|=1<<25|immediate(number(operand[1:]))
            else:word|=reg(operand)
        struct.pack_into('<I',words,at,word)
    return words,labels,swis
