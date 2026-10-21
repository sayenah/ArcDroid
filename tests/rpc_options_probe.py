"""Temporary, identified read-only probes; never shipping. GPL-2.0-only."""
import json
import os
from pathlib import Path
import subprocess
import sys
from rpc_ninja_commands import rebuild_commands, same_path

PREPARATION_PROBE = r'''
RETRO_API void arc_options_preparation_probe(const ArcRpcPreparation *p,unsigned *state,unsigned *out)
{
 *state=p?p->state:ARC_RPC_PREP_CANCELLED;
 if(p){out[1]=p->request.selection.model;out[2]=p->request.selection.os;
 out[3]=p->request.key.model;out[4]=p->request.key.layout;
 out[28]=p->launch_prepared;out[29]=p->attempt.live;out[30]=p->disc!=NULL;}
}
static const ArcRpcPreparation *option_clock_owner;
static unsigned option_clock_state;
uint64_t arc_options_unit_clock(void *opaque)
{
 (void)opaque;static uint64_t value;value+=1000;
 if(option_clock_owner && option_clock_owner->state!=option_clock_state){
 option_clock_state=option_clock_owner->state;value+=UINT64_C(66666665);}
 return value;
}
ArcRpcPreparationState arc_riscpc_preparation_frame(ArcRpcPreparation *p)
{
 option_clock_owner=p;option_clock_state=p?(unsigned)p->state:0;
 ArcRpcPreparationState state=arc_options_frame_step(p);option_clock_owner=NULL;return state;
}
'''
FRONTEND_PROBE = r'''
extern void arc_options_preparation_probe(const ArcRpcPreparation *,unsigned *,unsigned *);
RETRO_API void arc_options_probe(unsigned *state,unsigned out[64])
{
 memset(out,0,64*sizeof(*out));out[0]=arc_kind;
 arc_options_preparation_probe(arc_riscpc_host_preparation(arc_rpc_host),state,out);
 out[5]=arc_core_options.machine;out[6]=arc_core_options.cpu;out[7]=arc_core_options.requested_os;
 out[8]=arc_plan.disk_writes;out[9]=arc_core_options.jfd_write_policy;
 out[10]=arc_core_options.short_tail_zero;out[11]=arc_core_options.jfd_omitted_strict;
 out[12]=arc_core_options.launch;out[13]=arc_core_options.launch_chooser;
 out[14]=arc_core_options.launch_ramdisc;out[15]=arc_core_options.hard_disc_auto;
 out[16]=arc_core_options.jfd_boot;out[17]=arc_core_options.adffs_package;
 out[18]=arc_plan.a3010_joystick;
 out[19]=arc_core_options.pointer_relative;out[20]=arc_core_options.pointer_stick;
 out[21]=arc_core_options.pointer_speed;out[22]=arc_core_options.pointer_deadzone;
 out[23]=arc_core_options.osk_toggle;out[24]=arc_core_options.left_stick_dpad[0];out[25]=arc_core_options.left_stick_dpad[1];
 out[26]=arc_core_options.pad[0][0].automatic;
 out[27]=arc_disc_mode;
 for(unsigned port=0;port<2;++port)for(unsigned input=0;input<ARC_PAD_INPUT_COUNT;++input)
 out[32+port*ARC_PAD_INPUT_COUNT+input]=arc_input_resolution.binding[port][input].bound;
}
'''


class ProbeBuilder:
    def __init__(self, build, root, temporary):
        self.build = Path(build).resolve(); self.root = Path(root).resolve()
        self.temp = Path(temporary)
        cache = (self.build/'CMakeCache.txt').read_text(encoding='utf-8')
        ninja = next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
        self.commands = [r['command'] for r in json.loads(subprocess.check_output(
            [ninja,'-C',str(self.build),'-t','compdb','-x'],text=True))]
        self.objects = {}
        self.link = None

    def compile(self, relative, mutation=None):
        original = self.root/relative
        source = self.temp/original.name
        text = original.read_text(encoding='utf-8')
        if mutation:
            for before,after in (mutation if isinstance(mutation,list) else [mutation]):
                if text.count(before) != 1:
                    raise RuntimeError('mutation anchor is not unique: '+before)
                text = text.replace(before,after)
        if relative.endswith('/rpc_preparation.c'):
            anchor = 'ArcRpcPreparationState arc_riscpc_preparation_frame(ArcRpcPreparation *p)'
            assert text.count(anchor) == 1
            text = text.replace(anchor,'static ArcRpcPreparationState arc_options_frame_step(ArcRpcPreparation *p)')+PREPARATION_PROBE
        else:
            anchor = 'static ArcRpcFrontendOps arc_riscpc_host_ops(void);'
            assert text.count(anchor) == 1
            text = text.replace(anchor,'extern uint64_t arc_options_unit_clock(void *);\n'+anchor)
            text = text.replace('arc_riscpc_preparation_clock','arc_options_unit_clock')
            anchor = '.kind=arc_kind,.module={.attach=arc_rpc_attach'
            assert text.count(anchor) == 1
            text = text.replace(anchor,'.kind=arc_kind,.module={.clock=arc_options_unit_clock,.attach=arc_rpc_attach')+FRONTEND_PROBE
        source.write_text(text,encoding='utf-8')
        cc, self.link = rebuild_commands(self.commands,original,'arcdroid_rpcroute_fault_libretro',self.build,os.name=='nt')
        obj = self.temp/(original.stem+'.o')
        self.objects[cc[cc.index('-o')+1]] = str(obj)
        cc[cc.index('-o')+1] = str(obj); cc[cc.index('-c')+1] = str(source)
        cc = [arg for arg in cc if not arg.startswith('-DARC_BUILD_SUBSTITUTIONS=')]
        cc += ['-DARC_BUILD_SUBSTITUTIONS="authored-catalogue,test-faults,test-plans,read-only-options,unit-clock"']
        if '-MF' in cc: cc[cc.index('-MF')+1] = str(self.temp/(original.stem+'.d'))
        run = subprocess.run(cc,cwd=self.build,capture_output=True,text=True,timeout=120)
        if run.returncode: raise RuntimeError(run.stderr)

    def library(self):
        library = self.temp/('options.dll' if os.name=='nt' else 'options.dylib' if sys.platform=='darwin' else 'options.so')
        link = [next((new for old,new in self.objects.items() if arg.lower().endswith(('.o','.obj')) and same_path(arg,old,self.build,os.name=='nt')),arg) for arg in self.link]
        link[link.index('-o')+1] = str(library)
        link = ['-Wl,--out-implib,'+str(self.temp/'options.dll.a') if arg.startswith('-Wl,--out-implib,') else arg for arg in link]
        run = subprocess.run(link,cwd=self.build,capture_output=True,text=True,timeout=120)
        if run.returncode: raise RuntimeError(run.stderr)
        return library
