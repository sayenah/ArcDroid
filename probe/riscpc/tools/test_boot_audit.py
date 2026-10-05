#!/usr/bin/env python3
"""Authored Resources audit boundaries; GPL-2.0-only, no supplied fixtures."""
import unittest
from boot_audit import directory_exists, scrap_leaf_valid


def entry(name, child, directory):
    result = bytearray(26)
    result[:len(name)] = name.encode('ascii')
    result[len(name)] = 13
    result[22:25] = child.to_bytes(3, 'little')
    result[25] = 8 if directory else 0
    return result


class AuthoredDisc:
    root = 1

    def read(self, address, length):
        assert length == 2048
        block = bytearray(2048)
        records = {1: [entry('Parent', 2, True), entry('File', 3, False)],
                   2: [entry('Empty', 3, True)], 3: []}[address]
        for i, record in enumerate(records):
            block[5+26*i:31+26*i] = record
        return block


class ResourcesAudit(unittest.TestCase):
    def test_scrap_source_selection(self):
        self.assertTrue(scrap_leaf_valid('ScrapDir', None))
        self.assertTrue(scrap_leaf_valid('ID000!000', None))
        self.assertTrue(scrap_leaf_valid('ID255!001', None))
        self.assertTrue(scrap_leaf_valid('ID192.0.2.7', '192.0.2.7'))
        self.assertTrue(scrap_leaf_valid('ID', ''))
        self.assertFalse(scrap_leaf_valid('ScrapDir', '192.0.2.7'))

    def test_invalid_station_network_or_path(self):
        for name in ('ID256!000', 'ID000!999', 'ID00!001', 'ID000!000.extra',
                     'Elsewhere', 'ScrapDir.extra', ''):
            self.assertFalse(scrap_leaf_valid(name, None), name)

    def test_empty_directory_and_case(self):
        self.assertTrue(directory_exists(AuthoredDisc(), ['parent', 'EMPTY']))
        self.assertFalse(directory_exists(AuthoredDisc(), ['Parent', 'Absent']))
        self.assertFalse(directory_exists(AuthoredDisc(), ['File']))
        self.assertFalse(directory_exists(AuthoredDisc(), ['File', 'Empty']))


if __name__ == '__main__':
    unittest.main()
