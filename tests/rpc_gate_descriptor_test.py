#!/usr/bin/env python3
"""Compare actual BASIC admission expressions with the C validator. GPL-2.0-only."""
from pathlib import Path
import ast,re,struct,subprocess,sys,tempfile
exe,gate=sys.argv[1:3];source=Path(gate).read_text()
# Evaluate the refusal expressions themselves, not copies of their constants.
# A new unsupported expression fails closed and requires updating the adapter.
section=source.split('780 IF ',1)[1].split('870 FOR ',1)[0]
expressions=[];bindings={}
for line in ('780 IF '+section).splitlines():
    line=line.split(' ',1)[1]
    if line.startswith('IF '):
        assert line.endswith(' THEN GOTO 950'),line
        expression=line[3:-len(' THEN GOTO 950')]
        expression=re.sub(r'image%!([0-9]+)',r'w\1',expression).replace('!image%','w0')
        expression=re.sub(r'\b(\w+)%',r'\1',expression)
        expression=re.sub(r'&([0-9A-F]+)',r'0x\1',expression)
        expression=expression.replace('<>','!=').replace(' OR ',' or ').replace(' AND ',' & ')
        tree=ast.parse(expression,mode='eval')
        allowed=(ast.Expression,ast.BoolOp,ast.Or,ast.BinOp,ast.BitAnd,ast.Add,ast.Sub,
                 ast.UnaryOp,ast.USub,ast.Compare,ast.NotEq,ast.Lt,ast.Gt,ast.Name,ast.Load,ast.Constant)
        assert all(isinstance(node,allowed) for node in ast.walk(tree)),expression
        expressions.append(compile(tree,'ArcGate descriptor expression','eval'))
    else:
        for assignment in line.split(':'):
            match=re.fullmatch(r'(\w+)%=image%!(\d+)',assignment);assert match,line
            bindings[match[1]]=int(match[2])
assert len(expressions)==8 and set(bindings)=={'entry','plan','plen'}
# Lookup's native size guard is part of admission too. Extract its operands.
minimum=int(re.search(r'CMP R9,#(\d+)\n\d+ BCC cleanup',source)[1])
maximum=int(re.search(r'CMP R9,#(\d+)\n\d+ BHI cleanup',source)[1])
alignment=int(re.search(r'TST R9,#(\d+)\n\d+ BNE cleanup',source)[1])
def basic(packet):
    size=len(packet)
    if size<minimum or size>maximum or size&alignment:return False,0
    signed=lambda value:value if value<0x80000000 else value-0x100000000
    values={'bytes':size,**{f'w{at}':signed(struct.unpack_from('<I',packet,at)[0]) for at in range(0,64,4)}}
    values.update({name:values[f'w{at}'] for name,at in bindings.items()})
    return (False,0) if any(eval(rule,{'__builtins__':{}},values) for rule in expressions) else (True,values['entry'])
packets=[]
for size in (64,68,69,72,144,8192,8196):
    packet=bytearray(size);packet[:8]=b'ARCRHLP1'
    for at,value in enumerate((1,64,size,1,1,64,1,1,1,68,max(0,size-68),0,0,0),2):struct.pack_into('<I',packet,at*4,value)
    packets.append(bytes(packet))
    for at in range(0,64,4):
        for value in (0,1,2,3,4,60,63,64,65,68,72,140,144,8192,8196,0x7ffffffc,0x80000000,0xfffffffc,0xffffffff):
            changed=packet.copy();struct.pack_into('<I',changed,at,value);packets.append(bytes(changed))
# Admission permits opaque code/plan bytes, a zero-length plan and the last
# aligned entry. Exercise these valid edges, not just malformed headers.
for size in (68,72,144,8192):
    for entry in (64,size-4):
        for plan in (entry+4,size):
            for count in (0,size-plan):
                packet=bytearray(size);packet[:8]=b'ARCRHLP1'
                struct.pack_into('<14I',packet,8,1,64,size,1,1,entry,1,1,1,plan,count,0,0,0)
                for body in (bytes(size-64),b'\xff'*(size-64),
                             bytes((i*73+19)&255 for i in range(size-64))):
                    packet[64:]=body
                    packets.append(bytes(packet))
with tempfile.TemporaryDirectory(prefix='arc-gate-descriptor-') as tmp:
    file=Path(tmp)/'packets'
    with file.open('wb') as stream:
        for packet in packets:stream.write(struct.pack('=I',len(packet)));stream.write(packet)
    result=subprocess.run([exe,'--descriptors',str(file)],capture_output=True,text=True,check=True,timeout=15)
    actual=[tuple(map(int,line.split())) for line in result.stdout.splitlines()]
    expected=[basic(packet) for packet in packets]
    assert len(actual)==len(expected),(len(actual),len(expected))
    for index,(c,b) in enumerate(zip(actual,expected)):
        assert c==b,('descriptor acceptance drift',index,len(packets[index]),c,b)
print(f'BASIC/C descriptor drift: {len(packets)} admission boundaries agree')
