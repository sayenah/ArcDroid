#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""R-e0-1: compare the endpoint-containing TEST_FAULTS core with a base build.

Check the preprocessed loop bodies and their local/inline helper closure, then
count emitted instructions and relocation references in each instantiation.
Exercise-begin must live in the shared wrapper, outside all three loops.
No disassembly, address or register masking is used for the source comparison.
"""
import collections
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from rpc_ninja_commands import split_command

LOOPS = tuple('arc_rpc_arm_exec_' + path for path in ('observed', 'cheap', 'unobserved'))
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\w+|[^\s]')


def body(source, name):
    match = re.search(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        return None
    tokens = TOKEN.findall(source[match.end():])
    depth = 1
    for i, token in enumerate(tokens):
        depth += (token == '{') - (token == '}')
        if not depth:
            return tuple(tokens[:i])
    raise RuntimeError('unclosed function ' + name)


def source_check(base, candidate):
    pending = list(LOOPS)
    checked = set()
    while pending:
        name = pending.pop()
        if name in checked:
            continue
        a, b = body(base, name), body(candidate, name)
        if a is None and b is None:
            continue  # external call; checked in the object relocations
        if a is None or b is None:
            raise RuntimeError('local helper set differs: ' + name)
        if a != b:
            raise RuntimeError('preprocessed instruction body/helper differs: ' + name)
        checked.add(name)
        pending.extend(a[i] for i in range(len(a)-1)
                       if re.fullmatch(r'[A-Za-z_]\w*', a[i]) and a[i+1] == '('
                       and a[i] not in ('if', 'for', 'while', 'switch', 'sizeof', '_Alignof'))
    if not set(LOOPS).issubset(checked) or 'arc_rpc_mem_batch_begin' not in checked:
        raise RuntimeError('missing interpreter bodies/helpers')
    wrapper = body(candidate, 'arc_rpc_arm_exec_batch')
    if wrapper is None or wrapper.count('arc_rpc_mem_exercise_begin') != 1:
        raise RuntimeError('expected exactly one common-wrapper exercise-begin call')
    if any(wrapper.index('arc_rpc_mem_exercise_begin') >= wrapper.index(name) for name in LOOPS):
        raise RuntimeError('exercise begin must precede loop dispatch')
    for name in LOOPS:
        tokens = body(candidate, name)
        begin = tokens.index('arc_rpc_mem_batch_begin')
        if begin >= tokens.index('for'):
            raise RuntimeError('batch begin moved into instruction loop: ' + name)
    return len(checked)


def preprocess(build):
    commands = json.loads((build / 'compile_commands.json').read_text())
    entry = next(c for c in commands if 'arcdroid_libretro_test_rpc.dir/' in c['command']
                 and c['file'].endswith('/arm.c'))
    argv = split_command(entry['command'], os.name == 'nt')
    if '-DARCDROID_TEST_FAULTS=1' not in argv or any('ARCDROID_RPC_MEASUREMENT' in x for x in argv):
        raise RuntimeError('loop comparison requires the endpoint-containing TEST_FAULTS core')
    obj = Path(entry['directory']) / argv[argv.index('-o')+1]
    index = argv.index('-o'); del argv[index:index+2]
    argv.remove('-c'); argv += ['-E', '-P']
    source = subprocess.check_output(argv, cwd=entry['directory'], text=True)
    return source, obj


def machine_loops(obj, objdump):
    output = subprocess.check_output([objdump, '-dr', '--no-show-raw-insn', str(obj)], text=True)
    result = {}
    for match in re.finditer(r'^([0-9a-f]+) <_?(arc_rpc_arm_exec_(?:observed|cheap|unobserved))>:\n(.*?)(?=^[0-9a-f]+ <|\Z)', output, re.M | re.S):
        _, name, text = match.groups()
        instructions = len(re.findall(r'^\s*[0-9a-f]+:\s+\t[a-z]', text, re.M))
        references = collections.Counter(re.findall(r'(?:RELOC_\w+|R_\w+|IMAGE_REL_\w+)\s+(?:\.refptr\.)?(_?arc_rpc_\w+)', text))
        references = collections.Counter({k.removeprefix('_'): v for k, v in references.items()})
        if not instructions or not references:
            raise RuntimeError('missing disassembly/relocations for ' + name)
        result[name] = instructions, references
    if set(result) != set(LOOPS):
        raise RuntimeError('missing three emitted TEST_FAULTS loops')
    return result


def compare(base, candidate, objdump):
    a, ao = preprocess(base); b, bo = preprocess(candidate)
    checked = source_check(a, b)
    am, bm = machine_loops(ao, objdump), machine_loops(bo, objdump)
    for name in LOOPS:
        old, old_refs = am[name]; new, new_refs = bm[name]
        if old_refs != new_refs:
            raise RuntimeError('unexpected loop symbol references: ' + name)
        if new > old:
            raise RuntimeError(f'emitted loop grew: {name}: {old} -> {new}')
        print(f'R-e0-1 TEST_FAULTS {name}: {old} -> {new} instructions; identical RPC symbol references')
    print(f'R-e0-1: three preprocessed loop bodies and {checked-3} reachable local/inline helpers equal the base; exercise admission stays in the common wrapper; no added per-instruction work')


def selftest():
    helpers = 'static void arc_rpc_mem_batch_begin(void *batch) { int x=0; }\n'
    loops = '\n'.join('static int '+name+'(void) { arc_rpc_mem_batch_begin(batch); for (;;) { step(); } }' for name in LOOPS)
    base = helpers + loops + '\nstatic void step(void) { value++; }'
    candidate = base + '\nint arc_rpc_arm_exec_batch(void) { arc_rpc_mem_exercise_begin(batch != 0); '+ ';'.join(name+'()' for name in LOOPS) + '; }'
    source_check(base, candidate)
    for old, new in (('value++;', 'value++; flag++;'),
                     ('step();', 'step(); extra();'),
                     ('arc_rpc_mem_batch_begin(batch); for (;;) {', 'for (;;) { arc_rpc_mem_batch_begin(batch);')):
        try:
            source_check(base, candidate.replace(old, new))
        except RuntimeError:
            continue
        raise RuntimeError('loop guard mutant survived: ' + new)
    print('TEST_FAULTS loop guard: added inline work, new call, per-instruction batch call rejected')


if __name__ == '__main__':
    if sys.argv[1:] == ['--self-test']:
        selftest()
    else:
        raise SystemExit('Run through tools/rpc_measure_compare.py (or --self-test)')
