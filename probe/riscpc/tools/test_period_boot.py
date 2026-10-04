#!/usr/bin/env python3
"""Authored R6-1 policy expectations; no firmware or release fixture bytes."""
import struct
import unittest
from period_boot import module_inventory, retain_rom_modules, read_firmware


class PeriodPolicy(unittest.TestCase):
    def test_loads_and_requirements(self):
        inventory = {'test': {'modules': {'RomService': {}}}}
        source = (b'If Boot$OSVersion >= 350 Then RMEnsure RomService 9.99 RMLoad System:Modules.Service\r'
                  b'RMEnsure Additional 2.00 RMLoad System:Modules.Additional\r'
                  b'RMEnsure RomService 9.99 Error A newer service is required\r'
                  b'| RMEnsure RomService 9.99 Run ignored\r')
        result, edits = retain_rom_modules(source, inventory, 'authored')
        # A present ROM skips the load. An absent service still executes the
        # original versioned load; independent minimum-version errors persist.
        self.assertIn(b'Then RMEnsure RomService 0.00 RMEnsure RomService 9.99 RMLoad', result)
        for line in source.split(b'\r')[1:]:
            self.assertIn(line + (b'\r' if line else b''), result)
        self.assertEqual(len(edits), 1)
        self.assertEqual(edits[0]['roms'], ['test'])
        self.assertEqual(result.count(b'ARCDROID-PROBE'), 1)
        self.assertNotIn(b'\n', result)

    def test_binary_and_absent_module_unchanged(self):
        binary = b'\0RMEnsure RomService 9.99 Run program'
        self.assertEqual(retain_rom_modules(binary, {}, 'binary'), (binary, []))
        text = b'RMEnsure Additional 9.99 Run program\n'
        self.assertEqual(retain_rom_modules(text, {}, 'text'), (text, []))

    def test_rom_headers_help_before_and_far_after_title(self):
        data = bytearray(4096)
        for header, title, help_at, name in ((0, 96, 64, 'SharedCLibrary'),
                                            (512, 64, 1024, 'WindowManager')):
            struct.pack_into('<II', data, header + 16, title, help_at)
            t = (name + '\0').encode()
            h = (name + '\t1.23 (authored)\0').encode()
            data[header + title:header + title + len(t)] = t
            data[header + help_at:header + help_at + len(h)] = h
        modules = module_inventory(data)
        self.assertEqual(set(modules), {'SharedCLibrary', 'WindowManager'})
        self.assertEqual(modules['WindowManager']['version'], '1.23')
        with self.assertRaises(ValueError):
            module_inventory(bytes(4096))

    def test_missing_firmware_refused(self):
        with self.assertRaises(ValueError):
            read_firmware([])


if __name__ == '__main__':
    unittest.main()
