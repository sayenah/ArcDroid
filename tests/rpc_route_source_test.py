#!/usr/bin/env python3
"""Frozen staging producers and the early no-execution refusal. GPL-2.0-only."""
import re,sys
from pathlib import Path
root=Path(sys.argv[1]);front=(root/'libretro/arcdroid_libretro.c').read_text()
def block(token):
    start=front.index(token);brace=front.index('{',start);depth=1;end=brace+1
    while depth:depth+=(front[end]=='{')-(front[end]=='}');end+=1
    return front[brace:end]
# These are all frontend staging output producers, including callbacks and the
# caller's final returned error. The cap is not a directory enumeration cap:
# folder warnings take the same R-3 exhaustion path as every other producer.
expected={'arc_prepare_bytes','arc_collect_folder_member','arc_log_folder_result','arc_folder_summary','arc_prepare_folder_siblings','arc_zip_summary','arc_zip_log','arc_prepare_zip_content'}
region=front[front.index('static bool arc_prepare_bytes('):front.index('static bool arc_prepare_media_list(')]
actual=set()
for match in re.finditer(r'^static [^;\n]*\b(arc_\w+)\(',region,re.M):
    name=match[1];body=block(match[0])
    if re.search(r'\barc_(?:log|rpc_staging_log|message|media_notice)\b',body):actual.add(name)
assert actual==expected,(actual,expected)
for name in expected:
    body=block(next(m[0] for m in re.finditer(r'^static [^;\n]*\b'+name+r'\(',front,re.M)))
    assert not re.search(r'\b(?:getenv|fprintf|printf)\s*\(',body)
prepared=block('static bool arc_prepare_bytes(')
assert prepared.index('arc_rpc_staging_flag()')<prepared.index('JFD: title=')
load=block('static bool arc_load_game(')
failed=load.split('if (!arc_prepare_media_list(',1)[1].split('   arc_image_index',1)[0]
assert failed.index('arc_message(error,')<failed.index('arc_rpc_staging_finish(arc_rpc_staging.flagged)')<failed.index('arc_release_session()')
entry=block('RETRO_API bool retro_load_game(')
barrier=entry.index('if(arc_loaded || arc_rpc_plan_admitted || arc_secondary_instance || arc_rpc_alias_lifecycle || arc_rpc_secondary_instance()')
assert entry.index('return false;',barrier)<entry.index('arc_rpc_load_entry_ns=')<entry.index('arc_load_game(game, true)')
identity=block('static bool arc_rpc_secondary_instance(')
assert 'GET_LIBRETRO_PATH' in identity and 'RTLD_NOLOAD' in identity and 'dli_fbase' in identity
assert 'GetModuleHandleExW' in identity and 'GetModuleHandleW' in identity
assert 'arc_video' not in identity
for token,registration in [('RETRO_API void retro_set_environment(', 'arc_build_options()'),
                           ('RETRO_API void retro_init(', 'RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE')]:
    body=block(token)
    assert body.index('arc_rpc_secondary_instance()')<body.index(registration)
assert 'arc_riscpc_production_catalogue(' in (root/'engine/arcdroid_engine.c').read_text()
print('rpc-route-source: complete staging producer census, final-error deferral and earliest secondary refusal verified')
