"""Real OS owners, deterministic VFS kill boundaries, same save tree. GPL-2.0-only."""
from pathlib import Path
import queue,shutil,struct,subprocess,sys,tempfile,threading
from rpc_boot_packages_test import inputs
from rpc_cmos_factory_rom import generate,factory
from rpc_save_snapshot import save_snapshot
exe=Path(sys.argv[1]).resolve()
class Child:
    def __init__(self,args):
        self.p=subprocess.Popen(list(map(str,args)),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
        self.q=queue.Queue();self.output=[]
        def read():
            for line in self.p.stdout:self.output.append(line);self.q.put(line.strip())
            self.q.put(None)
        threading.Thread(target=read,daemon=True).start()
    def point(self,name):
        while True:
            line=self.q.get(timeout=45)
            assert line is not None,''.join(self.output)
            if line==f'point:{name}':return
    def resume(self):self.p.stdin.write('continue\n');self.p.stdin.flush()
    def finish(self):
        result=self.p.wait(timeout=120);assert result==0,''.join(self.output)
        return ''.join(self.output)
    def kill(self):self.p.kill();self.p.wait(timeout=30);assert self.p.returncode!=0
    def close(self):
        if self.p.poll() is None:self.kill()
with tempfile.TemporaryDirectory(prefix='arc-r3d3-process-') as temporary:
    root=Path(temporary);system=inputs(root);roms=generate(root/'roms')
    # Preserve every factory write and the 45 s oracle, then use SA-110's
    # authored idle instruction. Busy-loop execution dominates ASan latency
    # without adding process-lock or defaults-publication coverage.
    words=factory('full').words;idle=max(i for i,w in enumerate(words) if w==0xeafffffe);words[idle]=0xee0f0f58
    (roms/'full.rom').write_bytes(struct.pack('<%dI'%len(words),*words)+struct.pack('<I',0xeafffffe)*(524288-len(words)))
    bad=bytearray((roms/'full.rom').read_bytes());struct.pack_into('<5I',bad,0,0xe3a00070,0xee010f10,0xe3a00014,0xe121f000,0xeafffffe)
    authored=root/'fault.rom';authored.write_bytes(bad)
    title='a'*40;other='b'*40
    def args(save,mode='run',point='none',content=title,rom=authored):
        return [exe,rom,system,save,mode,point,content]
    def run(save,**kw):
        r=subprocess.run(list(map(str,args(save,**kw))),capture_output=True,text=True,timeout=120)
        assert r.returncode==0,r.stdout+r.stderr
        return r.stdout
    baseline=root/'baseline';baseline.mkdir();run(baseline)
    def files(save):return save_snapshot(save)
    for point in ('lock','cmos','chunk','manifest','observation','drain','release'):
        save=root/point;shutil.copytree(baseline,save)
        child=Child(args(save,'fault' if point=='drain' else 'run',point))
        try:child.point(point);child.kill()
        finally:child.close()
        interrupted_temps={str(p.relative_to(save)):p.read_bytes() for p in save.rglob('*.tmp')}
        result=run(save)
        assert 'acquired: attempts=1' in result,result
        # The CMOS transaction preserves an unpublished candidate until the
        # next changed payload. It never promotes a .tmp during recovery.
        for p in save.rglob('*.tmp'):
            assert '/cmos-riscpc/' in p.as_posix() and p.read_bytes()==interrupted_temps[str(p.relative_to(save))],point
        print(f'kill/reap/reload: {point}; first native attempt; intact publication, no false CORRUPT or sticky refusal',flush=True)
    save=root/'same-title';shutil.copytree(baseline,save)
    child=Child(args(save,point='running'))
    try:
        child.point('running');before=files(save)
        assert 'N-14:' in run(save,mode='contender');assert files(save)==before
        child.resume();child.finish();run(save)
    finally:child.close()
    print('same title: suspended native owner retained, contender N-14 with zero store changes, clean reacquisition',flush=True)
    save=root/'two-titles';save.mkdir()
    first=Child(args(save,'generate','defaults',rom=roms/'full.rom'))
    second=None
    try:
        first.point('defaults')
        before_defaults={str(p.relative_to(save)):p.read_bytes() for p in (save/'arcdroid/cmos-riscpc/defaults').rglob('*') if p.is_file()}
        assert 'N-16:' in run(save,mode='defaults-contender',content='c'*40,rom=roms/'full.rom')
        assert before_defaults=={str(p.relative_to(save)):p.read_bytes() for p in (save/'arcdroid/cmos-riscpc/defaults').rglob('*') if p.is_file()}
        second=Child(args(save,'generate','defaults-wait',other,roms/'full.rom'));second.point('defaults-wait')
        first.resume();first.finish();second.resume();second.finish()
        defaults=list((save/'arcdroid/cmos-riscpc/defaults').glob('*.1'));assert len(defaults)==1
        payload=defaults[0].read_bytes()[104:344]
        records=list((save/'arcdroid/cmos-riscpc').glob('*.1'));assert len(records)==2
        assert all(p.read_bytes()[104:344]==payload for p in records)
    finally:
        first.close()
        if second:second.close()
    print('two titles: shared defaults contender adopts first publication, both payloads identical',flush=True)
