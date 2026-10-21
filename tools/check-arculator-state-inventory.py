#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check the reviewed SS-1 storage declarations and allocation sites offline.

This is a declaration-surface guard, independent of optimiser dead stripping.
It deliberately checks inactive preprocessor arms too. It never approves a
new declaration or allocation automatically. See docs/arculator-save-states.md.
"""
import argparse
import difflib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
                   r'[A-Za-z_]\w*|0[xX][\da-fA-F]+|\d+(?:\.\d+)?\w*|\S', re.S)
ALLOCATORS = {'malloc', 'calloc', 'realloc', 'create_bitmap', 'arc_io_open_file',
              'arc_io_read_all', 'arc_io_read_all_limit',
              'strdup', 'strndup', 'wcsdup', 'aligned_alloc', 'posix_memalign',
              'memalign', 'valloc', 'pvalloc', 'reallocarray', 'recallocarray',
              '_strdup', '_wcsdup', '_aligned_malloc', '_aligned_realloc',
              'mmap', 'VirtualAlloc', 'VirtualAllocEx', 'HeapAlloc', 'HeapReAlloc',
              'MapViewOfFile', 'MapViewOfFileEx',
              'dlopen', 'LoadLibraryA', 'LoadLibraryW', 'LoadLibraryExA', 'LoadLibraryExW',
              'GetModuleHandleExW'}
CLASSES = {'serialized', 'derived-on-load', 'host-only'}


def tokens(text):
    text = re.sub(r'#ifdef __cplusplus\s+extern "C"\s*\{\s*#endif', '', text)
    text = re.sub(r'#ifdef __cplusplus\s+}\s*#endif', '', text)
    # Keep every arm, removing directive syntax and multiline macro bodies.
    text = re.sub(r'^\s*#(?:[^\n]*\\\n)*[^\n]*', '', text, flags=re.M)
    return [m.group() for m in TOKEN.finditer(text)
            if not m.group().startswith(('//', '/*'))]


def closing(ts, start):
    pairs = {'(': ')', '[': ']', '{': '}'}
    stack = []
    for i in range(start, len(ts)):
        if ts[i] in pairs:
            stack.append(pairs[ts[i]])
        elif ts[i] in pairs.values():
            if not stack or stack.pop() != ts[i]:
                raise ValueError('unbalanced declaration delimiters')
            if not stack:
                return i
    raise ValueError('unterminated declaration')


def statement_end(ts, start):
    i = start
    while i < len(ts):
        if ts[i] == ';':
            return i
        if ts[i] in ('(', '[', '{'):
            i = closing(ts, i)
        i += 1
    raise ValueError('unterminated storage declaration')


def census(text):
    ts = tokens(text)
    # A macro can introduce a declaration at an otherwise unchanged call site.
    # Guard the directive bodies as well as their unexpanded declarations.
    directives = re.findall(r'^\s*#(?:[^\n]*\\\n)*[^\n]*', text, re.M)
    rows = [('preprocessor', ' '.join(t for t in TOKEN.findall(d.replace('\\\n', ' '))
                                     if not t.startswith(('//', '/*')))) for d in directives]
    start = i = 0
    while i < len(ts):
        t = ts[i]
        if t in ('(', '['):
            i = closing(ts, i) + 1
            continue
        if t == '{':
            prefix = ts[start:i]
            end = closing(ts, i)
            if ')' in prefix and '=' not in prefix:
                # A function definition, rather than an aggregate initializer.
                p = prefix.index('(')
                owner = prefix[p - 1]
                j = i + 1
                while j < end:
                    local_record = (ts[j] in {'struct', 'union', 'enum'} and
                                    (ts[j + 1:j + 2] == ['{'] or
                                     ts[j + 2:j + 3] == ['{']))
                    if ts[j] == 'typedef' or local_record:
                        k = statement_end(ts, j)
                        rows.append(('local-layout:' + owner, ' '.join(ts[j:k + 1])))
                    if ts[j] == 'static':
                        k = statement_end(ts, j)
                        rows.append(('local:' + owner, ' '.join(ts[j:k + 1])))
                        j = k + 1
                        continue
                    if ts[j] in ALLOCATORS:
                        k = closing(ts, j + 1) if ts[j + 1:j + 2] == ['('] else j
                        rows.append(('allocation:' + owner, ' '.join(ts[j:k + 1])))
                    j += 1
                i = start = end + 1
                continue
            i = end + 1
            continue
        if t == ';':
            decl = ts[start:i]
            if decl:
                # Typedefs/records are checked as well: changing an aggregate
                # can add live storage without adding a new root variable.
                kind = 'layout' if decl[0] == 'typedef' else 'declaration'
                # Keep prototypes/extern declarations too: attributes, array
                # bounds and function-pointer declarators contain parentheses,
                # and an extern declaration with an initializer defines storage.
                # Conservatively guarding these avoids a C-parser heuristic
                # silently exempting a real object.
                rows.append((kind, ' '.join(decl) + ' ;'))
            i += 1
            start = i
            continue
        i += 1
    if ts[start:]:
        raise ValueError('unrecognised top-level declaration: ' + ' '.join(ts[start:start + 12]))
    return [{'kind': kind, 'declaration': decl} for kind, decl in rows]


def sources(root):
    cmake = (root / 'CMakeLists.txt').read_text()
    names = set()
    for group in ('ARCDROID_ARCULATOR_SOURCES', 'ARCDROID_LIBRETRO_SOURCES', 'ARCDROID_CORE_SOURCES'):
        match = re.search(r'set\(' + group + r'\s+(.*?)\n\)', cmake, re.S)
        if not match:
            raise ValueError('missing production source group: ' + group)
        names.update(re.findall(r'[\w/]+\.c\b', match[1]))
    names.update(('core/adffs_profiles.c', 'core/firmware_table.c'))
    # Follow repository headers, including shared structures with owned heaps.
    todo = list(names)
    include_dirs = ('', 'core', 'engine', 'platform', 'libretro',
                    'engine/compat', 'engine/arculator/src', 'libretro/include', 'third_party/zlib', 'tests')
    while todo:
        name = todo.pop()
        path = root / name
        if not path.is_file():
            raise ValueError('missing source: ' + name)
        for header in re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(), re.M):
            candidates = [path.parent / header] + [root / d / header for d in include_dirs]
            found = next((p for p in candidates if p.is_file()), None)
            if found is None:
                # Build-generated declarations have no writable session roots.
                if header in ('arc_build_identity.h', 'cmos_defaults_data.h', 'arc_test_plan.h',
                              'engine_test.h', 'rpc_test_clock.h'):
                    continue
                raise ValueError(f'{name}: unresolved include {header}')
            relative = found.resolve().relative_to(root.resolve()).as_posix()
            if relative.startswith(('engine/rpc/', 'engine/rpcemu/', 'tests/', 'third_party/')):
                continue  # RPC state is SS-2; external code has no retained SS-1 heap.
            if relative not in names:
                names.add(relative)
                todo.append(relative)
    return sorted(names)


def cmake_source_commands(root):
    text = (root / 'CMakeLists.txt').read_text()
    # Check source-attachment commands outside the three normal lists too.
    # This includes generated/conditional sources without depending on a host
    # compiler or silently accepting a new target_sources route.
    ts = [m.group() for m in re.finditer(r'#[^\n]*|"(?:\\.|[^"\\])*"|[\w]+|\S', text)
          if not m.group().startswith('#')]
    result = []
    for i, token in enumerate(ts[:-1]):
        if ts[i + 1] != '(':
            continue
        if (token in {'add_library', 'add_subdirectory', 'target_sources', 'target_link_libraries', 'list'} or
            (token == 'file' and ts[i + 2] in {'GLOB', 'GLOB_RECURSE'}) or
            (token == 'set' and ts[i + 2] in {'ARCDROID_CORE_SOURCES',
                'ARCDROID_ARCULATOR_SOURCES', 'ARCDROID_LIBRETRO_SOURCES'})):
            result.append(' '.join(ts[i:closing(ts, i + 1) + 1]))
    return result


def verify(root, manifest):
    if manifest.get('version') != 1:
        raise ValueError('unsupported inventory schema')
    if cmake_source_commands(root) != manifest['cmake_source_commands']:
        raise ValueError('unreviewed CMake source attachment')
    expected = manifest['surfaces']
    names = sources(root)
    if set(names) != set(expected):
        raise ValueError(f'uncensused/removed source: {sorted(set(names) ^ set(expected))}')
    total = 0
    for name in names:
        current = census((root / name).read_text())
        saved = expected[name]
        surface = [{k: r[k] for k in ('kind', 'declaration')} for r in saved]
        if current != surface:
            before = [r['kind'] + ': ' + r['declaration'] + '\n' for r in saved]
            after = [r['kind'] + ': ' + r['declaration'] + '\n' for r in current]
            diff = ''.join(difflib.unified_diff(before, after, fromfile=name + ':reviewed', tofile=name + ':live'))
            raise ValueError('unreviewed storage/allocation surface\n' + diff[:6000])
        total += len(current)
        for row in saved:
            if row['classification'] not in CLASSES or not row.get('reason'):
                raise ValueError('unclassified declaration surface: ' + name)
    for row in manifest['objects'] + manifest['reachable_storage'] + manifest['allocation_review']:
        if row['classification'] not in CLASSES or not row.get('reason'):
            raise ValueError('unclassified object: ' + str(row))
    actual_allocations = [(s, r['kind'].split(':', 1)[1], r['declaration'])
                          for s, rows in expected.items() for r in rows
                          if r['kind'].startswith('allocation:')]
    reviewed_allocations = [(r['source'], r['owner'], r['call'])
                            for r in manifest['allocation_review']]
    if actual_allocations != reviewed_allocations:
        raise ValueError('allocation site lacks an ownership review')
    identities = [(r['source'], r['symbol']) for r in manifest['objects']]
    if len(identities) != len(set(identities)):
        raise ValueError('duplicate static object identity')
    # Declaration coverage alone cannot detect a removed codec in a function
    # body. Every serialized generated owner also needs its object selection
    # and a payload visitor before the next selection. arc_state_io's five
    # queue roots use the handwritten, bounded io.pending blob instead.
    visitors = {}
    for name in names:
        if name.startswith('engine/state-generated/') and name.endswith('.inc'):
            source = ' '.join(tokens((root / name).read_text()))
            selections = list(re.finditer(r'arc_state_object \( s , "([^"]+)" \)', source))
            for i, selection in enumerate(selections):
                end = selections[i + 1].start() if i + 1 < len(selections) else len(source)
                body = source[selection.end():end]
                if re.search(r'arc_state_(?:value|boolean|reference|typed_reference|memory_reference|'
                             r'string_bounded|raw_cache|media_entry_values|raster|heap|blob)\w* \(', body):
                    visitors[selection[1]] = True
    for row in manifest['objects']:
        if row['classification'] == 'serialized' and row['source'] != 'libretro/arc_state_io.c':
            identity = row['source'] + ':' + row['symbol']
            if identity not in visitors:
                raise ValueError('serialized object lacks a generated payload visitor: ' + identity)
    return len(names), total


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=ROOT)
    a = p.parse_args()
    try:
        manifest = json.loads((a.root / 'tests/arculator_state_inventory.json').read_text())
        count, declarations = verify(a.root, manifest)
        print(f'SS-1 inventory: {count} sources/headers; {declarations} reviewed declaration/allocation surfaces; '
              f'{len(manifest["objects"])} static objects; {len(manifest["reachable_storage"])} reachable storage rows')
    except (OSError, ValueError, KeyError) as error:
        print('SS-1 inventory guard: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
