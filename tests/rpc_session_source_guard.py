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
    # Part 2 adds two module ownership checks (reset and execution barrier);
    # their exact function owners are pinned below, with the original persist check.
    allowed=2 if path.name=='arcdroid_libretro.c' else 3 if path.name=='rpc_preparation.c' else 0
    require(count==allowed,f'{path}: unexpected last-backend query ({count})')
    capabilities=len(re.findall(r'\barc_engine_capabilities\s*\(',text))
    require(capabilities==(1 if path.name=='arcdroid_libretro.c' else 0),f'{path}: unexpected capability query ({capabilities})')
    observer_cells=re.findall(r'\barc_engine_rpc_observers_capability\s*\(\s*(\w+)\s*\)',text)
    require(len(observer_cells)==len(re.findall(r'\barc_engine_rpc_observers_capability\s*\(',text)),f'{path}: unlisted observer capability expression')
    require(sorted(observer_cells)==(['ARC_RPC_CELL_REPLACE_A','ARC_RPC_CELL_REPLACE_B','bit'] if path.name=='arcdroid_libretro.c' else []),f'{path}: unexpected observer capability query')
    require('arc_backend' not in text,f'{path}: dispatcher pointer escaped')
    if path.name!='arcdroid_libretro.c':
        require(not re.search(r'\barc_frontend_session_id\s*\(',text),f'{path}: ID authority escaped into storage')
require('arc_engine_is_rpc(' not in front.split('RETRO_API bool arc_libretro_test_rpc_admit(void)',1)[0], 'frontend policy must use kind, not dispatcher identity')
def body(name,source=front):
    match=re.search(r'(?m)^(?:static\s+|RETRO_API\s+)?[^\n;{}]*\b'+re.escape(name)+r'\([^;{}]*\)\s*\{',source)
    require(match is not None,f'{name}: missing policy function')
    # Match the function itself, including compact one-line definitions; an
    # adjacent function must not inherit a preparation ownership allowance.
    masked=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m:' '*len(m[0]),source,flags=re.S)
    depth=1
    for end in range(match.end(),len(masked)):
        depth+=(masked[end]=='{')-(masked[end]=='}')
        if not depth:return source[match.end():end]
    raise RuntimeError(f'{name}: unclosed policy function')
prep=(root/'libretro/rpc_preparation.c').read_text()
for name in ('arc_riscpc_preparation_persist','arc_riscpc_preparation_reset','arc_riscpc_preparation_execution_allowed'):
    require(len(re.findall(r'\barc_engine_is_rpc\s*\(',body(name,prep)))==1, f'{name}: changed preparation engine ownership check')
# Pinned eebfc01 has 15 frontend dispatcher-identity sites: these fourteen
# predicates plus the now constant maximum-geometry bound. Part 2 adds fourteen
# input/media/rollback policy sites, pinned below with the original predicates. H-J added consumers
# of rpc_error, but no additional dispatcher query. R3d-4 adds safe RPC log/
# error projections at six sites below; pin their owners and literal payloads.
policy_sites={'arc_disk_insert_entry':1,'arc_disc_commit':5,
    'arc_disc_observation':2,'arc_disc_setup':1,'arc_rpc_disc_log':1,
    'arc_disc_boundary':2,'arc_disk_replace':4,'arc_poll_inputs':4,
    'arc_disk_set_eject':2,'arc_disk_set_index':1,'arc_disk_add':1,'arc_keyboard':1,
    'retro_reset':1,'arc_run':2}
for name,count in policy_sites.items():
    require(len(re.findall(r'\barc_kind\s*==\s*ARC_SESSION_KIND_RISCPC\b',body(name)))==count,f'{name}: changed session-kind policy census')
media_error='arc_kind==ARC_SESSION_KIND_RISCPC?"The selected disc could not be loaded. Check that the file is complete and a supported type.":'
for name,count in [('arc_disk_replace',2),('arc_disk_set_eject',1)]:
    require(body(name).count(media_error)==count,f'{name}: changed safe RPC media error owner')
require('arc_kind==ARC_SESSION_KIND_RISCPC?"The emulated RiscPC stopped. Close and reload this content.":' in body('arc_run'),
        'retro_run: changed safe RPC fault owner')
for name,signal in [('arc_disc_observation','B'),('arc_disc_boundary','A')]:
    require(re.search(r'if\([^\n]*arc_kind==ARC_SESSION_KIND_RISCPC\)\s*arc_log\(RETRO_LOG_INFO,\s*"Disc request: signal='+signal,body(name)),
            f'{name}: changed safe RPC disc log owner')
# R-SS4e: secondary/failed rollback stops execution, callback restoration alone
# cannot publish speculation, and the outer real return finalizes storage.
# Pin the full delegation/ordering; comments and whitespace carry no policy.
wrapper=re.sub(r'/\*.*?\*/|//[^\n]*','',body('retro_run'),flags=re.S)
expected_wrapper='''
   if(arc_secondary_instance||arc_rpc_alias_lifecycle||arc_state_rollback_failed)return;
   if(arc_state_speculation&&arc_input_poll==arc_state_real_poll)return;
   arc_state_inside_run=true;arc_run();
   if(arc_kind==ARC_SESSION_KIND_ARCULATOR&&!arc_state_speculation)
      (void)arc_state_boundary_finish();
   arc_state_inside_run=false;
'''
require(''.join(wrapper.split())==''.join(expected_wrapper.split()),
        'retro_run: changed secondary/reentrancy/speculation/publication wrapper')
for name in ('arc_disc_commit','arc_disk_set_eject','arc_disk_set_index','arc_disk_replace','arc_disk_add','arc_keyboard'):
    require(len(re.findall(r'\barc_kind\s*!=\s*ARC_SESSION_KIND_RISCPC\b',body(name)))==1,f'{name}: changed part 2 non-RPC policy census')
poll=body('arc_poll_inputs')
require('bool rpc_error=(arc_kind == ARC_SESSION_KIND_RISCPC);' in poll,'hint/input RPC owner must use frontend kind')
require(len(re.findall(r'\brpc_error\s*=(?!=)',poll))==1,'hint/input RPC owner must not be reassigned')
for name,cell in [('arc_disc_observation','ARC_RPC_CELL_REPLACE_B'),('arc_disc_boundary','ARC_RPC_CELL_REPLACE_A')]:
    require('arc_engine_rpc_observers_capability('+cell+')' in body(name),f'{name}: changed observer capability owner')
attach=body('arc_rpc_attach')
require(re.search(r'#if ARC_RPC_ORDINARY_LOAD\s+if\(!arc_rpc_plan_admitted && arc_log\)\{unsigned cells=0;for\(unsigned bit=1;bit<=128;bit<<=1\)if\(arc_engine_rpc_observers_capability\(bit\)\)cells\|=bit;',attach),
        'ordinary attach must own the bounded production observer summary')
require(attach.count('arc_engine_rpc_observers_capability(')==1 and
        '"RPC route: production-observer-cells=%u\\n",cells' in attach,
        'observer summary must only report the effective cells')
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
print('session identity source guard: original + part 2 policy predicates + constant AV bound; hint/reset/log owners; two observer cells and ordinary attach summary; preparation ownership exception; library-local nonpersisted IDs')
