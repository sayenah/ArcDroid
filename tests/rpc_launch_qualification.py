#!/usr/bin/env python3
"""Private runner recipes using only project-authored launch subjects.
Firmware/boot resources are supplied by the caller, never copied into source.
GPL-2.0-only.
"""
import argparse, hashlib, shutil, struct, subprocess, sys
from pathlib import Path
from generate_fixtures import _old_directory, _old_entry, _l_write, _store_u24, _old_map_checksum
from rpc_video_rom import Guest

def subject(kind):
    g=Guest()
    # Independent execution count, kept outside the reloaded helper image.
    g.literal(0,0x8a00);g.literal(1,0x8b00);g.literal(2,4);g.literal(3,0);g.literal(4,0)
    g.emit(0xef020023);g.literal(1,0x8b00)
    g.emit(0x63a05000,0x75915000,0xe2855001,0xe5815000)
    g.literal(0,0x8a00);g.literal(2,4);g.literal(3,0);g.literal(4,1);g.emit(0xef020024)
    if kind=='returning':g.emit(0xef000011)
    elif kind=='resetting':g.emit(0xef00006a) # PRM OS_Reset; never returns
    elif kind=='blocked':g.emit(0xeafffffe)
    elif kind=='prompt':
        g.literal(0,0x8c00);g.emit(0xef000002)
        g.literal(0,0x9000);g.literal(1,255);g.literal(2,32);g.literal(3,126);g.emit(0xef00000e,0xef000011)
    else:
        g.literal(0,200);g.literal(1,0x4b534154);g.literal(2,0x8800);g.literal(3,0);g.emit(0xef0600c0)
        poll=len(g.w);g.literal(0,0);g.literal(1,0x9000);g.emit(0xef0600c7)
        g.emit(0xea000000|((poll-len(g.w)-2)&0xffffff))
    data=bytearray(0xc20);code=struct.pack('<'+'I'*len(g.w),*g.w);data[:len(code)]=code
    data[0x800:0x80c]=b'ArcAuthored\0';data[0xa00:0xa0d]=b'ArcAuth$Runs\0'
    data[0xc00:0xc10]=b'ArcAuth input: \0'
    return data

def disc(kind):
    data=subject(kind);out=bytearray(655360);end=7+(len(data)+255)//256
    _store_u24(out,0,end);_store_u24(out,256,2560-end);_store_u24(out,252,2560)
    out[247:252]=b'Acro\r';out[502:507]=b'rted\r';out[510]=3
    out[255]=_old_map_checksum(out[:255]);out[511]=_old_map_checksum(out[256:511])
    root=_old_directory();_old_entry(root,0,b'!Boot',len(data),7,load=0x8000)
    _l_write(out,2,root);_l_write(out,7,data);return out

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner',required=True);p.add_argument('--core',required=True)
    p.add_argument('--firmware',required=True);p.add_argument('--archives',required=True)
    p.add_argument('--cpu',required=True,choices=['arm610','arm710','sa110','arm7500'])
    p.add_argument('--layout',required=True,choices=['ro350','ro360','ro370'])
    p.add_argument('--work',required=True);p.add_argument('--case',default='returning',choices=['returning','desktop','blocked','prompt','resetting'])
    p.add_argument('--media');p.add_argument('--input');p.add_argument('--disk-control')
    p.add_argument('--action',default='boot',choices=['boot','application','product']);p.add_argument('--target');p.add_argument('--index',type=int)
    p.add_argument('--frames',type=int,default=3600);p.add_argument('--cells',type=int,default=3)
    p.add_argument('--session',type=int,default=1);p.add_argument('--reset-frame',type=int)
    p.add_argument('--reset-stage',choices=['prepared','consumed','dispatch','handoff'])
    p.add_argument('--reset-stage-frame',type=int,default=2600)
    p.add_argument('--enter-gate-frame',type=int);p.add_argument('--content-id');p.add_argument('--fresh-hd',action='store_true')
    p.add_argument('--trace',action='store_true');p.add_argument('--dump-frame',type=int);p.add_argument('--copy-save');p.add_argument('--prepare-only',action='store_true')
    a=p.parse_args();root=Path(a.work).resolve();root.mkdir(parents=True,exist_ok=True)
    system=root/'system';save=root/'save';boot=system/'arcdroid/riscpc/boot';boot.mkdir(parents=True,exist_ok=True)
    for leaf in ('HardDisc4.zip','PlingSystem.zip'):shutil.copyfile(Path(a.archives)/leaf,boot/leaf)
    if a.copy_save:
        if save.exists():raise ValueError('copy destination already exists')
        shutil.copytree(a.copy_save,save)
    else:save.mkdir(exist_ok=True)
    media=Path(a.media).resolve() if a.media else root/(a.case+'.adl')
    if not a.media:media.write_bytes(disc(a.case))
    # A structural authored subject, rather than a private title identity.
    content=a.content_id or hashlib.sha1(b'ArcDroid authored controller qualification').hexdigest()
    if len(content)!=40 or any(c not in '0123456789abcdef' for c in content):raise ValueError('content ID')
    if a.fresh_hd:
        if not a.copy_save:raise ValueError('--fresh-hd requires a newly copied save')
        old=save/'arcdroid/hd-riscpc'/content
        if old.exists():old.rename(root/'retained-hd')
    plan=root/'case.arcplan';plan.write_text(f'version=2\nmodel={a.cpu}\nrate_mips=16\nfirmware={Path(a.firmware).resolve()}\ncmos=persist\nrtc_epoch=946684800\ncontent_id={content}\ncmos_layout={a.layout}\nmedia={media}\nhd_populate=on\nlaunch_action={a.action}\nlaunch_session={a.session}\nobserver_cells={a.cells}\n')
    with plan.open('a') as f:
        if a.target:f.write(f'launch_target={a.target}\n')
        if a.index is not None:f.write(f'launch_index={a.index}\n')
        if a.input:f.write(f'input={Path(a.input).resolve()}\n')
        if a.disk_control:f.write(f'disk_control={Path(a.disk_control).resolve()}\n')
    if a.input and a.enter_gate_frame is not None:raise ValueError('input and enter-gate-frame are exclusive')
    if a.enter_gate_frame is not None:
        frame=a.enter_gate_frame;lines=[f'{frame} key 293 1',f'{frame+2} key 293 0'];frame+=30
        for ch in 'basic -quit adfs::4.$.arcgate':
            shifted=ch in ':$';key={':':59,'$':52}.get(ch,ord(ch))
            if shifted:lines.append(f'{frame} key 304 1')
            lines.extend([f'{frame} key {key} 1',f'{frame+2} key {key} 0'])
            if shifted:lines.append(f'{frame+3} key 304 0')
            frame+=6
        lines.extend([f'{frame} key 13 1',f'{frame+2} key 13 0'])
        script=root/'enter-gate.txt';script.write_text('\n'.join(lines)+'\n')
        with plan.open('a') as f:f.write(f'input={script}\n')
    if a.trace:
        with plan.open('a') as f:f.write(f'observation_trace={save/"status.jsonl"}\nobservation_classes=swi\nobservation_swis=0x20006\nobservation_status=authored\n')
    command=[a.runner,'--core',a.core,'--plan',str(plan),'--system',str(system),'--save',str(save),'--frames',str(a.frames),
        '--timing',str(root/'timing.csv')]
    if a.dump_frame is not None:command+=['--dump-frame',str(a.dump_frame),str(root/'frame.ppm')]
    if a.reset_frame is not None:command+=['--preparation-reset-at',str(a.reset_frame)]
    if a.reset_stage is not None:command+=['--preparation-reset-stage',a.reset_stage,'--reset-stage-frame',str(a.reset_stage_frame)]
    if not a.prepare_only:
        with (root/'runner.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
        print(f'{a.cpu} {a.case}: runner completed; observations in {root/"runner.log"}')
    else:print(subprocess.list2cmdline(command))
if __name__=='__main__':main()
