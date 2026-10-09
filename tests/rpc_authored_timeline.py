#!/usr/bin/env python3
"""Independent ARM subset oracle for the authored R1 guests. GPL-2.0-only.

This is deliberately not an emulator: it admits only the instructions emitted
by the calibration generators, fails on anything else, and implements the
uncached timing equations of riscpc-timing.md §§2/5. No core is an input.
"""
from fractions import Fraction as F

def ceil(t): return -(-t.numerator // t.denominator)

class Timeline:
    def __init__(self, words, model):
        self.words=words; self.model=model; self.reg=[0]*16; self.pc=0
        self.time=F(0); self.ordinal=0; self.zero=False; self.memory={}
        self.issues={}; self.writes=[]; self.reads=[]; self.controls={}
        self.timer_latch=0; self.timer_due=None; self.timer_pending=False
        self.base=max(F(1000, (20,35,100,35)[model]),F(125,2)) if model==2 else F(1000,(20,35,100,35)[model])
        self.q=F(125,2) if model==3 else F(125,4)
        self.fetch=28*self.q if model==3 else 7*self.q
        self.next_event=None
    def event(self,now):raise ValueError('no declared event')
    def advance_base(self,amount):
        end=self.time+amount
        while self.next_event is not None and self.next_event<=ceil(end):
            end+=self.event(self.next_event)
        self.time=end
    def advance_bus(self,amount):
        end=self.time+amount;hold=0;arrival=None
        while self.next_event is not None and self.next_event<=ceil(end):
            now=self.next_event;cost=self.event(now)
            if cost:
                if arrival is None:arrival=now
                hold+=cost
        self.time=end
        if hold:
            self.time=max(end,arrival)+hold
            while self.next_event is not None and self.next_event<=ceil(self.time):
                self.time+=self.event(self.next_event)
    def service(self, address, byte=False):
        address &= 0x1fffffff
        if address < 0x02000000: return self.fetch
        if address < 0x03000000 or address >= 0x10000000: return 5*self.q
        if 0x03200000 <= address < 0x03210000 or 0x03400000 <= address < 0x03800000: return 4*self.q
        period=F(125,2)*(2 if self.model==3 else 1)
        return ceil(self.time/period)*period-self.time+9*period
    def read(self,address):
        address &= 0x1fffffff
        if address < 0x02000000:
            index=(address % 2097152)//4
            return self.words[index] if index<len(self.words) else 0xeafffffe
        if address==0x03200010:
            if self.timer_due is not None and self.timer_due<=ceil(self.time): self.timer_pending=True
            return 32 if self.timer_pending else 0
        return self.memory.get(address,0)
    def write(self,address,value):
        address &= 0x1fffffff; self.memory[address]=value
        self.writes.append((ceil(self.time),address,value,self.ordinal))
        if address==0x03200040: self.timer_latch=(self.timer_latch&0xff00)|(value&255)
        if address==0x03200044: self.timer_latch=(self.timer_latch&255)|((value&255)<<8)
        if address==0x03200048: self.timer_due=ceil(self.time)+500*(self.timer_latch+1)
        if address==0x03200014 and value&32: self.timer_pending=False
    def operand(self,op):
        if op & (1<<25):
            v=op&255; rotate=((op>>8)&15)*2
            return ((v>>rotate)|(v<<(32-rotate)))&0xffffffff if rotate else v
        assert not(op&16), hex(op)
        v=self.reg[op&15]; shift=(op>>7)&31; kind=(op>>5)&3
        if kind==0:return (v<<shift)&0xffffffff
        if kind==1:return v>>shift if shift else 0
        raise ValueError('unsupported authored shift '+hex(op))
    def step(self):
        self.advance_base(self.base); self.ordinal+=1; self.issues[self.ordinal]=ceil(self.time)
        self.reg[15]=self.pc+8; op=self.read(self.pc); target=self.pc+4
        condition=op>>28
        assert condition in (0,1,14), hex(op)
        execute=condition==14 or self.zero==(condition==0)
        fetch=self.service(self.pc)
        data_address=None
        if execute:
            if op & 0x0e000000 == 0x0a000000:
                offset=op&0xffffff
                if offset&0x800000:offset-=0x1000000
                if op&(1<<24):self.reg[14]=self.pc+4
                target=self.pc+8+4*offset
            elif op & 0x0c000000 == 0x04000000:
                assert not(op&(1<<25)),hex(op)
                rn=(op>>16)&15; rd=(op>>12)&15
                offset=(op&4095)*(1 if op&(1<<23) else -1)
                address=self.reg[rn]+(offset if op&(1<<24) else 0)
                if op&(1<<20):
                    self.reg[rd]=self.read(address); self.reads.append((ceil(self.time),address,self.ordinal))
                    if rd==15:target=self.reg[rd]
                else:self.write(address,self.reg[rd])
                if not(op&(1<<24)) or op&(1<<21):self.reg[rn]=(self.reg[rn]+offset)&0xffffffff
                # A functional transfer is at issue; its service follows fetch.
                data_address=address
            elif op & 0x0f000000 == 0x0e000000:
                assert op in (0xee010f10,),hex(op) # control changes no external cost
            elif op==0xe121f000:pass # SVC32 status setup
            elif op & 0x0c000000 == 0:
                code=(op>>21)&15; rn=(op>>16)&15; rd=(op>>12)&15
                a=self.reg[rn]; b=self.operand(op)
                if code==0:v=a&b
                elif code==1:v=a^b
                elif code in (2,10):v=(a-b)&0xffffffff
                elif code==4:v=(a+b)&0xffffffff
                elif code==8:v=a&b
                elif code==12:v=a|b
                elif code==13:v=b
                else:raise ValueError('unsupported authored ALU '+hex(op))
                if code in (8,10) or op&(1<<20):self.zero=v==0
                if code not in (8,10):
                    self.reg[rd]=v
                    if rd==15:target=v&~3
            else:raise ValueError('unsupported authored instruction '+hex(op))
        self.pc=target;self.advance_bus(fetch)
        if data_address is not None:self.advance_bus(self.service(data_address,bool(op&(1<<22))))
        return op
    def through(self,ordinal):
        while self.ordinal<ordinal:self.step()
        return self

def actions(guest,model):
    t=Timeline(guest.words,model).through(max(n for n,_,_ in guest.actions))
    return [(t.issues[n],kind,value) for n,kind,value in guest.actions]
