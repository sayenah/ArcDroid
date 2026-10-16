#!/usr/bin/env python3
"""Admitted identity queries and library-local ID authority. GPL-2.0-only."""
from pathlib import Path
import re
import sys
root=Path(sys.argv[1])
def require(ok,message):
    if not ok:raise RuntimeError(message)
front=(root/'libretro/arcdroid_libretro.c').read_text()
for path in (root/'libretro').glob('*.c'):
    text=path.read_text()
    count=len(re.findall(r'\barc_engine_is_rpc\s*\(',text))
    # Production-module ownership check: not frontend route policy. The test
    # snapshot deliberately exposes the engine query to the behavioural oracle.
    allowed=2 if path.name=='arcdroid_libretro.c' else 1 if path.name=='rpc_preparation.c' else 0
    require(count==allowed,f'{path}: unexpected last-backend query ({count})')
    capabilities=len(re.findall(r'\barc_engine_capabilities\s*\(',text))
    require(capabilities==(1 if path.name=='arcdroid_libretro.c' else 0),f'{path}: unexpected capability query ({capabilities})')
    observer_cells=re.findall(r'\barc_engine_rpc_observers_capability\s*\(\s*(\w+)\s*\)',text)
    require(len(observer_cells)==len(re.findall(r'\barc_engine_rpc_observers_capability\s*\(',text)),f'{path}: unlisted observer capability expression')
    require(sorted(observer_cells)==(['ARC_RPC_CELL_REPLACE_A','ARC_RPC_CELL_REPLACE_B'] if path.name=='arcdroid_libretro.c' else []),f'{path}: unexpected observer capability query')
    require('arc_backend' not in text,f'{path}: dispatcher pointer escaped')
    if path.name!='arcdroid_libretro.c':
        require(not re.search(r'\barc_frontend_session_id\s*\(',text),f'{path}: ID authority escaped into storage')
require('arc_engine_is_rpc(' not in front.split('RETRO_API bool arc_libretro_test_rpc_admit(void)',1)[0], 'frontend policy must use kind, not dispatcher identity')
def body(name):
    match=re.search(r'(?m)^(?:static\s+|RETRO_API\s+)[^\n;{}]*\b'+re.escape(name)+r'\([^;{}]*\)\s*\{',front)
    require(match is not None,f'{name}: missing policy function')
    return front[match.end():].split('\n}',1)[0]
# Pinned eebfc01 has 15 frontend dispatcher-identity sites: these fourteen
# predicates plus the now constant maximum-geometry bound. H-J added consumers
# of rpc_error, but no additional dispatcher query. Pin their owner as well.
policy_sites={'arc_disk_insert_entry':1,'arc_disc_commit':3,
    'arc_disc_observation':1,'arc_disc_setup':1,'arc_rpc_disc_log':1,
    'arc_disc_boundary':1,'arc_disk_replace':1,'arc_poll_inputs':3,
    'retro_reset':1,'retro_run':1}
for name,count in policy_sites.items():
    require(len(re.findall(r'\barc_kind\s*==\s*ARC_SESSION_KIND_RISCPC\b',body(name)))==count,f'{name}: changed session-kind policy census')
poll=body('arc_poll_inputs')
require('bool rpc_error=(arc_kind == ARC_SESSION_KIND_RISCPC);' in poll,'hint/input RPC owner must use frontend kind')
require(len(re.findall(r'\brpc_error\s*=(?!=)',poll))==1,'hint/input RPC owner must not be reassigned')
for name,cell in [('arc_disc_observation','ARC_RPC_CELL_REPLACE_B'),('arc_disc_boundary','ARC_RPC_CELL_REPLACE_A')]:
    require('arc_engine_rpc_observers_capability('+cell+')' in body(name),f'{name}: changed observer capability owner')
require(front.count('arc_session_id_allocate(&arc_session_ids, &arc_session_id)')==3,'one admission per frontend route plus direct-engine test seam')
require('static ArcSessionIdSource arc_session_ids;' in front,'library-local counter')
require(not re.search(r'arc_session_ids\s*(?:=|\.)',front),'counter must survive init/deinit')
require('session=arc_session_id;' in front,'disc log lifetime')
require('static uint64_t input_generation' not in front,'function-local stale input owner')
require(front.count('arc_engine_session_generation()')==2,'RPC restart generation query census')
require('info->geometry.max_width = 4096u;' in front,'stable maximum geometry')
for name in ['arc_sync_cmos','arc_seed_cmos','arc_persist_cmos','arc_boot_persist_seed',
             'arc_hd_reconcile','arc_boot_record_publish','arc_hd_outcome_boundary',
             'arc_boot_record_path','arc_launch_binding_publish','arc_construct_machine']:
    guard=re.search(r'if\s*\((.*?)\)\s*return(?:\s+false)?\s*;',body(name),re.S)
    require(guard is not None,f'{name}: missing early persistence guard')
    condition=guard.group(1)
    require(condition=='arc_kind != ARC_SESSION_KIND_ARCULATOR' or
            condition.startswith('arc_kind != ARC_SESSION_KIND_ARCULATOR || '),
            f'{name}: Arculator persistence guard must be an unconditional kind exclusion')
# No serialization/boot/store writer references frontend ID/source types. Part
# 2 must extend this with its attempt/helper/guest-memory integration evidence.
for directory in ('core','libretro'):
    for path in (root/directory).glob('*.[ch]'):
        if path.name in ('arc_session.c','arc_session.h','arcdroid_libretro.c'):continue
        require(not re.search(r'\b(?:ArcSessionIdSource|arc_frontend_session_id|arc_session_ids)\b',path.read_text()),f'{path}: persisted ID dependency')
print('session identity source guard: 14 policy predicates + constant AV bound; hint/reset/log owners; two observer cells; preparation ownership exception; library-local nonpersisted IDs')
