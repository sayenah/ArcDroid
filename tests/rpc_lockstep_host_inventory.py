#!/usr/bin/env python3
"""Explicit preparation/host member census. GPL-2.0-only."""
import re
from rpc_launch_predicates import masked, end_at

SOURCES = ('libretro/rpc_preparation.c', 'libretro/rpc_preparation.h',
           'libretro/rpc_preparation_host.c', 'libretro/rpc_preparation_host.h',
           'libretro/rpc_cmos_store.h', 'libretro/rpc_launch_record_store.h',
           'libretro/rpc_hd_store.c', 'libretro/arc_lock.h')
OBJECTS = ('rpc_preparation', 'rpc_preparation_host', 'rpc_cmos_store',
           'rpc_launch_record_store', 'rpc_hd_store', 'arc_lock')
STATIC_SOURCES = ('libretro/arcdroid_libretro.c', 'tests/arc_test_plan.c')


def split(code, delimiter):
    depth = 0
    start = 0
    for i, char in enumerate(code):
        if char in '({[': depth += 1
        elif char in ')}]': depth -= 1
        elif char == delimiter and not depth:
            yield code[start:i]
            start = i + 1
    if code[start:].strip(): yield code[start:]


def members(text):
    """Keep complete declarations as well as names; no source hash/exclusion."""
    code = masked(text)
    code = re.sub(r'^\s*#.*$', '', code, flags=re.M)
    rows = {}
    for match in re.finditer(r'\b(typedef\s+)?struct\s*(\w+)?\s*\{', code):
        begin = code.index('{', match.start())
        end = end_at(code, begin, '{', '}')
        if match[1]:
            tail = re.match(r'\s*(\w+)\s*;', code[end+1:])
            if not tail: continue  # Anonymous member, owned by its enclosing declaration.
            typename = tail[1]
        else:
            typename = match[2]
        if not typename: continue
        fields = {}
        for declaration in split(code[begin+1:end], ';'):
            declaration = ' '.join(declaration.split())
            if not declaration: continue
            for part in split(declaration, ','):
                pointer = re.search(r'\(\s*\*\s*(\w+)\s*\)', part)
                name = pointer[1] if pointer else re.search(r'(\w+)\s*(?:\[[^]]*\]\s*)*$', part)[1]
                if name in fields: raise RuntimeError(('duplicate member', typename, name))
                fields[name] = declaration
        rows[typename] = fields
    return rows


def statics(text):
    code = masked(text)
    functions = []
    for match in re.finditer(r'^(?:static\s+|RETRO_API\s+)?[\w *]+\b(\w+)\([^;{}]*\)\s*\{', code, re.M):
        begin = code.index('{', match.start())
        functions.append((begin, end_at(code, begin, '{', '}'), match[1]))
    rows = {}
    for match in re.finditer(r'\bstatic\b', code):
        tail = code[match.start():]
        function = re.match(r'static\s+[\w\s*]+?\b\w+\s*\(', tail)
        if function: continue
        if not re.match(r'static\s+struct\s*\{', tail) and not re.match(r'static\s+[\w\s*]+?(?:\b\w+\s*(?=[\[,;=])|\(\s*\*\s*\w+\s*\))', tail):
            raise RuntimeError('unsupported static declaration; classify explicitly')
        depth = 0
        for end in range(match.start(), len(code)):
            char = code[end]
            if char in '({[': depth += 1
            elif char in ')}]': depth -= 1
            elif char == ';' and not depth: break
        else: raise RuntimeError('unterminated static declaration')
        declaration = ' '.join(code[match.start():end].split())
        owner = next((name for begin, finish, name in functions if begin < match.start() < finish), '')
        for part in split(declaration, ','):
            declarator = part.split('=', 1)[0]
            pointer = re.search(r'\(\s*\*\s*(\w+)\s*\)', declarator)
            name = pointer[1] if pointer else re.search(r'(\w+)\s*(?:\[[^]]*\]\s*)*$', declarator)[1]
            key = owner+'::'+name
            if key in rows: raise RuntimeError(('ambiguous static identity', key))
            rows[key] = declaration
    return rows


def check(schema, root):
    contract = schema['host_state_members']
    if set(contract) != set(SOURCES): raise RuntimeError('host member source census changed')
    total = new = 0
    for source in SOURCES:
        actual = members((root/source).read_text(encoding='utf-8'))
        expected = contract[source]
        if actual.keys() != expected.keys(): raise RuntimeError(('unclassified host type', source))
        for typename, fields in actual.items():
            if fields.keys() != expected[typename].keys():
                raise RuntimeError(('unclassified host member', source, typename,
                                    sorted(fields.keys() ^ expected[typename].keys())))
            for name, declaration in fields.items():
                rule = expected[typename][name]
                if rule['declaration'] != declaration:
                    raise RuntimeError(('host member declaration changed', source, typename, name))
                if not rule.get('reason') or not rule.get('citation'):
                    raise RuntimeError(('missing member rationale', source, typename, name))
                if rule['classification'] not in ('baseline M2b member', 'contents-compared',
                                                    'host-only/excluded', 'immutable-input identity'):
                    raise RuntimeError(('unclassified member', source, typename, name))
                if rule['classification'] == 'contents-compared' and not rule.get('fields'):
                    raise RuntimeError(('missing emitted field', source, typename, name))
                if not set(rule.get('fields', [])) <= {f['name'] for f in schema['fields']}:
                    raise RuntimeError(('unknown emitted field', source, typename, name))
                total += 1
                new += rule['classification'] != 'baseline M2b member'
    # Same parser used for real source: unknown scalar, pointer, array, nested
    # storage, callback, and changed capacity must all alter the census.
    probe = 'typedef struct { unsigned known; } Probe;'
    baseline = members(probe)
    for extra in ('unsigned novel;', 'void *novel;', 'char novel[3];',
                  'struct { unsigned value; } novel;', 'void (*novel)(void *);'):
        if members(probe.replace('unsigned known;', 'unsigned known;'+extra)) == baseline:
            raise RuntimeError(('host member control survived', extra))
    if members(probe.replace('unsigned known;', 'unsigned known[2];')) == baseline:
        raise RuntimeError('host capacity control survived')
    print(f'host member census: {total} explicit members, {new} new/changed classifications; six controls rejected')
    static_contract = schema['host_static_objects']
    if set(static_contract) != set(STATIC_SOURCES): raise RuntimeError('host static source census changed')
    count = added = 0
    for source in STATIC_SOURCES:
        actual = statics((root/source).read_text(encoding='utf-8'))
        expected = static_contract[source]
        if actual.keys() != expected.keys(): raise RuntimeError(('unclassified frontend static', source))
        for name, declaration in actual.items():
            rule = expected[name]
            if rule['declaration'] != declaration or not rule.get('reason') or not rule.get('citation'):
                raise RuntimeError(('changed/unclassified frontend static', source, name))
            if rule['classification'] not in ('baseline M2b object', 'contents-compared',
                                               'host-only/excluded', 'immutable-input identity'):
                raise RuntimeError(('unclassified frontend static', source, name))
            if not set(rule.get('fields', [])) <= {f['name'] for f in schema['fields']}:
                raise RuntimeError(('unknown frontend emitted field', source, name))
            count += 1
            added += rule['classification'] != 'baseline M2b object'
    baseline = statics('static unsigned known;')
    for extra in ('static unsigned novel;', 'static void *novel;',
                  'static char novel[3];', 'void f(void){static bool novel;}',
                  'static bool (*novel)(void *);'):
        if statics('static unsigned known;'+extra) == baseline:
            raise RuntimeError(('static census control survived', extra))
    print(f'frontend/plan static census: {count} explicit objects, {added} new/changed classifications; five controls rejected')
