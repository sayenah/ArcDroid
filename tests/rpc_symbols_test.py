#!/usr/bin/env python3
"""Reject real malformed objects with the backend symbol guard. GPL-2.0-only."""
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

sys.dont_write_bytecode = True
root = Path(sys.argv[1])
compiler = sys.argv[2]
nm = sys.argv[3]
spec = importlib.util.spec_from_file_location("symbol_guard", root / "tools/check-backend-symbols.py")
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)
policy = json.loads((root / "engine/backend-symbols.json").read_text())


def check(source, expected=None, flags=()):
    with tempfile.TemporaryDirectory(prefix="arcdroid-symbol-test-") as temp:
        directory = Path(temp)
        c = directory / "injected.c"
        c.write_text(source)
        subprocess.check_call([compiler, "-fno-common", *flags, "-c", str(c),
                               "-o", str(directory / "injected.o")])
        try:
            guard.verify([], [directory], policy, nm)
        except ValueError as error:
            assert expected is not None and expected in str(error), str(error)
            return
        assert expected is None, f"invalid object was accepted: {expected}"


check("int arc_rpc_data = 7; int arc_rpc_fn(void) { return arc_rpc_data; }")
check("int collision_data = 1;", "unprefixed RPCEmu definition")
check("int collision_fn(void) { return 1; }", "unprefixed RPCEmu definition")
check("extern void foreign(void); void arc_rpc_fn(void) { foreign(); }",
      "unexpected rpc reference")
check("extern void exit(int); void arc_rpc_fn(void) { exit(1); }",
      "unexpected rpc reference")
check("extern void abort(void); void arc_rpc_fn(void) { abort(); }",
      "unexpected rpc reference")
check("int arc_rpc_common;", "common symbol", ("-fcommon",))
check("__attribute__((weak)) int arc_rpc_weak = 1;", "weak external symbol")
check("__attribute__((weak)) void arc_rpc_fn(void) {}", "weak external symbol")
with tempfile.TemporaryDirectory(prefix="arcdroid-symbol-collision-") as temp:
    directory = Path(temp)
    for name in ("first", "second"):
        source = directory / (name + ".c")
        source.write_text("int arc_rpc_collision = 1;")
        subprocess.check_call([compiler, "-c", str(source), "-o", str(directory / (name + ".o"))])
    try:
        guard.verify([], [directory], policy, nm)
    except ValueError as error:
        assert "duplicate/colliding" in str(error)
    else:
        raise AssertionError("colliding definitions accepted")
print("rpc-symbol-guard: data/function/undefined/termination/common/weak/collision injections rejected")


def check_nm(output, engine, expected=None, sanitized=False, form="elf"):
    """Exercise GCC ELF output even when the host compiler emits Mach-O/COFF."""
    magic = {"elf": b"\x7fELF", "macho": b"\xcf\xfa\xed\xfe", "coff": b"\x64\x86\0\0"}
    with tempfile.TemporaryDirectory(prefix="arcdroid-symbol-nm-") as temp:
        directory = Path(temp)
        (directory / "injected.o").write_bytes(magic[form])
        with patch.object(guard.subprocess, "check_output", return_value=output), \
                contextlib.redirect_stdout(io.StringIO()):
            try:
                if engine == "outside":
                    guard.verify_outside([directory], policy, nm, sanitized)
                else:
                    guard.verify([directory] if engine == "arculator" else [],
                                 [directory] if engine == "rpc" else [], policy, nm, sanitized)
            except ValueError as error:
                assert expected is not None and expected in str(error), str(error)
                return
            assert expected is None, f"invalid nm output was accepted: {output}"


for engine in ("arculator", "rpc", "outside"):
    check_nm("_GLOBAL_OFFSET_TABLE_ U\n", engine)
    check_nm("_GLOBAL_OFFSET_TABLE_ U\n", engine, sanitized=True)
    check_nm("__odr_asan.debugon B 0 1\n", engine, sanitized=True)
    # GCC runtime families include versioned and abort-on-error entry points
    # beyond the exact Clang service names in the existing policy.
    check_nm("__asan_report_load4_noabort U\n__asan_version_mismatch_check_v8 U\n"
             "__ubsan_handle_type_mismatch_v1_abort U\n", engine, sanitized=True)

for engine in ("arculator", "rpc"):
    definition = "unexpected Arculator definition" if engine == "arculator" else "unprefixed RPCEmu definition"
    reference = f"unexpected {engine} reference"
    for output in ("__odr_asan.debugon B 0 1\n", "__asan_report_load4_noabort U\n",
                   "__ubsan_handle_type_mismatch_v1_abort U\n"):
        check_nm(output, engine, definition if " B " in output else reference)
    # Format, exact family spelling, and toolchain role are all necessary.
    for form in ("macho", "coff"):
        decoration = "_" if form == "macho" else ""
        check_nm(decoration + "_GLOBAL_OFFSET_TABLE_ U\n", engine, reference, form=form)
        check_nm(decoration + "__odr_asan.debugon B 0 1\n", engine, definition,
                 sanitized=True, form=form)
    for name in ("_GLOBAL_OFFSET_TABLE_", "__asan_engine", "__ubsan_engine",
                 "__odr_asan_debugon", "__odr_asan."):
        check_nm(f"{name} B 0 1\n", engine, definition, sanitized=True)
    for name in ("_GLOBAL_OFFSET_TABLE_engine", "__asanengine", "__ubsanengine",
                 "__asan_", "__ubsan_", "__odr_asan.debugon"):
        check_nm(f"{name} U\n", engine, reference, sanitized=True)
    for name in ("__odr_asan.debugon", "__asan_engine", "__ubsan_engine"):
        check_nm(f"{name} C 0 1\n", engine, "common symbol", sanitized=True)
        check_nm(f"{name} V 0 1\n", engine, "weak external symbol", sanitized=True)
    check_nm("__asan_engine T 0 4\n", engine, definition, sanitized=True)
    check_nm("collision_data B 0 1\n__odr_asan.collision_data B 0 1\n", engine,
             definition, sanitized=True)
check_nm("arc_rpc_foreign U\n", "arculator", "unexpected arculator reference", sanitized=True)
check_nm("debugon U\n", "rpc", "unexpected rpc reference", sanitized=True)
check_nm("arc_rpc_foreign U\n", "outside", "outside engine references RPCEmu", sanitized=True)
print("rpc-symbol-guard: ELF linker/GCC sanitizer roles accepted; mode/format/look-alike/common/weak/cross-engine violations rejected")

exports = (root / "libretro/arcdroid.exports").read_text().replace("_retro_", "retro_")
helper = ["sh", str(root / "tools/check-export-names.sh")]
subprocess.run(helper, input=exports, text=True, check=True)
for invalid in (exports.replace("retro_api_version", "retro_wrong_name"),
                exports + "arc_rpc_data\n", exports + "retro_run\n"):
    result = subprocess.run(helper, input=invalid, text=True, capture_output=True)
    assert result.returncode != 0, "non-exact exports were accepted"
print("rpc-symbol-guard: renamed, added data/weak and duplicate exports rejected")

# FORTIFY wrappers inherit only an allowed libc reference, never a definition
# or an arbitrary compiler service. Exercise glibc output on every host.
for engine in ('arculator', 'rpc'):
    reference = f'unexpected {engine} reference'
    definition = 'unexpected Arculator definition' if engine == 'arculator' else 'unprefixed RPCEmu definition'
    for name in policy[engine + '_services']:
        if name.startswith(('arc_', '__')):
            continue
        check_nm(f'__{name}_chk U\n', engine)
    for name in ('__printf_chk', '__snprintf_chk', '__longjmp_chk'):
        service = name[2:-4]
        check_nm(name+' U\n', engine, None if service in policy[engine+'_services'] else reference)
        for kind, expected in [('T', definition), ('C', 'common symbol'), ('W', 'weak external symbol')]:
            check_nm(f'{name} {kind} 0 1\n', engine, expected)
    for name in ('__foreign_chk', '__printf_chk_extra', '__printf_chk_chk',
                 '__arc_input_consume_chk', '__exit_chk', '__abort_chk'):
        check_nm(name+' U\n', engine, reference)
    # A service allowed only in Arculator cannot leak into RPC through FORTIFY.
    if engine == 'rpc':
        check_nm('__strcpy_chk U\n__sprintf_chk U\n', engine, reference)

# Collect independent violations across objects, both engines, and outside
# objects, including malformed symbol roles in the same object as references.
with tempfile.TemporaryDirectory(prefix='arc-symbol-all-') as temp:
    base = Path(temp)
    for directory in ['arculator', 'rpc', 'outside']:
        (base/directory).mkdir()
        (base/directory/'bad.o').write_bytes(b'\x7fELF')
    outputs = {
        'arculator': 'arc_rpc_cross U\nforeign_a U\n',
        'rpc': 'bad_definition T 0 4\narc_rpc_common C 0 4\narc_rpc_weak W 0 4\nforeign_b U\nforeign_c U\n',
        'outside': 'arc_rpc_private U\n',
    }
    def nm_result(argv, **kwargs):
        return outputs[Path(argv[-1]).parent.name]
    with patch.object(guard.subprocess, 'check_output', side_effect=nm_result):
        errors = []
        guard.verify([base/'arculator'], [base/'rpc'], policy, nm, errors=errors)
        guard.verify_outside([base/'outside'], policy, nm, errors=errors)
        for fragment in ['arc_rpc_cross', 'foreign_a', 'bad_definition', 'arc_rpc_common',
                         'arc_rpc_weak', 'foreign_b', 'foreign_c', 'arc_rpc_private']:
            assert any(fragment in error for error in errors), (fragment, errors)
        try:
            guard.finish(errors)
        except ValueError as error:
            assert all(fragment in str(error) for fragment in ['foreign_a', 'foreign_b', 'arc_rpc_private'])
        else:
            raise AssertionError('collected violations passed')
print('rpc-symbol-guard: allowed FORTIFY libc families and complete multi-object diagnostics passed')

# Bionic lowers allowed Arculator sprintf through its variadic libc service.
check_nm('__vsprintf_chk U\n', 'arculator')
check_nm('__vsprintf_chk U\n', 'rpc', 'unexpected rpc reference')

# Shared observers execute on RPC's guest route even though their objects live
# outside the private rpc directory. None may terminate the host process.
for name in ('arc_error_box', 'arc_disc_request', 'arc_launch_outcome', 'arc_boot_watch'):
    with tempfile.TemporaryDirectory(prefix='arc-shared-symbol-') as temp:
        directory=Path(temp);(directory/'engine').mkdir()
        source=directory/'injected.c';source.write_text('extern void abort(void); void probe(void) { abort(); }\n')
        obj=directory/'engine'/(name+'.c.o')
        subprocess.check_call([compiler,'-c',str(source),'-o',str(obj)])
        try: guard.verify_outside([directory],policy,nm)
        except ValueError as error: assert 'process termination reference' in str(error)
        else: raise AssertionError((name,'shared termination was accepted'))
print('rpc-symbol-guard: process termination rejected in all four shared observers')

# Census import policy is independent of the ordinary-core service policy.
# Compile real platform objects (also run with the MinGW compiler), including
# a Windows-only allocator/import-pointer route and QA's POSIX realpath escape.
def census_check(sources, rejected=(), sanitized=False, flags=()):
    with tempfile.TemporaryDirectory(prefix='arc-census-symbols-') as temp:
        directory = Path(temp)
        for name, source in sources.items():
            c = directory / (name + '.c')
            c.write_text(source)
            subprocess.run([compiler, '-fno-common', *flags, '-c', str(c),
                            '-o', str(directory / (name + '.c.o'))], check=True)
        errors = []
        guard.verify_census([directory], nm, sanitized, errors, policy)
        if rejected:
            assert all(any(name in e and 'allocation census bypass:' in e for e in errors)
                       for name in rejected), errors
        else:
            assert not errors, errors

census_check({'caller': 'extern void internal(void); void call(void) { internal(); }',
              'callee': 'void internal(void) {}'})
census_check({'caller': 'extern void arc_rpc_missing(void); void call(void) { arc_rpc_missing(); }'},
             ('arc_rpc_missing',))
for allocator in ('malloc', 'calloc', 'realloc', 'free'):
    source = '#include <stdlib.h>\nvoid *p; void probe(void) { ' + {
        'malloc': 'p = malloc(1);', 'calloc': 'p = calloc(1,1);',
        'realloc': 'p = realloc(p,1);', 'free': 'free(p);'}[allocator] + ' }'
    census_check({'caller': source}, (allocator,))
    census_check({'arc_alloc_census': source})
    census_check({'arc_alloc_census_extra': source}, (allocator,))
# The bridge exemption is restricted to the four instrumented primitives.
for allocator in ('realpath', 'strdup', 'asprintf', 'HeapAlloc', '_strdup'):
    census_check({'arc_alloc_census': 'extern void ' + allocator + '(void); '
                  'void probe(void) { ' + allocator + '(); }'}, (allocator,))
census_check({'platform': '#ifdef _WIN32\n'
              '__declspec(dllimport) char *_strdup(const char *);\n'
              'char *probe(void) { return _strdup("leak"); }\n'
              '#else\n#include <stdlib.h>\n'
              'char *probe(void) { return realpath("/", 0); }\n#endif\n'},
             ('_strdup' if 'mingw' in compiler.lower() or sys.platform == 'win32' else 'realpath',))
census_check({'caller': 'extern void __asan_report_load1(void); '
              'void probe(void) { __asan_report_load1(); }'}, ('__asan_report_load1',))
census_check({'caller': 'extern void __asan_report_load1(void); '
              'void probe(void) { __asan_report_load1(); }'}, sanitized=True)
print('rpc-symbol-guard: census allowlist rejects unknown/internal-prefix/allocator/bridge-lookalike imports; realpath or Windows _strdup rejected')

# The census must retain ELF sanitizer imports for its finite allowlist even
# though the ordinary backend checker accepts compiler runtime families.
with tempfile.TemporaryDirectory(prefix='arc-census-elf-') as temp:
    directory = Path(temp); (directory / 'probe.o').write_bytes(b'\x7fELF')
    for name, accepted in [('__asan_report_load4_noabort', True),
                           ('__ubsan_handle_type_mismatch_v1_abort', True),
                           ('__asan_unknown_allocator', False),
                           ('__ubsan_unknown_allocator', False),
                           ('__realpath_chk', False), ('__memcpy_chk', True)]:
        with patch.object(guard.subprocess, 'check_output', return_value=name + ' U\n'):
            errors = []
            guard.verify_census([directory], nm, True, errors, policy)
            assert bool(errors) != accepted, (name, errors)
print('rpc-symbol-guard: census ELF sanitizer and fortified imports checked against finite service names')

# Clang copied-DSO instrumentation: bookkeeping never admits the referent.
for form,decoration,kind in [('elf','','B'),('macho','_','S')]:
    check_nm(decoration+'arc_rpc_data D\n'+decoration+'__odr_asan_gen_arc_rpc_data '+kind+'\n','rpc',sanitized=True,form=form)
    check_nm(decoration+'__odr_asan_gen_arc_rpc_absent '+kind+'\n','rpc','ODR indicator without local data referent',sanitized=True,form=form)
    check_nm(decoration+'collision D\n'+decoration+'__odr_asan_gen_collision '+kind+'\n','rpc','unprefixed RPCEmu definition',sanitized=True,form=form)
    check_nm(decoration+'arc_rpc_data D\n'+decoration+'__odr_asan_gen_arc_rpc_data T\n','rpc','unprefixed RPCEmu definition',sanitized=True,form=form)
    check_nm(decoration+'arc_rpc_data D\n'+decoration+'__odr_asan_gen_arc_rpc_data '+kind+'\n','rpc','unprefixed RPCEmu definition',sanitized=False,form=form)
print('rpc-symbol-guard: Clang ODR indicators bind to locally defined, independently checked data; orphan/function/non-sanitized lookalikes rejected')
