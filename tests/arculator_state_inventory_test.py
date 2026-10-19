#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""SS-1 inventory mutants operate on a disposable source closure."""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('ss1_inventory', ROOT / 'tools/check-arculator-state-inventory.py')
GUARD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GUARD)


class InventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((ROOT / 'tests/arculator_state_inventory.json').read_text())

    def mutate(self, source, change):
        with tempfile.TemporaryDirectory(prefix='arcdroid-ss1-inventory-') as directory:
            root = Path(directory)
            for name in ['CMakeLists.txt', *self.manifest['surfaces']]:
                dest = root / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / name, dest)
            path = root / source
            original = path.read_text()
            updated = change(original)
            self.assertNotEqual(original, updated)
            path.write_text(updated)
            with self.assertRaises(ValueError):
                GUARD.verify(root, self.manifest)

    def test_live_tree(self):
        GUARD.verify(ROOT, self.manifest)

    def test_unused_writable_globals(self):
        for declaration in ('int ss1_uncensused;', 'static int ss1_uncensused;',
                            'static unsigned char ss1_uncensused[37];',
                            'static void (*ss1_uncensused)(void);',
                            'static struct { unsigned a; void *b; } ss1_uncensused;',
                            'static int ss1_uncensused __attribute__((used));',
                            'extern int ss1_uncensused = 7;',
                            'static int ss1_uncensused[sizeof(unsigned)];',
                            '#if 0\nstatic unsigned ss1_uncensused;\n#endif'):
            with self.subTest(declaration=declaration):
                self.mutate('engine/arculator/src/arm.c', lambda s: s + '\n' + declaration + '\n')

    def test_function_local(self):
        self.mutate('engine/arculator/src/arm.c', lambda s:
                    s + '\nvoid ss1_control(void) { static unsigned ss1_uncensused; }\n')

    def test_frontend_root(self):
        self.mutate('libretro/arcdroid_libretro.c', lambda s:
                    s + '\nstatic unsigned ss1_uncensused;\n')

    def test_record_member(self):
        self.mutate('engine/arculator/src/timer.h', lambda s:
                    s.replace('int enabled;', 'int enabled; unsigned ss1_uncensused;'))

    def test_heap_owner(self):
        self.mutate('engine/arculator/src/mem.c', lambda s:
                    s.replace('ram=(uint32_t *)malloc(memsize*1024);',
                              'ram=(uint32_t *)malloc(memsize*2048);', 1))
        self.mutate('engine/arculator/src/mem.c', lambda s:
                    s + '\nvoid ss1_heap(void) { (void)malloc(19); }\n')

    def test_function_local_heap_layout(self):
        before = ('void f(void) { struct LocalHeap { unsigned x; }; '
                  'static struct LocalHeap *p; p = malloc(sizeof(*p)); }')
        after = before.replace('unsigned x;', 'unsigned x; unsigned uncensused;')
        self.assertNotEqual(GUARD.census(before), GUARD.census(after))

    def test_macro_introduces_storage(self):
        before = '#define SS1_ROOT int old_root\nSS1_ROOT;\n'
        after = '#define SS1_ROOT int old_root; static int new_root\nSS1_ROOT;\n'
        self.assertNotEqual(GUARD.census(before), GUARD.census(after))

    def test_multiline_source_attachment(self):
        self.mutate('CMakeLists.txt', lambda s:
                    s + '\ntarget_sources(arcdroid_core PRIVATE\n  core/new.c)\n')
        self.mutate('CMakeLists.txt', lambda s:
                    s.replace('core/hash.c', 'core/hash.c\n  core/new.c', 1))
        self.mutate('CMakeLists.txt', lambda s:
                    s + '\nlist(APPEND ARCDROID_CORE_SOURCES core/new.c)\n')

    def test_nonstorage_change(self):
        before = 'unsigned state;\nvoid step(void) { state += 1; }\n'
        after = '/* comment */ unsigned state;\nvoid step(void) { state += 2; }\n'
        self.assertEqual(GUARD.census(before), GUARD.census(after))

    def test_allocation_classification_required(self):
        bad = copy.deepcopy(self.manifest)
        bad['allocation_review'].pop()
        with self.assertRaisesRegex(ValueError, 'ownership review'):
            GUARD.verify(ROOT, bad)
        bad = copy.deepcopy(self.manifest)
        bad['reachable_storage'][0]['reason'] = ''
        with self.assertRaisesRegex(ValueError, 'unclassified'):
            GUARD.verify(ROOT, bad)


if __name__ == '__main__':
    unittest.main()
