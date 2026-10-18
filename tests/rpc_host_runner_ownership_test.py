"""Native store holders and real retro_* runner contenders. GPL-2.0-only."""
from pathlib import Path
import json,os,queue,shutil,subprocess,sys,tempfile,threading
from rpc_ninja_commands import rebuild_commands,same_path

def holder(binary,save,content):
    child=subprocess.Popen([str(binary),str(save),'ownership-hold',content,'0'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    lines=queue.Queue();threading.Thread(target=lambda:lines.put(child.stdout.readline()),daemon=True).start()
    try:assert lines.get(timeout=30)=='held\n'
    except BaseException:child.kill();child.wait();raise
    return child

def release(child):
    try:
        child.stdin.write('release\n');child.stdin.flush();assert child.wait(timeout=30)==0,child.stderr.read()
    finally:
        if child.poll() is None:child.kill();child.wait()

def native_clients(binary,save):
    first=holder(binary,save,'c'*40);second=None
    try:
        contender=subprocess.run([str(binary),str(save),'ownership-try','c'*40,'0'],capture_output=True,text=True,timeout=30)
        assert contender.returncode==0 and contender.stdout=='contended\n',contender.stderr
        second=holder(binary,save,'b'*40)
    finally:
        if second:release(second)
        release(first)

def runner_clients(build,root,runner,binary,fixture):
    cache=(build/'CMakeCache.txt').read_text();ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
    commands=[row['command'] for row in json.loads(subprocess.check_output([ninja,'-C',str(build),'-t','compdb','-x'],text=True))]
    with tempfile.TemporaryDirectory(prefix='arc-host-runner-owners-') as directory:
        temp=Path(directory);source=root/'tests/arc_test_plan.c';copy=temp/source.name
        text=source.read_text();anchor='void arc_test_plan_set_host_ops(const ArcRpcFrontendOps *ops){host_ops=*ops;}'
        assert text.count(anchor)==1
        # The existing injectable clock makes W deterministic. Production
        # ownership, the host, preparation and every retro_* path are intact.
        text=text.replace(anchor,'static uint64_t owner_test_clock(void *opaque){(void)opaque;static uint64_t value;return value+=16666667;}\nvoid arc_test_plan_set_host_ops(const ArcRpcFrontendOps *ops){host_ops=*ops;host_ops.module.clock=owner_test_clock;}')
        copy.write_text(text);cc,link=rebuild_commands(commands,source,'arcdroid_rpcplan_libretro',build,os.name=='nt')
        obj=temp/'plan.o';old=cc[cc.index('-o')+1];cc[cc.index('-o')+1]=str(obj);cc[cc.index('-c')+1]=str(copy)
        if '-MF' in cc:cc[cc.index('-MF')+1]=str(temp/'plan.d')
        library=temp/('owners.dll' if os.name=='nt' else 'owners.dylib' if sys.platform=='darwin' else 'owners.so')
        link=[str(obj) if x.lower().endswith(('.o','.obj')) and same_path(x,old,build,os.name=='nt') else x for x in link];link[link.index('-o')+1]=str(library)
        link=['-Wl,--out-implib,'+str(temp/'owners.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in link]
        for command in (cc,link):
            result=subprocess.run(command,cwd=build,capture_output=True,text=True,timeout=90);assert result.returncode==0,result.stderr
        save=temp/'save';shutil.copytree(fixture/'save',save);native_clients(binary,save)
        child=holder(binary,save,'c'*40)
        owned_plan=temp/'owned.arcplan'
        owned_plan.write_text('\n'.join(f'firmware={fixture/"hd.rom"}' if line.startswith('firmware=') else line for line in (fixture/'hd.arcplan').read_text().splitlines() if not line.startswith(('media=','disk_control=')))+'\n')
        def persisted():return {str(p.relative_to(save)):p.read_bytes() for p in save.rglob('*') if p.is_file() and 'locks-riscpc' not in p.parts}
        def run(plan):return subprocess.run([str(runner),'--core',str(library),'--plan',str(plan),'--system',str(fixture),'--save',str(save),'--frames','1'],capture_output=True,text=True,timeout=120)
        try:
            before=persisted();result=run(owned_plan)
            assert result.returncode==1 and 'N-14:' in result.stderr,result.stdout+result.stderr
            assert persisted()==before
            seed=temp/'seed';seed.write_bytes(bytes(256));plan=temp/'other.arcplan'
            plan.write_text(f'version=2\nmodel=arm610\nfirmware={fixture/"hd.rom"}\ncmos=persist\nseed={seed}\ncmos_layout=ro370\ncontent_id={"b"*40}\nrtc_epoch=946684800\nhard_disc=on\n')
            result=run(plan);assert result.returncode==0 and 'N-14:' not in result.stderr,result.stdout+result.stderr
            assert all(persisted().get(name)==data for name,data in before.items())
        finally:release(child)
        result=run(owned_plan);assert result.returncode==0 and 'N-14:' not in result.stderr,result.stdout+result.stderr
        print('native two-process same/different title exclusion; real runner N-14 with zero store changes, independent same-pair title, clean holder release/reload passed')

if __name__=='__main__':
    runner_clients(*map(lambda value:Path(value).resolve(),sys.argv[1:]))
