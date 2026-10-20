"""AY-1 calendar and L-1 checksum-preservation mutants. GPL-2.0-only."""
from pathlib import Path
import subprocess, sys, tempfile

compiler, root = sys.argv[1:]
root = Path(root).resolve()
device = root/'engine/arculator/src/cmos.c'
store = root/'libretro/rpc_cmos_store.c'
cases = [
    ('RTC year bits', device, ' | ((systemtime.year & 3) << 6)', ''),
    ('full year', device, 'cmos.ram[0xc0] = year ? year[0] : (uint8_t)(systemtime.year % 100);', ''),
    ('century byte', device, 'cmos.ram[0xc1] = year ? year[1] : (uint8_t)(systemtime.year / 100);', ''),
    ('live checksum', device, 'cmos.ram[63] = (uint8_t)sum;', '(void)sum;'),
    ('guest-only persistence', device, 'if (!data || !year_overlay) return;', 'return;'),
    ('invalid checksum healing', store, 'p[47]=(uint8_t)(p[47]+p[176]+p[177]-old);',
     'unsigned sum=1;for(unsigned i=0;i<240;++i)if(i!=47)sum+=p[i];p[47]=(uint8_t)sum;'),
]
with tempfile.TemporaryDirectory(prefix='arc-calendar-mutants-') as temp:
    temp = Path(temp)
    for source in (device, store):
        selected = [('baseline', source, None, None)] + [c for c in cases if c[1] == source]
        for label, _, old, new in selected:
            text = source.read_text(encoding='utf-8')
            if old is not None:
                assert text.count(old) == 1, label
                text = text.replace(old, new)
            path = temp/source.name; path.write_text(text,encoding='utf-8')
            if source == device:
                test = root/'tests/cmos_device_tests.c'
                sources = [test, path]
            else:
                test = root/'tests/rpc_cmos_store_test.c'
                sources = [test, path, root/'libretro/arc_io.c', root/'libretro/arc_lock.c', root/'core/hash.c']
            binary = temp/'test'
            cmd = [compiler, '-std=c11', '-O2', '-UNDEBUG', '-DARCDROID_TEST_FAULTS=1',
                   *[f'-I{root/p}' for p in ('core','engine','engine/compat','engine/arculator/src','libretro','libretro/include','tests')],
                   *map(str,sources), '-o', str(binary)]
            built = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
            assert built.returncode == 0, (label,built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            if old is None:
                assert run.returncode == 0, run.stderr
            else:
                assert run.returncode != 0, (label,'survived')
                print('calendar mutant killed: '+label, flush=True)
