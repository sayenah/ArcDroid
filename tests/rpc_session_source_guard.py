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
    require('arc_backend' not in text,f'{path}: dispatcher pointer escaped')
    if path.name!='arcdroid_libretro.c':
        require(not re.search(r'\barc_frontend_session_id\s*\(',text),f'{path}: ID authority escaped into storage')
require('arc_engine_is_rpc(' not in front.split('RETRO_API bool arc_libretro_test_rpc_admit(void)',1)[0], 'frontend policy must use kind, not dispatcher identity')
require(front.count('arc_session_id_allocate(&arc_session_ids, &arc_session_id)')==3,'one admission per frontend route plus direct-engine test seam')
require('static ArcSessionIdSource arc_session_ids;' in front,'library-local counter')
require(not re.search(r'arc_session_ids\s*(?:=|\.)',front),'counter must survive init/deinit')
require('session=arc_session_id;' in front,'disc log lifetime')
require('static uint64_t input_generation' not in front,'function-local stale input owner')
require(front.count('arc_engine_session_generation()')==2,'RPC restart generation query census')
require('info->geometry.max_width = 4096u;' in front,'stable maximum geometry')
for name in ['arc_sync_cmos','arc_seed_cmos','arc_persist_cmos','arc_boot_persist_seed','arc_hd_reconcile','arc_boot_record_publish','arc_hd_outcome_boundary']:
    body=front.split(name+'(void)\n{',1)[1].split('\n}',1)[0]
    require('arc_kind != ARC_SESSION_KIND_ARCULATOR' in body,f'{name}: Arculator persistence guard')
# No serialization/boot/store writer references frontend ID/source types. Part
# 2 must extend this with its attempt/helper/guest-memory integration evidence.
for directory in ('core','libretro'):
    for path in (root/directory).glob('*.[ch]'):
        if path.name in ('arc_session.c','arc_session.h','arcdroid_libretro.c'):continue
        require(not re.search(r'\b(?:ArcSessionIdSource|arc_frontend_session_id|arc_session_ids)\b',path.read_text()),f'{path}: persisted ID dependency')
print('session identity source guard: frontend kinds; preparation ownership exception; restart generations; library-local nonpersisted IDs')
