"""Exercise session-start/reset wiring mutants in the real test core. GPL-2.0-only."""
from pathlib import Path
import json, os, subprocess, sys, tempfile
from rpc_ninja_commands import rebuild_commands, split_command, same_path

build, root, exe, launch_exe, fixtures = map(lambda s: Path(s).resolve(), sys.argv[1:])
ninja = next(s.split('=',1)[1] for s in (build/'CMakeCache.txt').read_text(encoding='utf-8').splitlines()
             if s.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands = [r['command'] for r in json.loads(subprocess.check_output([ninja,'-C',str(build),'-t','compdb','-x'],text=True,encoding='utf-8'))]
source = root/'engine/arculator_backend.c'
rows = [split_command(c,os.name=='nt') for c in commands if ' -c ' in c]
compile = next(r for r in rows if same_path(r[r.index('-c')+1],source,build,os.name=='nt')
               and 'arcdroid_libretro_test_arculator.dir/' in r[r.index('-o')+1].replace('\\','/'))
frontend_compile, original_link = rebuild_commands(commands,root/'libretro/arcdroid_libretro.c',
                                    'arcdroid_libretro_test',build,os.name=='nt')
original = source.read_text(encoding='utf-8')
start = original.index('   cmos_session_year(arc_cmos_uses_year(os)')
end = original.index(';', start)+1
cases = [('baseline',original),
         ('skip session patch',original[:start]+'   cmos_session_year(0,NULL);'+original[end:]),
         ('patch again on reset',original.replace('return arc_machine_create(failure, failure_size);',
          'if(!arc_machine_create(failure,failure_size))return false;cmos_session_year(1,NULL);return true;'))]
cases=[(label,text,compile,False) for label,text in cases]
frontend=(root/'libretro/arcdroid_libretro.c').read_text(encoding='utf-8')
anchor='arc_engine_cmos_resume_year(arc_cmos_session_year_valid?arc_cmos_session_year:NULL);'
assert frontend.count(anchor)==1
construction_anchor='   arc_cmos_session_year_valid=arc_engine_cmos_year(arc_cmos_session_year);\n   bool riscos3'
assert frontend.count(construction_anchor)==1
cases.extend([
    ('frontend baseline',frontend,frontend_compile,True),
    ('discard live year during frontend reconstruction',frontend.replace(anchor,'arc_engine_cmos_resume_year(NULL);'),frontend_compile,True),
    ('carry year into next load',frontend.replace('   arc_cmos_session_year_valid = false;',''),frontend_compile,True),
    ('skip construction year cache refresh',frontend.replace(construction_anchor,'   bool riscos3'),frontend_compile,True),
])
with tempfile.TemporaryDirectory(prefix='arc-calendar-wiring-') as temp:
    temp = Path(temp)
    for label,text,compile,frontend_case in cases:
        path=temp/('arcdroid_libretro.c' if frontend_case else 'arculator_backend.c');path.write_text(text,encoding='utf-8');obj=temp/'calendar.o'
        cc=compile.copy();cc[cc.index('-c')+1]=str(path);cc[cc.index('-o')+1]=str(obj)
        if '-MF' in cc: cc[cc.index('-MF')+1]=str(temp/'calendar.d')
        built=subprocess.run(cc,cwd=build,capture_output=True,text=True,timeout=90)
        assert built.returncode==0,(label,built.stderr)
        link=original_link.copy();library=temp/('calendar.dll' if os.name=='nt' else 'calendar.dylib' if sys.platform=='darwin' else 'calendar.so')
        link[link.index('-o')+1]=str(library)
        old=compile[compile.index('-o')+1]
        link=[str(obj) if a.lower().endswith(('.o','.obj')) and same_path(a,old,build,os.name=='nt') else a for a in link]
        link=['-Wl,--out-implib,'+str(temp/'calendar.dll.a') if a.startswith('-Wl,--out-implib,') else a for a in link]
        linked=subprocess.run(link,cwd=build,capture_output=True,text=True,timeout=90)
        assert linked.returncode==0,(label,linked.stderr)
        command=([sys.executable,'-B',str(root/'tests/calendar_launch_test.py'),str(launch_exe),str(library),str(fixtures)] if frontend_case else
                 [sys.executable,'-B',str(root/'tests/arc_calendar_fixture.py'),str(exe),str(library)])
        run=subprocess.run(command,capture_output=True,text=True,timeout=180)
        if label in ('baseline','frontend baseline'):assert run.returncode==0,run.stdout+run.stderr
        else:
            assert run.returncode!=0,(label,'survived')
            if label=='skip construction year cache refresh':
                assert 'live[192]+100u*live[193]==base' in run.stderr,run.stdout+run.stderr
            print('calendar wiring mutant killed: '+label,flush=True)
