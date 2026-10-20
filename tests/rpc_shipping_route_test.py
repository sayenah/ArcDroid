#!/usr/bin/env python3
"""Hidden catalogue and route boundaries, beyond the export guard. GPL-2.0-only."""
import json,re,sys
from pathlib import Path
build=Path(sys.argv[1]);binary=Path(sys.argv[2]);root=Path(__file__).resolve().parent.parent
rows=json.loads((build/'rpc-artifact-targets.json').read_text())['targets'];target=next(r for r in rows if r['target']=='arcdroid_libretro')
assert not target['authored'] and not target['substitutions']
data=binary.read_bytes();marker=re.findall(rb'ARCDROID-ROUTE-V1:gate=(open|closed);catalogue=(production|authored);end',data)
assert marker==[(b'open' if target['gate_open'] else b'closed',b'production')]
commands=json.loads((build/'compile_commands.json').read_text())
core=[row for row in commands if 'CMakeFiles/arcdroid_core.dir/' in row['command'].replace('\\','/')]
assert any(row['file'].replace('\\','/').endswith('/core/rpc_catalogue_route.c') for row in core)
assert not any('generated/rpc-route' in row['file'] for row in core)
links=[line for line in (build/'build.ninja').read_text().splitlines() if line.startswith('build ') and binary.name in line[6:].split(': ',1)[0].split()]
assert len(links)==1,links
link=links[0]
assert 'libarcdroid_core.a' in link and 'core_route' not in link
engine=(root/'engine/arcdroid_engine.c').read_text().split('void arc_engine_rpc_observers_bind(',1)[1].split('\n}',1)[0]
assert 'arc_riscpc_production_catalogue(' in engine and 'arc_riscpc_firmware_catalogue(' not in engine
provider=build/'generated/rpc-route/rpc_route_catalogue.c'
if provider.exists():
    for sha in re.findall(r'"([0-9a-f]{40})"',provider.read_text()):assert sha.encode() not in data
# A future runtime provider seam must be reviewed, never admitted implicitly.
print('rpc-shipping-route: production link/provider, retained identity and observer qualification verified')
