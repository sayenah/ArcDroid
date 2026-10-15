"""Authored qualification controller; never the generated shipping no-op.

CALL owns the whole Wimp lifetime. Its plan, poll/status blocks and code stay
in the BASIC slot; its temporary ByteV claimant has independent RMA ownership.
GPL-2.0-only.
"""
import struct


def immediate(value):
    for rotate in range(16):
        v=((value << (2*rotate)) | (value >> (32-2*rotate if rotate else 32))) & 0xffffffff
        if v<256:return rotate<<8 | v
    raise ValueError(value)


class Arm:
    def __init__(self):self.w=[0]*16;self.labels={};self.fix=[]
    def emit(self,*words):self.w.extend(words)
    def label(self,name):self.labels[name]=len(self.w)*4
    def branch(self,name,condition=14,link=False):
        self.fix.append((len(self.w),'b',name,condition,link));self.emit(0)
    def adr(self,reg,name):
        self.fix.append((len(self.w),'adr',name,reg,False));self.emit(0)
    def literal(self,reg,value):self.emit(0xe59f0000|reg<<12,0xea000000,value)
    def data(self,name,data):
        self.label(name);data+=bytes(-len(data)%4);self.emit(*struct.unpack('<'+'I'*(len(data)//4),data))
    def finish(self):
        for at,kind,name,arg,link in self.fix:
            delta=self.labels[name]-(at*4+8)
            self.w[at]=(arg<<28 | 0x0a000000 | int(link)<<24 | (delta//4 & 0xffffff)) if kind=='b' else (
                (0xe28f0000 if delta>=0 else 0xe24f0000) | arg<<12 | immediate(abs(delta)))
        return bytearray(struct.pack('<'+'I'*len(self.w),*self.w))


def controller():
    a=Arm();a.label('entry')
    a.emit(0xe92d5ff0,0xe1a0a002,0xe3a08000,0xe3a0b000) # save BASIC; ACK R10; task/RMA zero
    a.emit(0xe3a000c8);a.literal(1,0x4b534154);a.adr(2,'name');a.emit(0xe3a03000,0xef0600c0)
    a.branch('fail',6);a.emit(0xe1a08001,0xe3a09002)
    # A separate ByteV claimant, copied to RMA before claim. It only counts
    # calls, preserving every register and passing the documented query on.
    a.emit(0xe3a00006,0xe3a03040,0xef02001e);a.branch('close_fail',6);a.emit(0xe1a0b002)
    a.adr(4,'spy');a.emit(0xe894003f,0xe88b003f,0xe3a00000,0xe58b003c)
    a.emit(0xe3a00000,0xef02006d);a.branch('claim_spy',6)
    a.emit(0xe3100001);a.branch('claim_spy',0)
    a.emit(0xe3a00001,0xe1a0100b,0xe28b2014,0xef02006e);a.branch('free_fail',6)
    a.label('claim_spy');a.emit(0xe3a00006,0xe1a0100b,0xe28b203c,0xef02001f);a.branch('free_fail',6)
    a.emit(0xe3a07004);a.branch('notify',link=True)
    a.adr(0,'command');a.emit(0xef0600de);a.branch('release_fail',6)
    # A returning StartTask is not an outcome. Independently verify that this
    # particular authored CLI command performed its harmless variable write.
    a.adr(0,'childvar');a.adr(1,'childvalue');a.emit(0xe3a02004,0xe3a03000,0xe3a04000,0xef020023)
    a.branch('release_fail',6);a.emit(0xe3520002);a.branch('release_fail',1)
    a.emit(0xe5910000);a.literal(1,0x6b6f);a.emit(0xe1500001);a.branch('release_fail',1)
    a.emit(0xe3a07005);a.branch('notify',link=True)
    a.emit(0xef020042,0xe2802001,0xe3a00000);a.adr(1,'poll');a.emit(0xe3a03000,0xef0600e1)
    a.branch('release_fail',6);a.emit(0xe3a07006);a.branch('notify',link=True)
    a.branch('release')
    a.label('release_fail');a.emit(0xe3a09003)
    a.label('release');a.emit(0xe3a00006,0xe1a0100b,0xe28b203c,0xef020020)
    a.branch('close_fail',6) # retaining RMA is safe even if spy release fails
    a.emit(0xe59b703c);a.adr(4,'status');a.emit(0xe584702c) # scalar coexistence count
    a.emit(0xe3a00007,0xe1a0200b,0xef02001e,0xe3a0b000)
    a.emit(0xe1a00008);a.literal(1,0x4b534154);a.emit(0xef0600dd);a.branch('fatal',6)
    a.emit(0xe3a08000,0xe3a07007);a.branch('notify',link=True)
    a.emit(0xe58a900c);a.branch('return')
    a.label('free_fail');a.emit(0xe3a00007,0xe1a0200b,0xef02001e,0xe3a0b000)
    a.label('close_fail');a.emit(0xe1a00008);a.literal(1,0x4b534154);a.emit(0xef0600dd);a.branch('fatal',6)
    a.label('fail');a.emit(0xe3a00003,0xe58a000c)
    a.label('return');a.emit(0xe8bd5ff0,0xe10f3000,0xe3130010,0x01b0f00e,0x11a0f00e)
    a.label('fatal');a.emit(0xe3a00000,0xef000011);a.branch('fatal')
    a.label('notify');a.emit(0xe92d40ff);a.adr(3,'status')
    a.emit(0xe5837020,0xe5930024,0xe2800001,0xe5830024,0xe10f0000,0xe200001f,0xe5830028)
    a.emit(0xe3a00000,0xe3a01001,0xe3a02000);a.literal(4,0x53435241)
    a.emit(0xe3a05040,0xe3a06000,0xef020006,0xe3510006,0x13a09003,0xe8bd40ff,0xe10f3000,0xe3130010,0x01b0f00e,0x11a0f00e)
    a.label('spy');a.emit(0xe92d0001,0xe59c0000,0xe2800001,0xe58c0000,0xe8bd0001,0xe1b0f00e)
    a.data('name',b'ArcStub\0');a.data('command',b'Set ArcStub$Child ok\0')
    a.data('childvar',b'ArcStub$Child\0');a.data('childvalue',bytes(4))
    a.data('status',struct.pack('<16I',0x53435241,1,64,*([0]*13)))
    a.data('poll',bytes(256));a.data('plan',struct.pack('<2I',1,0))
    data=a.finish();data[:8]=b'ARCRHLP1'
    struct.pack_into('<14I',data,8,1,64,len(data),1,1,a.labels['entry'],1,1,1,a.labels['plan'],8,0,0,0)
    return data


def replace_carrier(data,image,first=7):
    """Replace only authored payload and its old-map allocation/length."""
    data=bytearray(data);end=first+(len(image)+255)//256
    assert 7<=first<end<=2560
    data[:3]=end.to_bytes(3,'little');data[256:259]=(2560-end).to_bytes(3,'little')
    for base in (0,256):
        value=255
        for i in range(254,-1,-1):
            if value>255:value=(value+1)&255
            value+=data[base+i]
        data[base+255]=value&255
    struct.pack_into('<I',data,535,len(image))
    data[539:542]=first.to_bytes(3,'little')
    for at in range(0,len(image),256):
        sector=first+at//256;track=sector//16
        physical=((track%80)*2+track//80)*4096+(sector%16)*256
        data[physical:physical+min(256,len(image)-at)]=image[at:at+256]
    return data
