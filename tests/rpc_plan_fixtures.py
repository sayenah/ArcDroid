#!/usr/bin/env python3
"""Generate external test-plan inputs in the build tree only. GPL-2.0-only."""
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
from rpc_session_rom import devices
from rpc_video_rom import Guest, program, cases
from rpc_d2_rom import audio, inputs, RATES
from rpc_plan_matrix_rom import matrix
from rpc_video_paths import CASES as path_cases, program as path_program


def rom(directory, name, words):
    data = bytearray(struct.pack('<I', 0xeafffffe) * (2097152//4))
    data[:len(words)*4] = struct.pack('<'+'I'*len(words), *words)
    (directory / (name+'.rom')).write_bytes(data)


def plan(directory, name, model, firmware=None, factory=False, script=None):
    text = f'version=1\nmodel={model}\nfirmware={firmware or name}.rom\ncmos={"factory" if factory else "seed"}\n'
    if not factory: text += 'seed=authored.seed\n'
    text += 'rtc_epoch=0\n'
    if script: text += f'input={script}\n'
    (directory / (name+'.arcplan')).write_text(text)


def generate(directory):
    directory.mkdir(parents=True,exist_ok=True)
    (directory/'authored.seed').write_bytes(bytes(i^0xa5 for i in range(256)))
    # Script is sampled before the corresponding retro_run; starts at zero.
    # Chronological frame order, with equal-frame source events permitted.
    (directory/'keys.input').write_text('0 key 97 1\n0 mouse 12 -7 1\n3 key 97 0\n4 mouse 0 0 0\n')
    for m,model in enumerate(['arm610','arm710','sa110','arm7500']):
        plan(directory,f'bad-mode-{m}',model,firmware='bad-mode')
        name=f'matrix-{m}';words,_=matrix(m);rom(directory,name,words);plan(directory,name,model)
        name=f'devices-{m}';rom(directory,name,devices(RATES[m],m));plan(directory,name,model)
        name=f'audio-{m}';rom(directory,name,audio(RATES[m]).words);plan(directory,name,model)
        name=f'mixed-av-{m}'
        words=audio(RATES[m]).words[:-1]+program(m,32,8,3,0,'palette')
        rom(directory,name,words);plan(directory,name,model)
        name=f'input-{m}';rom(directory,name,inputs(m).words);plan(directory,name,model,script='keys.input')
        if m>=2:
            name=f'factory-{m}';rom(directory,name,inputs(m,factory=True).words);plan(directory,name,model,factory=True)
        for name,w,h,d,hz,scenario in cases():
            if name in ('frontend','threshold'):continue
            name=f'video-{m}-{name}';rom(directory,name,program(m,w,h,d,hz,scenario));plan(directory,name,model)
        for case in path_cases:
            name=f'video-paths-{m}-{case[0]}'
            rom(directory,name,path_program(m,case));plan(directory,name,model)
        g=Guest();g.w=[0xeafffffe]*64;g.w[0]=0xea00003e;g.w[7]=0xea000017
        g.w[32:44]=[0xe59f0018,0xe3a01000,0xe5801000,0xe59f0018,
                     0xe5901000,0xe2811001,0xe5801000,0xe25ef004,
                     0x03200038,0xeafffffe,0xeafffffe,0x10020000]
        g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0x93);g.emit(0xe121f000)
        g.write(0x10020000,0);g.write(0x03200038,128)
        g.write(0x10020004,0xa5);g.emit(0xeafffffe)
        name=f'fiq-{m}';rom(directory,name,g.w);plan(directory,name,model)
        # Read each 2 MiB boundary of the full 8 MiB aperture after entering
        # 32-bit mode. Distinct declared end words identify whole-image mirrors.
        for size in [2,4,8]:
            g=Guest();g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
            for i in range(4):
                g.literal(0,(i+1)*2097152-4);g.emit(0xe5901000)
                g.literal(0,0x10020000+i*4);g.emit(0xe5801000)
            g.emit(0xeafffffe)
            name=f'aperture-{m}-{size}';data=bytearray(struct.pack('<I',0xeafffffe)*(size*1048576//4))
            data[:len(g.w)*4]=struct.pack('<'+'I'*len(g.w),*g.w)
            for i in range(size//2):struct.pack_into('<I',data,(i+1)*2097152-4,0x10+i)
            (directory/(name+'.rom')).write_bytes(data);plan(directory,name,model)
    for m in [2,3]:
        name=f'empty-factory-{m}';rom(directory,name,inputs(m,factory=True,write_cmos=False).words)
        plan(directory,name,['sa110','arm7500'][m-2],factory=True)
    # Real guest containment, never test-hook fault injection.
    rom(directory,'bad-mode',[0xe3a00070,0xee010f10,0xe3a00014,0xe121f000,0xeafffffe])
    plan(directory,'bad-mode','sa110')


if __name__=='__main__':generate(Path(sys.argv[1]))
