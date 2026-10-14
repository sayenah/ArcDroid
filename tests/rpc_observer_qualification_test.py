#!/usr/bin/env python3
"""Pin compiled production cells/profiles to the adopted record. GPL-2.0-only."""
from pathlib import Path
import re
import subprocess
import sys
sys.dont_write_bytecode = True
binary, record = sys.argv[1:]
text = Path(record).read_text(encoding='utf-8')
assert 'Status: adopted (D-181)' in text
revision = int(re.search(r'## Profile revision (\d+) parameters', text)[1])
cells = {}
columns = [(2, 0), (2, 1), (3, 0), (3, 2), (3, 3), (3, 4), (0, 0), (1, 0)]
for row in text.split('## Cells\n', 1)[1].split('## Evidence per cell', 1)[0].splitlines():
    pair = re.search(r'\((\d+) × (\d+)\)', row)
    if pair:
        catalogue, cpu = map(int, pair.groups())
        values = [part.strip() for part in row.split('|')[2:-1]]
        assert len(values) == len(columns)
        for (owner, cap), value in zip(columns, values):
            assert value in ('on', 'off')
            cells[catalogue, cpu, owner, cap] = value == 'on'
assert len(cells) == 56 and sum(cells.values()) == 42
disabled = re.search(r'\*\*Disabled reason, boot and launch \(all pairs\):\*\* "([^"]+)"', text)[1]

# Evidence references come from the record's leg tables, including the two
# pair-specific additions. Do not turn their private paths into test content.
evidence = {key: [] for key, enabled in cells.items() if enabled}
for section, owners in [('The error-box evidence', [(2, 0), (2, 1)]), ('The disc evidence', [(3, 0), (3, 2), (3, 3), (3, 4)])]:
    part = text.split(section, 1)[1].split('## Cost', 1)[0]
    if section == 'The error-box evidence': part = part.split('The disc evidence', 1)[0]
    for row in part.splitlines():
        if not row.startswith('| `'): continue
        fields = [field.strip() for field in row.split('|')[1:-1]]
        legs = re.findall(r'`([a-z0-9]+-[a-z0-9*-]+)`', fields[0])
        pairs = [(key[0], key[1]) for key in cells if key[2:] == (2, 0)]
        pairs = pairs if fields[1] == 'all seven' else [(1, 0)] if fields[1] == '3.50 ARM610' else [(4, 2)]
        targets = owners
        if legs == ['o-350-arm610']: targets = [(2, 0)]
        if section == 'The disc evidence': targets = [(3, 3), (3, 4)] if legs == ['sb-*'] else [(3, 0), (3, 2)]
        for pair in pairs:
            for owner, cap in targets: evidence[(*pair, owner, cap)].extend(legs)

# Every profile value is pinned, including the shared B predicates. A changed
# record must be adjudicated with its corresponding production profile/test.
parameters = dict(re.findall(r'^\| ([^|]+?) \| ([^|]+?) \|$', text.split('## Profile revision', 1)[1].split('## Calibration', 1)[0], re.M))
assert parameters['Error-box poll'] == '`OS_Byte 129` with R1 = 0 and R2 = 0, from ROM, in SVC26 or SVC32 at SWI entry. The site and stack are learned from the first such poll while the user report is pending.'
assert parameters['Answerable forms'] == 'Old-style flags 3 and 14 only.'
assert parameters['Form 3'] == 'OK and Cancel; default OK. A answers OK (Return), B answers Cancel (Escape).'
assert parameters['Form 14'] == 'Cancel only; default Cancel. B answers Cancel; A gives no answer.'
assert parameters['Other synchronous forms'] == 'Observation only: a truthful manual hint, no OK/Cancel claim.'
assert parameters['Disc signal A device forms'] == 'Unspecified device only (`XOS_UpCall` R3 = 0xFFFFFFFF, D-179).'
assert parameters['Disc signal B'] == 'The shared Arculator path and operation predicates (`ADFS::<inserted disc or 0>.$…`), first miss ignored and a repeated miss resolved, as on Arculator.'
assert 'since only error number 0 was exercised' in text

output = subprocess.check_output([binary, '--qualification'], text=True, timeout=30)
seen_cells = set(); seen_table = set(); seen_error = set(); seen_disc = set()
for line in output.splitlines():
    kind, *values = line.split('\t')
    if kind in ('cell', 'table'):
        catalogue, cpu, owner, cap, profile, enabled = map(int, values[:6]); key = catalogue, cpu, owner, cap
        seen = seen_table if kind == 'table' else seen_cells
        assert key not in seen; seen.add(key)
        assert profile == revision and bool(enabled) == cells.get(key, False), line
        reason = ('docs/riscpc-observer-qualification.md: ' + ', '.join(evidence[key])) if cells.get(key) else disabled if key in cells else 'unknown firmware, CPU pair or sub-capability'
        assert values[6] == reason, (key, values[6], reason)
    elif kind == 'error':
        catalogue, cpu, *profile = map(int, values); key = catalogue, cpu
        assert key not in seen_error; seen_error.add(key)
        assert profile == [1, 0, 0, (1 << 3) | (1 << 19), (1 << 3) | (1 << 14), 1 << 3, (1 << 3) | (1 << 14), 1 << 14, 0], line
    elif kind == 'disc':
        catalogue, cpu, *profile = map(int, values); key = catalogue, cpu
        assert key not in seen_disc; seen_disc.add(key)
        assert profile == [(1 << 0) | (1 << 16), (1 << 3) | (1 << 19), 2, 1, 1], line
    else: raise AssertionError(line)
assert seen_cells == {(cat, cpu, owner, cap) for cat in range(6) for cpu in range(5) for owner in range(4) for cap in range(5)}
assert seen_table == set(cells), 'missing/extra production table row'
pairs = {(cat, cpu) for cat, cpu, _, _ in cells}
assert seen_error == seen_disc == pairs
print('qualification record: every cell/value/reason/reference pinned; 42 enabled, 14 deferred, unknown tuples disabled')
