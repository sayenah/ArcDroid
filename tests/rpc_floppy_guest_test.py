#!/usr/bin/env python3
"""Authored ARM/MMIO/FIQ floppy workloads; datasheet expectations, no captures.
FDC37C665GT pp.43–45,61–71/tables 27/28. GPL-2.0-only.
"""
from pathlib import Path
import hashlib,re,struct,subprocess,sys,tempfile,zlib,zipfile
sys.dont_write_bytecode=True
from rpc_d2_rom import Guest
from generate_fixtures import build_d_discovery,build_l_discovery,build_pdd5_jfd,pdd5_descriptor_offset,build_extended_jfd
runner,core,shipping=sys.argv[1:4]
fixtures=Path(sys.argv[4]) if len(sys.argv)>4 else None
class FdcGuest:
    def __init__(self):
        self.g=Guest();self.g.init();self.g.load(10,0x03010fc0)
        self.expected=[];self.finished=False
        self.g.write(9,0x28,0x10)
        self.write(0x3f2,0x3c);self.irq()
        for d in range(4):self.cmd(8);self.result([0xc0|d,0])
        self.cmd(3,0xf1,2);self.write(0x3f7,2)
    def write(self,port,value):
        self.g.mov(0,value);self.g.emit(0xe5ca0000|((port-0x3f0)*4))
    def cmd(self,op,*p):
        self.write(0x3f5,op)
        for v in p:self.write(0x3f5,v)
    def irq(self):
        g=self.g;loop=len(g.words);g.read(0,9,0x24);g.emit(0xe3100010);g.branch(loop,0)
    def observe(self,reg,value):
        self.g.store(reg,8,len(self.expected)*4);self.expected.append(value)
    def result(self,values):
        g=self.g;loop=len(g.words);g.read(0,10,16);g.emit(0xe20000f0,0xe35000d0);g.branch(loop,1)
        for v in values:g.read(0,10,20);self.observe(0,v)
    def seek(self,unit,drive,c):
        self.write(0x3f2,0x3c|drive);self.cmd(15,unit,c);self.irq();self.cmd(8);self.result([0x20|unit,c])
    def data(self,op,unit=0,head=0,c=0,h=0,r=3,n=3,eot=4,dtl=255,gpl=0x35):
        self.cmd(op,unit|(head<<2),c,h,r,n,eot,gpl,dtl)
    def transfer(self,count,total,write=None,tc=True,nd=False):
        g=self.g;g.load(1,0x03012000);g.load(2,0x0302a000);g.load(5,count)
        g.mov(3,0);g.mov(4,0)
        loop=len(g.words)
        g.read(0,10,16);g.emit(0xe20000f0,0xe35000d0);done=len(g.words);g.emit(0)
        if nd:
            g.emit(0xe3100080);g.branch(loop,0)
        else:g.read(0,9,0x30);g.emit(0xe3100001);g.branch(loop,0)
        if write is not None:g.emit(0xe1530005);g.branch(loop,2) # stop host writes at its byte count
        g.emit(0xe2833001,0xe1530005)  # ADD count, CMP count,target
        if write is None:
            if nd:g.read(6,10,20)
            elif tc:g.emit(0x05d26000,0x15d16000) # last byte uses IOMD TC address
            else:g.emit(0xe5d16000)
            g.emit(0xe0844006)
        else:
            g.mov(6,write)
            if nd:g.emit(0xe5ca6014)
            elif tc:g.emit(0x05c26000,0x15c16000)
            else:g.emit(0xe5c16000)
        g.branch(loop)
        end=len(g.words);g.words[done]=0x0a000000|((end-done-2)&0xffffff)
        self.observe(3,count);self.observe(4,total if write is None else 0)
    def finish(self,path):
        if not self.finished:
            self.g.mov(0,0xa5);self.observe(0,0xa5);self.g.emit(0xeafffffe);self.finished=True
        rom=struct.pack('<'+'I'*len(self.g.words),*self.g.words)
        path.write_bytes(rom+bytes(2097152-len(rom)))

def ordinary(save,rom,model=0):
    digest=hashlib.sha256(rom.read_bytes()).digest();b=bytearray(512);b[:8]=b'ARCRPCM1'
    struct.pack_into('<IIIIQ',b,8,1,512,model+1,0x10003,1);b[32:72]=b'a'*40;b[72:104]=digest
    struct.pack_into('<I',b,508,zlib.crc32(b[:508]));base=save/'arcdroid/cmos-riscpc';base.mkdir(parents=True,exist_ok=True)
    (base/f'{"a"*40}-m{model+1}-{digest.hex()}.1').write_bytes(b)

def execute(d,guest,media,helper=None,extra=(),script=None,pacing='flat',save=None,frames=60,model=0):
    rom=d/'guest.rom';guest.finish(rom);save=save or d/'save';save.mkdir(exist_ok=True);ordinary(save,rom,model)
    models=["arm610","arm710","sa110","arm7500"]
    plan=d/'probe.arcplan';plan.write_text(f'version=2\nmodel={models[model]}\nfirmware={rom}\ncmos=persist\ncmos_layout=ro370\ncontent_id={"a"*40}\nrtc_epoch=0\nmedia={media}\n'+(f'drive1={helper}\n' if helper else '')+(f'disk_control={script}\n' if script else ''))
    r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(d),'--save',str(save),'--frames',str(frames),'--pacing',pacing,'--ram-offset','131072','--ram-words',str(len(guest.expected)),*extra],capture_output=True,text=True)
    assert r.returncode==0,r.stderr+r.stdout
    assert "Timed RPC disk-control operation refused" not in r.stderr,r.stderr
    words=[int(w,16) for w in re.findall(r'^ram=(.*)$',r.stdout,re.M)[-1].split(',')]
    assert words==guest.expected,[(i,a,b) for i,(a,b) in enumerate(zip(words,guest.expected)) if a!=b]
    return r.stdout,save,plan

def stores(case):return {x.name:x.read_bytes() for x in (case/'save/arcdroid/jfd').glob('*.[ab]')}
def latest(values):return max(values.values(),key=lambda b:struct.unpack_from('<Q',b,68)[0])
def committed_view(b):return b[:68]+b[76:-4]

with tempfile.TemporaryDirectory(prefix='arc-rpc-floppy-') as tmp:
    d=Path(tmp);a=build_d_discovery([],b'RPC authored');h=build_l_discovery([],b'Helper authored')
    for t in range(4):
        for r in range(1,5):a[(t*5+r)*1024:(t*5+r+1)*1024]=bytes([0x10+t*8+r])*1024
    for r in [3,4]:h[r*256:(r+1)*256]=bytes([0x53+r])*256
    (d/'disc.adf').write_bytes(a);(d/'helper.adl').write_bytes(h)
    g=FdcGuest();g.write(0x3f2,0x34);g.cmd(15,0,0)
    loop=len(g.g.words);g.g.read(0,10,16);g.g.emit(0xe3100001);g.g.branch(loop,1)
    g.g.read(0,9,0x24);g.g.emit(0xe2000010);g.observe(0,0)
    g.write(0x3f2,0x3c);g.g.read(0,9,0x24);g.g.emit(0xe2000010);g.observe(0,0x10)
    g.cmd(8);g.result([0x20,0]);g.cmd(4,0);g.result([0x38]);g.cmd(0x4a,0);g.result([0,0,0,0,0,1,3]);g.cmd(16);g.result([0x90]);g.cmd(0x11);g.result([0x80]);g.cmd(0xc2);g.result([0x80])
    g.cmd(19,0,0x0d,7);g.cmd(18,0x9f);g.cmd(0x94);g.result([0x10]);g.cmd(14);g.result([0,0,0,0,0xf1,2,0,0x9f,0x0d,7])
    g.write(0x3f4,0x82);g.irq()
    for unit in range(4):g.cmd(8);g.result([0xc0|unit,0])
    g.cmd(14);g.result([0,0,0,0,0,0,0,0x9c,0x0d,7]);g.cmd(3,0xf1,2);g.cmd(18,0x9f);g.cmd(0x14);g.result([0])
    g.seek(1,0,1);g.cmd(14);g.result([0,1,0,0,0xf1,2,0,0x1f,0x0d,7])
    g.data(0x46,c=0);g.result([0x40,4,0x10,0,0,3,3]);g.seek(1,0,0)
    g.data(0x46,h=1);g.result([0x40,4,0,0,1,3,3])
    g.data(0x46,unit=3,head=1,h=1,eot=3);g.transfer(1024,0x1b*1024);g.result([7,0,0,1,1,1,3])
    g.write(0x3f2,0x3d);g.data(0x46,unit=0,n=1,eot=3);g.transfer(256,0x56*256);g.result([0,0,0,1,0,1,1])
    g.write(0x3f2,0x3c);g.data(0xc6,eot=4);g.transfer(3072,(0x13+0x14+0x19)*1024);g.result([4,0,0,0,1,2,3])
    g.data(0x56,unit=0x80,r=3,dtl=2);g.result([0,0,0,1,0,1,3])
    g.write(0x3f7,1);g.data(0x46,r=3,eot=3);g.transfer(1024,0x13*1024);g.result([0,0,0,1,0,1,3]);g.write(0x3f7,2)
    g.data(0x45,r=3,eot=3);g.transfer(1024,0,write=0x6a);g.result([0,0,0,1,0,1,3])
    g.data(0x45,r=4,eot=4);g.transfer(1023,0,write=0x7b);g.result([0,0,0,1,0,1,3])
    g.data(0x4c,r=3,eot=3);g.transfer(1024,0x6a*1024,tc=False);g.result([0x40,0,0x40,0,0,3,3])
    g.data(0x6c,r=3,eot=4);g.result([0x40,0x80,0x40,1,0,1,3])
    g.cmd(3,0xf1,3);g.data(0x46,r=3,eot=3);g.transfer(1024,0x6a*1024,tc=False,nd=True);g.result([0x40,0x80,0,1,0,1,3])
    g.data(0x45,r=2,eot=2);g.transfer(1024,0,write=0x8b,tc=False,nd=True);g.result([0x40,0x80,0,1,0,1,3]);g.cmd(3,0xf1,2)
    g.cmd(0x0a,0);g.result([0x40,1,0,0,0,0,0])
    g.data(6,r=3,eot=3);g.result([0x40,4,0,0,0,3,3]) # valid FM, unrepresentable DD track
    g.data(0x42,r=0,eot=2);g.transfer(2048,sum(a[:2048]),tc=False);g.result([0x40,4,0,0,0,1,3])
    g.cmd(0xcf,0,1);g.irq();g.cmd(8);g.result([0x20,1])
    g.cmd(0x8f,0,1);g.irq();g.cmd(8);g.result([0x20,0])
    g.cmd(7,0);g.irq();g.cmd(8);g.result([0x20,0])
    g.cmd(19,0,0x4d,7);g.data(0x46,c=1,r=3,eot=3);g.g.read(0,10,16);g.g.emit(0xe2000001);g.observe(0,1);g.transfer(1024,0x23*1024);g.result([0,0,0,2,0,1,3]);g.seek(0,0,0)
    g.data(0x42,c=1,r=0,eot=2);g.transfer(2048,sum(a[10240:12288]),tc=False);g.result([0x40,4,0,1,0,1,3]);g.seek(0,0,0)
    g.write(0x3f2,0x3d);g.cmd(0x4d,1,1,16,0x32,0xa6)
    for sector in range(16):
        for lane,value in enumerate([0,0,sector,1]):
            loop=len(g.g.words);g.g.read(0,9,0x30);g.g.emit(0xe3100001);g.g.branch(loop,0)
            g.g.load(1,0x0302a000 if sector==15 and lane==3 else 0x03012000)
            g.g.mov(0,value);g.g.emit(0xe5c10000)
    g.result([1,0,0,0,0,15,1]);g.data(0x46,unit=1,n=1,r=3,eot=3);g.transfer(256,0xa6*256);g.result([1,0,0,1,0,1,1])
    g.write(0x3f2,0x3c);g.cmd(0x4d,0,3,5,0xff,0x5a);g.result([0x40,2,0,0,0,0,3])
    first,save,plan=execute(d,g,'disc.adf','helper.adl',extra=('--disk-writes','original'),save=d,frames=360)
    changed=(d/'disc.adf').read_bytes();assert changed[3*1024:4*1024]==bytes([0x6a])*1024
    assert changed[4*1024:5*1024]==bytes([0x7b])*1023+b'\0' and (d/'helper.adl').read_bytes()==bytes([0xa6])*4096+h[4096:]
    # Replay source bytes and an independently encoded CMOS record: identical trace.
    (d/'disc.adf').write_bytes(a);(d/'helper.adl').write_bytes(h);second,_,_=execute(d,g,'disc.adf','helper.adl',extra=('--disk-writes','original'),pacing='burst',save=save,frames=360)
    assert first==second,'frontend pacing changed canonical floppy trace'
    for model in [1,2,3]:
        traces=[]
        for pacing in ['flat','burst']:
            (d/'disc.adf').write_bytes(a);(d/'helper.adl').write_bytes(h)
            trace,_,_=execute(d,g,'disc.adf','helper.adl',extra=('--disk-writes','original'),save=d,frames=360,model=model,pacing=pacing)
            traces.append(trace)
        assert traces[0]==traces[1],('pacing',model)

    # The shipping core rejects these plans before accessing authored media.
    r=subprocess.run([runner,'--core',shipping,'--plan',str(plan),'--save',str(save),'--reject'],capture_output=True,text=True);assert r.returncode==0,r.stderr
    # Raw/JFD write-policy matrix; full TC commits, early TC never reaches JFD store.
    j=build_pdd5_jfd(a,{t:list(range(5)) for t in range(160)})
    for header in [0,1,2,3]:
        struct.pack_into('<I',j,304,header);(d/'disc.jfd').write_bytes(j)
        for writes in ['off','shadow','original']:
            for policy in ['as-imaged','off']:
                enabled=writes!='off' and (header==2 or policy=='off')
                p=FdcGuest();p.data(0x45,r=3,eot=3)
                if enabled:p.transfer(1024,0,write=0x6a);p.result([0,0,0,1,0,1,3])
                else:p.result([0x40,2,0,0,0,3,3])
                if enabled:
                    p.data(0x45,r=4,eot=4);p.transfer(1023,0,write=0x7b);p.result([0,0,0,1,0,1,3])
                case=d/f'j-{header}-{writes}-{policy}';case.mkdir();(case/'disc.jfd').write_bytes(j)
                execute(case,p,'disc.jfd',extra=('--disk-writes',writes,'--jfd-policy',policy))
                records=list((case/'save/arcdroid/jfd').glob('*.[ab]'))
                assert bool(records)==enabled,(header,writes,policy,records)
                for record in records:
                    saved=record.read_bytes();assert struct.unpack_from('<QI',saved,68)==(1,1)
                    assert struct.unpack_from('<HBHBBH',saved,80)==(0,0,3,3,2,1024)
                    assert saved[89:-4]==bytes([0x6a])*1024 and struct.unpack_from('<I',saved,len(saved)-4)[0]==zlib.crc32(saved[:-4])
                assert (case/'disc.jfd').read_bytes()==j,'JFD source changed'
    # New admission keys are v2-only; helper JFD and malformed scripts fail
    # through the same preparation/parser boundaries used by real plans.
    valid_plan=plan.read_text();bad_plan=d/'invalid.arcplan'
    def reject(text):
        bad_plan.write_text(text)
        result=subprocess.run([runner,'--core',core,'--plan',str(bad_plan),'--system',str(d),'--save',str(save),'--frames','1'],capture_output=True,text=True)
        assert result.returncode==1 and ('Invalid plan' in result.stderr or 'RPC drive 1 requires raw helper media' in result.stderr),result.stderr+result.stdout
    v1=f'version=1\nmodel=arm610\nfirmware={d / "guest.rom"}\ncmos=factory\nrtc_epoch=0\n'
    for key,value in [('media','disc.adf'),('drive1','helper.adl'),('disk_control','control')]:reject(v1+f'{key}={value}\n')
    reject(re.sub(r'^drive1=.*$', 'drive1=disc.jfd',valid_plan,flags=re.M))
    for text in ['0\n','1 eject\n0 insert\n','4294967296 eject\n','0 replace 0\n','0 replace 0 ../disc.adf\n','0 unknown\n']:
        (d/'invalid.control').write_text(text);reject(valid_plan+'disk_control=invalid.control\n')
    # Live policy changes remain pending through reinsertion, then reset samples
    # them. This also verifies that Shadow -> Original re-stages the raw file.
    def looping_writer():
        p=FdcGuest();target=len(p.g.words);p.data(0x45,r=3,eot=3);p.transfer(1024,0,write=0x5a);p.result([0,0,0,1,0,1,3])
        p.finish(d/'loop.rom');pos=len(p.g.words)-1;p.g.words[pos]=0xea000000|((target-pos-2)&0xffffff)
        return p
    pending=d/'pending';pending.mkdir();struct.pack_into('<I',j,304,3);(pending/'disc.jfd').write_bytes(j)
    (pending/'control').write_text('10 eject\n10 insert\n')
    trace,_,_=execute(pending,looping_writer(),'disc.jfd',script='control',extra=('--jfd-policy-at','2','off','--reset-at','30'))
    rows=[[int(x,16) for x in row.split(',')] for row in re.findall(r'^ram=(.*)$',trace,re.M)]
    assert rows[29][8]==0 and rows[29][11]==2,'live reinsertion applied pending policy'
    assert stores(pending),'reset did not apply the JFD policy'
    raw_policy=d/'raw-policy';raw_policy.mkdir();(raw_policy/'disc.adf').write_bytes(a)
    (raw_policy/'control').write_text('10 eject\n10 insert\n')
    execute(raw_policy,looping_writer(),'disc.adf',script='control',save=raw_policy,extra=('--disk-writes-at','2','original','--reset-at','30'))
    assert (raw_policy/'disc.adf').read_bytes()[3072:4096]==bytes([0x5a])*1024,'reset retained the old shadow path'
    # m3u and zip are prepared by the shipping adapters; timed replacement is real disk control.
    (d/'disc.adf').write_bytes(a);(d/'helper.adl').write_bytes(h);(d/'set.m3u').write_text('disc.adf\nhelper.adl\n')
    for media in ['set.m3u','set.zip']:
        if media.endswith('zip'):
            with zipfile.ZipFile(d/media,'w') as z:z.writestr('disc.adf',a);z.writestr('helper.adl',h)
        p=FdcGuest();p.seek(0,0,1);p.seek(0,0,0)
        p.data(0x46,r=3,eot=3);p.transfer(1024,0x13*1024);p.result([0,0,0,1,0,1,3])
        def wait_changed():
            loop=len(p.g.words);p.g.read(0,10,28);p.g.emit(0xe3100080);p.g.branch(loop,0)
        wait_changed();p.data(0x46,r=3,n=1,eot=3);p.transfer(256,0x56*256);p.result([0,0,0,1,0,1,1])
        p.seek(0,0,1);p.seek(0,0,0);wait_changed()
        p.data(0x46,r=3,eot=3);p.transfer(1024,0x13*1024);p.result([0,0,0,1,0,1,3])
        p.seek(0,0,1);p.seek(0,0,0);wait_changed()
        p.data(0x46,r=3,eot=3);p.result([0x40,5,0,0,0,0,0])
        (d/'control').write_text('30 eject\n30 select 1\n30 insert\n60 eject\n60 add\n60 replace 2 disc.adf\n60 select 2\n60 insert\n90 eject\n90 remove 2\n')
        one,_,_=execute(d,p,media,script='control',frames=120)
        two,_,_=execute(d,p,media,script='control',pacing='burst',frames=120);assert one==two
    if fixtures and len(sys.argv)>5:
        # The same authored capture is serviced by each shipping controller.
        # Compare committed snapshot bytes; each backend reads its peer's store.
        harness,testcore=sys.argv[5:7]
        for kind,sector,rom_kind in [('captured',1,'save'),('created',3,'create')]:
            image=fixtures/f'jfd-tab/{kind}-2.jfd';capture=image.read_bytes()
            if kind=='captured':
                at=pdd5_descriptor_offset(capture,0,sector);data_base=struct.unpack_from('<I',capture,32)[0]
                offset=struct.unpack_from('<I',capture,at+4)[0];pristine=capture[data_base+offset:data_base+offset+1024]
            else:pristine=bytes(1024)
            rpc=d/('cross-rpc-'+kind);rpc.mkdir();(rpc/'disc.jfd').write_bytes(capture)
            p=FdcGuest();p.data(0x46,r=sector,eot=sector);p.transfer(1024,sum(pristine));p.result([0,0,0,1,0,1,3])
            if kind=='created':
                # The established creation ROM writes map and directory first.
                # Match those observable writes before comparing whole stores.
                for prefix_sector in [0,2]:
                    p.data(0x45,r=prefix_sector,eot=prefix_sector);p.transfer(1024,0,write=0x5a);p.result([0,0,0,1,0,1,3])
            p.data(0x45,r=sector,eot=sector);p.transfer(1024,0,write=0x5a);p.result([0,0,0,1,0,1,3])
            execute(rpc,p,'disc.jfd')
            before=stores(rpc);assert before
            for machine,prefix in [('A3000',''),('A3020','newio-')]:
                rom=fixtures/f'arcdroid-jfd-{prefix}{rom_kind}-test.rom'
                r=subprocess.run([harness,testcore,str(rom),str(image),str(rpc),'--jfd-tab-step',machine,'as-imaged','shadow','00001500','basic'],capture_output=True,text=True)
                assert r.returncode==0,r.stderr+r.stdout
                peer=latest(stores(rpc));assert committed_view(peer)==committed_view(latest(before))
                arc=d/('cross-'+machine+'-'+kind);arc.mkdir()
                r=subprocess.run([harness,testcore,str(rom),str(image),str(arc),'--jfd-tab-step',machine,'as-imaged','shadow','006a0000','basic'],capture_output=True,text=True)
                assert r.returncode==0,r.stderr+r.stdout
                actual=stores(arc)
                if actual!=before:
                    left=latest(before);right=latest(actual)
                    raise AssertionError((kind,machine,'committed result differs',len(left),len(right),[(i,x,y) for i,(x,y) in enumerate(zip(left,right)) if x!=y][:32]))
                (arc/'disc.jfd').write_bytes(capture)
                reader=FdcGuest();reader.data(0x46,r=sector,eot=sector);reader.transfer(1024,0x5a*1024);reader.result([0,0,0,1,0,1,3])
                execute(arc,reader,'disc.jfd');assert stores(arc)==before
    if fixtures:
        identity=d/'identification';identity.mkdir();(identity/'disc.jfd').write_bytes((fixtures/'jfd-identification/sparse-d-timed.jfd').read_bytes())
        p=FdcGuest()
        for sector in [1,2,3]:p.cmd(0x4a,0);p.result([0,0,0,0,0,sector,3])
        p.data(0x46,r=3,eot=3);p.result([0x40,4,0,0,0,3,3])
        execute(identity,p,'disc.jfd')
        omitted=d/'omitted';omitted.mkdir();(omitted/'disc.jfd').write_bytes((fixtures/'jfd-file-data/d-track0-file-free.jfd').read_bytes())
        p=FdcGuest();p.data(0x46,r=3,eot=3);p.transfer(1024,0);p.result([0,0,0,1,0,1,3])
        execute(omitted,p,'disc.jfd',extra=('--disk-writes','off'))
        p=FdcGuest();p.data(0x46,r=3,eot=3);p.result([0x40,4,0,0,0,3,3])
        execute(omitted,p,'disc.jfd',extra=('--disk-writes','off','--jfd-omitted','strict'))
        quarantine=d/'quarantine';quarantine.mkdir();(quarantine/'disc.jfd').write_bytes((fixtures/'jfd-family/quarantine-guest.jfd').read_bytes())
        p=FdcGuest();p.data(0x46,r=3,eot=3);p.result([0x40,4,0,0,0,3,3])
        execute(quarantine,p,'disc.jfd')
        if len(sys.argv)<=5:
            created=d/'created';created.mkdir();(created/'disc.jfd').write_bytes((fixtures/'jfd-tab/created-2.jfd').read_bytes())
            p=FdcGuest();p.data(0x46,r=3,eot=3);p.transfer(1024,0);p.result([0,0,0,1,0,1,3])
            p.data(0x45,r=3,eot=3);p.transfer(1024,0,write=0x5a);p.result([0,0,0,1,0,1,3]);execute(created,p,'disc.jfd')
            saved=stores(created);assert saved
            p=FdcGuest();p.data(0x46,r=3,eot=3);p.transfer(1024,0x5a*1024);p.result([0,0,0,1,0,1,3]);execute(created,p,'disc.jfd');assert stores(created)==saved
        capture=(fixtures/'jfd-tab/captured-2.jfd').read_bytes()
        at=pdd5_descriptor_offset(capture,0,1);base=struct.unpack_from('<I',capture,32)[0];offset=struct.unpack_from('<I',capture,at+4)[0]
        for name,flag in [('id-crc',1),('data-crc',2)]:
            crc=d/name;crc.mkdir();faulty=bytearray(capture);header=struct.unpack_from('<I',faulty,at)[0]
            struct.pack_into('<I',faulty,at,header|(flag<<4));(crc/'disc.jfd').write_bytes(faulty)
            p=FdcGuest();p.data(0x46,r=1,eot=1)
            if flag==2:p.transfer(1024,sum(capture[base+offset:base+offset+1024]),tc=False)
            p.result([0x40,0x20,0x20 if flag==2 else 0,0,0,1,3]);execute(crc,p,'disc.jfd')
        readid_crc=d/'readid-crc';readid_crc.mkdir();image=build_extended_jfd(bytes([0x2a])*256)
        at=struct.unpack_from('<I',image,28)[0]
        struct.pack_into('<I',image,at,struct.unpack_from('<I',image,at)[0]|(1<<4));(readid_crc/'disc.jfd').write_bytes(image)
        p=FdcGuest();p.cmd(0x4a,0);p.result([0x40,0x20,0,0,0,0,1]);execute(readid_crc,p,'disc.jfd')
        fm=d/'fm';fm.mkdir();image=build_extended_jfd(bytes([0x4f])*256)
        table=struct.unpack_from('<I',image,28)[0];struct.pack_into('<I',image,table,(1<<16)|1);(fm/'disc.jfd').write_bytes(image)
        p=FdcGuest();p.cmd(0x0a,0);p.result([0,0,0,0,0,0,1]);p.data(6,r=0,n=1,eot=0);p.transfer(256,0x4f*256);p.result([0,0,0,1,0,1,1]);execute(fm,p,'disc.jfd')
        dtl=d/'dtl';dtl.mkdir();image=build_extended_jfd(bytes([0x2b])*128);table=struct.unpack_from('<I',image,28)[0]
        struct.pack_into('<I',image,table,2<<16);struct.pack_into('<I',image,304,2);(dtl/'disc.jfd').write_bytes(image)
        p=FdcGuest();p.data(0x46,r=0,n=0,eot=0,dtl=64);p.transfer(64,0x2b*64);p.result([0,0,0,1,0,1,0])
        p.data(0x45,r=0,n=0,eot=0,dtl=64);p.result([0x40,2,0,0,0,0,0]);execute(dtl,p,'disc.jfd')
        # D-121 fail-closed map guard, CRC result; a failed store gives WP.
        guard=d/'guard';guard.mkdir();(guard/'disc.jfd').write_bytes((fixtures/'jfd-allocation/guard-crossing.jfd').read_bytes())
        p=FdcGuest();p.data(0x45,r=0,eot=0);p.transfer(1024,0,write=0x6a);p.result([0x40,0x20,0x20,0,0,0,3])
        execute(guard,p,'disc.jfd');assert not stores(guard)
        fault=d/'fault';fault.mkdir();(fault/'disc.jfd').write_bytes((fixtures/'jfd-tab/captured-2.jfd').read_bytes())
        p=FdcGuest();p.data(0x45,r=1,eot=1);p.transfer(1024,0,write=0x6a);p.result([0x40,2,0,0,0,1,3])
        execute(fault,p,'disc.jfd',extra=('--fail-jfd-store',));assert not stores(fault)
print('rpc-floppy-guest: SuperIO/IOMD/FIQ DMA, two drives, TC/FIFO/ND, policies, zip/m3u/disk control and pacing passed')
