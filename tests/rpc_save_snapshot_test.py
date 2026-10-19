"""Windows snapshot regressions, using authored files and denied sentinel reads.
SPDX-License-Identifier: GPL-2.0-only
"""
from pathlib import Path
import tempfile
from unittest.mock import patch
from rpc_save_snapshot import save_snapshot


def main():
    with tempfile.TemporaryDirectory(prefix='arc-save-snapshot-') as temporary:
        save = Path(temporary)
        locks = save/'arcdroid/locks-riscpc'
        locks.mkdir(parents=True)
        sentinel = locks/('c-' + 'a'*40 + '.lock')
        sentinel.touch()
        record = save/'arcdroid/cmos-riscpc/authored.1'
        record.parent.mkdir()
        record.write_bytes(b'authored\r\nstore\x00bytes')
        original_read = Path.read_bytes
        reads = []

        def locked_read(path):
            if path == sentinel:
                raise PermissionError('authored simulation of a Windows locked sentinel')
            reads.append(path)
            return original_read(path)

        with patch.object(Path, 'read_bytes', locked_read):
            before = save_snapshot(save)
            assert before[sentinel.relative_to(save).as_posix()] == ('lock-sentinel', 0)
            assert before[record.relative_to(save).as_posix()] == original_read(record)
            assert reads == [record]
            # Both metadata fields matter: loss and nonzero length are failures.
            sentinel.unlink()
            assert save_snapshot(save) != before
            sentinel.write_bytes(b'x')
            try:
                save_snapshot(save)
            except AssertionError:
                pass
            else:
                raise AssertionError('nonzero sentinel was accepted')
            sentinel.write_bytes(b'')
            assert save_snapshot(save) == before
            # Store mutations, additions and removals remain observable. CRLF
            # is binary data; no case or line-ending normalization hides writes.
            record.write_bytes(b'authored\nstore\x00bytes')
            assert save_snapshot(save) != before
            record.write_bytes(b'authored\r\nstore\x00bytes')
            extra = record.with_name('extra.tmp')
            extra.write_bytes(b'candidate')
            assert save_snapshot(save) != before
            extra.unlink()
            record.unlink()
            assert save_snapshot(save) != before
            record.write_bytes(b'authored\r\nstore\x00bytes')
            # Reproduce the Windows test-only CREATE_ALWAYS addition. Seeding
            # that known empty fixture before baseline preserves byte checks
            # for the diagnostic too; no filename is excluded from comparison.
            diagnostic = save/'rpc-hd-exception.txt'
            diagnostic.touch()
            assert save_snapshot(save) != before
            baseline = save_snapshot(save)
            diagnostic.write_bytes(b'')
            assert save_snapshot(save) == baseline
            diagnostic.write_bytes(b'authored unexpected exception')
            assert save_snapshot(save) != baseline
            diagnostic.write_bytes(b'')
            nested = record.with_name(diagnostic.name)
            nested.write_bytes(b'authored store bytes')
            assert save_snapshot(save) != baseline
            assert nested in reads
            unknown = locks/'not-a-sentinel.data'
            unknown.write_bytes(b'authored namespace file')
            assert save_snapshot(save)[unknown.relative_to(save).as_posix()] == b'authored namespace file'
            assert unknown in reads
    print('save snapshots: denied sentinel reads, zero-size/existence checks and byte comparisons including the seeded diagnostic passed')


if __name__ == '__main__':
    main()
