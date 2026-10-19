#!/usr/bin/env python3
"""Generate lockstep C layout/policy from the reviewed schema. GPL-2.0-only."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SECTIONS = ('reviewed_conditions', 'reviewed_exclusions', 'reviewed_mode_exclusions',
            'cross_quota_projections', 'stdout_projection', 'observer_projections', 'wire', 'output_compared', 'reviewed_inventory', 'reachable_types', 'reviewed_serializer_bridges', 'host_state_members', 'host_static_objects')


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode('utf-8')


def compatibility(schema):
    """Only compared layout and executable projection rules enter wire identity."""
    keys = ('name', 'group', 'type', 'count', 'class', 'phases', 'condition',
            'allowed_allocations', 'index_limit', 'index_values', 'mode_conditions')
    fields = []
    for field in schema['fields']:
        if field['condition'] is False:continue
        row = {k: field[k] for k in keys if k in field}
        row['mode_exclusions'] = sorted(field['mode_exclusions'])
        fields.append(row)
    projections = schema['cross_quota_projections']
    return hashlib.sha256(canonical(dict(fields=fields, groups=schema['groups'],
        allocations=schema['allocations'], wire=schema['wire'],
        observer={k:v for k,v in schema['observer_projections'].items()
                  if k not in ('citation', 'vectors')},
        driver=projections['fields'],reachable=schema['reachable_types']))).digest()


def load(schema_path=ROOT/'tests/rpc_lockstep_schema.json',
         policy_path=ROOT/'tests/rpc_lockstep_reviewed_policy.json'):
    schema = json.loads(Path(schema_path).read_text(encoding='utf-8'))
    policy = json.loads(Path(policy_path).read_text(encoding='utf-8'))
    for section in SECTIONS:
        if schema[section] != policy[section]:
            raise ValueError('unreviewed schema policy: '+section)
    for name, rule in policy['cross_quota_projections'].get('conditions', {}).items():
        field = next(f for f in schema['fields'] if f['name'] == name)
        if field.get('mode_conditions', {}) != {'cross-quota': rule['condition']}:
            raise ValueError('unreviewed cross-quota condition: '+name)
    if schema['reviewed_inventory'] != {k:hashlib.sha256(canonical(schema[k])).hexdigest() for k in ('inventory','object_symbols','local_static_symbols')}:
        raise ValueError('inventory metadata changed without independent review')
    bridges={rule['field']:rule for rule in schema['reviewed_serializer_bridges']}
    if len(bridges)!=len(schema['reviewed_serializer_bridges']):
        raise ValueError('duplicate serializer bridge')
    for f in schema['fields']:
        rule=bridges.get(f['name'])
        if (f.get('c_layout') is False)!=(rule is not None) or rule and (
            not rule.get('reason') or not rule.get('citation') or
            any(f.get(key)!=rule.get(key) for key in ('source','c_expression'))):
            raise ValueError('unreviewed serializer bridge: '+f['name'])
    if not bridges.keys() <= {f['name'] for f in schema['fields']}:
        raise ValueError('unknown serializer bridge')
    return schema


def identifier(name):
    return name.replace('.', '__')


def expression(e, fields):
    if type(e) in (int, bool): return str(int(e))+'u'
    if 'field' in e:
        f = fields[e['field']]
        i = e.get('index', 0)
        i = 'i' if i == 'i' else expression(i, fields)
        return f'v[{f["offset"]}u+({i})*{f["width"]}u]'
    if 'index' in e: return 'i'
    if 'phase' in e: return 'phase'
    op, args = next(iter(e.items()))
    args = [expression(x, fields) for x in args]
    if op in ('not', 'neg'): return ('!' if op=='not' else '-')+'('+args[0]+')'
    symbols = dict(and_='&&', or_='||', eq='==', ne='!=', lt='<', add='+', mod='%', div='/')
    return '('+(' '+symbols.get(op+'_', symbols.get(op, '?'))+' ').join(args)+')'


def initializer(source, symbol):
    # Immutable operation/profile tables use source-token identities: no
    # padding, callback address or string-literal address enters their hash.
    text = (ROOT/source).read_text(encoding='utf-8')
    match = re.search(r'\b'+re.escape(symbol)+r'\s*(?:\[[^]]*\]\s*)*\s*=\s*', text)
    if not match: raise ValueError('missing immutable initializer: '+symbol)
    start = match.end(); depth = 0; quoted = False; escaped = False
    for end in range(start, len(text)):
        ch=text[end]
        if quoted:
            if escaped: escaped=False
            elif ch=='\\': escaped=True
            elif ch=='"': quoted=False
        elif ch=='"': quoted=True
        elif ch=='{': depth+=1
        elif ch=='}': depth-=1
        elif ch==';' and depth==0:
            return ' '.join(text[start:end].split()).encode('utf-8')
    raise ValueError('unterminated immutable initializer: '+symbol)


def reachable_helpers(schema):
    types=schema['reachable_types'];lines=[]
    lines += ['static void ls_media_bytes(const void *p,size_t n){arc_rpc_lockstep_output_word(p!=NULL);arc_rpc_lockstep_output_word(p?n:0);if(p)arc_rpc_lockstep_content_bytes(p,n);}',
              'static void ls_media_text(const char *p){ls_media_bytes(p,p?strlen(p):0);}']
    lines.append('_Static_assert(sizeof((ArcIoLockstepFile){0,0,0,0,0,0})==sizeof(ArcIoLockstepFile),"classified file snapshot");')
    for name in types:
        lines.append(f'static void ls_media_{name}(const {name} *,const ArcJfdImage *);')
    for name,members in types.items():
        literal=[]
        for member in members:
            zero='{0}' if member.get('record') else '0'
            for dimension in member['dimensions']:zero='{'+zero+'}'
            literal.append(zero)
        lines.append(f'_Static_assert(sizeof(({name}){{'+','.join(literal)+f'}})==sizeof({name}),"classified reachable members: {name}");')
        lines.append(f'static void ls_media_{name}(const {name} *p,const ArcJfdImage *image){{')
        lines.append('(void)image;arc_rpc_lockstep_output_word(p!=NULL);if(!p)return;')
        for member in members:
            dims=member['dimensions'];field=member['name'];value='p->'+field
            loops=''
            for i,dimension in enumerate(dims):
                loops+=f'for(unsigned j{i}=0;j{i}<{dimension}u;++j{i})';value+=f'[j{i}]'
            if member.get('record'):
                body=f'ls_media_{member["record"]}(&{value},image);'
            elif member.get('pointer'):
                if name=='ArcJfdImage' and field=='decoded':body=f'ls_media_bytes({value},p->decoded_size);'
                elif name=='ArcJfdImage' and field=='sectors':
                    body=f'arc_rpc_lockstep_output_word({value}!=NULL);if({value})for(size_t i=0;i<p->sector_count;++i)ls_media_ArcJfdSector({value}+i,p);'
                elif name=='ArcJfdImage' and field=='overlay':body=f'ls_media_ArcJfdOverlay({value},p);'
                elif name=='ArcJfdOverlay' and field=='data':
                    body=f'arc_rpc_lockstep_output_word({value}!=NULL);if({value})for(size_t i=0;i<image->sector_count;++i)ls_media_bytes({value}[i],image->sectors[i].size);'
                elif name=='ArcJfdOverlay' and field=='created':body=f'ls_media_bytes({value},1024u);'
                elif name=='ArcJfdOverlay' and field=='eligible':body=f'ls_media_bytes({value},image->sector_count);'
                elif name=='ArcJfdView' and field=='data':body=f'arc_rpc_lockstep_output_word({value}!=NULL);'
                elif member['c_type']=='const char *':body=f'ls_media_text({value});'
                else:body=f'arc_rpc_lockstep_output_word({value}!=NULL); /* typed owner callback/context identity */'
            elif re.match(r'^char(?:\[|$)',member['c_type']):body=f'arc_rpc_lockstep_output_word((uint64_t)(uint8_t)({value}));'
            else:body=f'arc_rpc_lockstep_output_word((uint64_t)({value}));'
            lines.append(loops+'{'+body+'}')
        lines.append('}')
    lines+=['static uint64_t ls_media_drive(const ArcRpcFloppyDrive *drive){',
        'arc_rpc_lockstep_content_begin();ls_media_bytes(drive->raw,drive->extent);',
        'ls_media_ArcJfdImage(drive->jfd,drive->jfd);ArcIoLockstepFile f;arc_io_lockstep_file(drive->file,&f);',
        'arc_rpc_lockstep_output_word(f.present);arc_rpc_lockstep_output_word(f.writable);arc_rpc_lockstep_output_word(f.external);',
        'arc_rpc_lockstep_output_word(f.memory_size);arc_rpc_lockstep_output_word(f.memory_at);ls_media_bytes(f.memory,f.memory_size);',
        'return arc_rpc_lockstep_content_end();}']
    return lines


def layout_rows(schema,source,fields):
    rows=[]
    locals_={v['name'] for v in schema['local_static_symbols'].get(source,[])}
    for fid,f in enumerate(fields.values()):
        if f['source']!=source or f.get('c_layout') is False:continue
        expr=f.get('c_expression');identity=f.get('c_identity')
        if identity:
            expr=identity['expression']
            if '.' in expr or expr in locals_:continue
        if not expr:continue
        expr=expr.replace('arc_rpc_observers_test_horizon.shortened','arc_rpc_lockstep_shortened')
        zero=re.sub(r'\[j\d+\]','[0]',expr)
        if '->' in zero:
            base,member=zero.split('->',1);root='*'+base;size=f'sizeof(*{base})'
            offset=f'offsetof(__typeof__(*{base}),{member})'
        else:
            base,sep,member=zero.partition('.');root=base.split('[')[0];size=f'sizeof({root})'
            offset=f'offsetof(__typeof__({base}),{member})' if sep else '0u'
        dims=f.get('c_dimensions',[]) if not identity else []
        strides=[]
        for level,dimension in enumerate(dims):
            prefix=expr[:expr.index(f'[j{level}]')];prefix=re.sub(r'\[j\d+\]','[0]',prefix)
            strides.append((dimension,f'sizeof(({prefix})[0])'))
        rows.append(dict(field=f['name'],root=root,values=[fid,offset,f'sizeof({zero})',size,len(strides),
            *[v for pair in strides for v in pair]],dimensions=len(strides)))
    if Path(source).stem=='fdc':
        types=dict(schema['reachable_types'])
        types['ArcIoLockstepFile']=[dict(name=n) for n in ('present','writable','external','memory','memory_size','memory_at')]
        for typename,members in types.items():
            def leaves(members,path='',strides=()):
                for member in members:
                    name=path+member['name'];axes=list(strides)
                    for dimension in member.get('dimensions',[]):
                        prefix=re.sub(r'\[j\d+\]','[0]',name)
                        axes.append((dimension,f'sizeof((({typename} *)0)->{prefix}[0])'))
                        name+=f'[j{len(axes)-1}]'
                    if member.get('record'):
                        leaves(types[member['record']],name+'.',tuple(axes));continue
                    zero=re.sub(r'\[j\d+\]','[0]',name)
                    rows.append(dict(field='@'+typename+'.'+name,root='@'+typename,dimensions=len(axes),
                        values=[len(fields),f'offsetof({typename},{zero})',f'sizeof((({typename} *)0)->{zero})',f'sizeof({typename})',len(axes),
                            *[v for pair in axes for v in pair]]))
            leaves(members)
    return rows


def generate(schema, directory):
    directory.mkdir(parents=True, exist_ok=True)
    fields={}; offset=0
    for f in schema['fields']:
        f=dict(f, offset=offset, width=2 if f['type']=='allocation-offset' else 1)
        fields[f['name']]=f; offset+=f['count']*f['width']
    header=['/* SPDX-License-Identifier: GPL-2.0-only; generated, do not commit. */',
            '#ifndef ARC_RPC_LOCKSTEP_GENERATED_H', '#define ARC_RPC_LOCKSTEP_GENERATED_H',
            '#include "rpc_lockstep.h"', f'#define ARC_RPC_LS_WORDS {offset}u',
            f'#define ARC_RPC_LS_FIELDS {len(fields)}u',
            f'#define ARC_RPC_LS_GROUPS {len(schema["groups"])}u']
    for f in fields.values(): header.append(f'#define LS_{identifier(f["name"])} {f["offset"]}u')
    sources=sorted({f['source'] for f in fields.values()}-{'','engine/rpc/rpc_profile.c','engine/rpc/rpc_crosscheck.c'})
    tu_ids={source:i for i,source in enumerate(sources)}
    cached={'rpc_audio','rpc_input','rpc_floppy','rpc_observers','cmos','fdc','i8042','ide','iomd','keyboard','superio','vidc20'}
    for source,i in tu_ids.items():header.append(f'#define ARC_RPC_LS_TU_{Path(source).stem} {i}u')
    layouts={}
    for source in sorted({f['source'] for f in fields.values()}-{'','engine/rpc/rpc_profile.c','engine/rpc/rpc_crosscheck.c'}):
        tu=Path(source).stem
        body=(reachable_helpers(schema) if tu=='fdc' else [])+[f'void arc_rpc_lockstep_collect_{tu}(uint64_t *out) {{']
        # Positional compound literals are unevaluated layout probes. A new
        # member, including one occupying old padding, must trigger the
        # compiler's missing-field-initializers error. Arrays have separate
        # capacity assertions; no probe changes the live object.
        roots={}
        for f in fields.values():
            if f['source']!=source or not f.get('c_expression') or '.shortened.' in f['c_expression']:continue
            parts=f['c_expression'].split('.');node=roots
            for part in parts:
                m=re.fullmatch(r'(\w+)((?:\[j\d+\])*)',part)
                if not m:break
                node=node.setdefault(m[1],{'array':bool(m[2]),'children':{}})['children']
        member_order=schema['c_member_order'].get(source,[])
        def probe(node,prefix):
            values=[]
            def position(name):
                path=prefix+'.'+name
                return next((i for i,p in enumerate(member_order) if p==path or p.startswith(path+'.')),len(member_order))
            for name,entry in sorted(node.items(),key=lambda pair:position(pair[0])):
                value=probe(entry['children'],prefix+'.'+name) if entry['children'] else '0'
                if entry['array']:value='{'+value+'}'
                values.append(value)
            return '{'+','.join(values)+'}'
        for root,entry in roots.items():
            if entry['children'] and not entry['array']:
                body.append(f'_Static_assert(sizeof((__typeof__({root})){probe(entry["children"],root)})==sizeof({root}),"classified member layout: {root}");')
        if tu=='arm':
            for number,root in enumerate(('reg','user_reg','fiq_reg','irq_reg','super_reg','abort_reg','undef_reg'),12):
                body.append(f'arc_rpc_lockstep_register({number}u,arm.{root},sizeof(arm.{root}));')
        if tu=='rpc_session':
            body.append('_Static_assert(sizeof((ArcInputState){{{0}},{0},{0},0,{0},0,{{0}},0,0,0,0,0})==sizeof(ArcInputState),"classified input pointee members");')
            body.append('_Static_assert(sizeof((ArcInputEvent){0,0,0,0,0,0})==sizeof(ArcInputEvent),"classified input event members");')
            body.append('arc_rpc_lockstep_register(10u,&arc_rpc_scheduler,sizeof(arc_rpc_scheduler));')
            body.append('arc_rpc_lockstep_register(8u,arc_rpc_input_service,arc_rpc_input_service?sizeof(*arc_rpc_input_service):0);')
        if tu=='vidc20':
            body.append('arc_rpc_lockstep_register(5u,presentation,presentation?ARC_RPC_VIDEO_LIMIT*ARC_RPC_VIDEO_LIMIT*4u:0);')
        if tu=='fdc':
            body.append('for(unsigned i=0;i<2;++i){const ArcJfdImage *image=media.drives[i].jfd;')
            body.append('arc_rpc_lockstep_register(33u+i,media.drives[i].raw,media.drives[i].raw?media.drives[i].extent:0);')
            body.append('arc_rpc_lockstep_register(35u+i,image,image?sizeof(*image):0);')
            body.append('arc_rpc_lockstep_register(37u+i,image?image->decoded:NULL,image?image->decoded_size:0);')
            body.append('arc_rpc_lockstep_register(39u+i,image?image->sectors:NULL,image?image->sector_count*sizeof(*image->sectors):0);')
            body.append('arc_rpc_lockstep_register(41u+i,image?image->overlay:NULL,image && image->overlay?sizeof(*image->overlay):0);}')
        if tu=='cmos':
            for number,root in enumerate(('pcf_s','pcf8583_s','spd_s','spd_i2c_s','serdes_s','pcf8583_ops','spd_ops'),19):
                body.append(f'arc_rpc_lockstep_register({number}u,&{root},sizeof({root}));')
        if tu=='rpc_observers':
            for number,root in enumerate(('default_disc_profile','default_error_profile','launch_profile','boot_profile','state.error_profile','state.disc_profile','arc_rpc_lockstep_shortened'),26):
                body.append(f'arc_rpc_lockstep_register({number}u,&{root},sizeof({root}));')
        for fid,f in enumerate(fields.values()):
            if f['source']!=source or f['condition'] is False or f['name']=='driver.linecyc': continue
            start=f['offset'];name=f['name'];expr=f.get('c_expression')
            if f.get('c_contents')=='media contents':
                body.append(f'for(unsigned i=0;i<2;++i)out[{start}+i]=ls_media_drive(media.drives+i);')
                continue
            if f.get('c_contents')=='selected view contents':
                body.append(f'arc_rpc_lockstep_content_begin();ls_media_ArcJfdView(&fdc.sector.view,NULL);out[{start}]=arc_rpc_lockstep_content_end();')
                continue
            if f.get('c_contents')=='observer SWI lists':
                body.append(f'for(unsigned i=0;i<ARC_RPC_OBSERVERS;++i)for(unsigned j=0;j<64u;++j)out[{start}+i*64u+j]=observer.owners[i].armed && j<observer.owners[i].filter.swi_count?observer.owners[i].filter.swis[j]:0;')
                continue
            if name=='rpc_session.arc_rpc_refused':
                choices=','.join('NULL' if v is None else json.dumps(v) for v in f['index_values'])
                body.append(f'{{const char *const choices[]={{ {choices} }};out[{start}]=arc_rpc_lockstep_string_index(arc_rpc_refused,choices,{len(f["index_values"])});}}');continue
            if 'c_identity' in f:
                e=f['c_identity']['expression'];root=e.split('.')[0]
                numeric=(name in ('arm.flaglookup','arm.stmlookup','arm.countbitstable') or
                         name.startswith('rpc_input.') or name=='rpc_observers.bits' or name.endswith('_swis'))
                if numeric:
                    body.append(f'out[{start}]=arc_rpc_lockstep_identity({fid}u,&{e},sizeof({e}));')
                else:
                    symbol=e.split('.')[-1]
                    raw=initializer(source,symbol)
                    value=int.from_bytes(hashlib.sha256(raw).digest()[:8],'little')
                    body.append(f'out[{start}]=UINT64_C({value});')
                continue
            if not expr: raise ValueError('missing C expression: '+name)
            if '.shortened.' in expr:expr=expr.replace('arc_rpc_observers_test_horizon.shortened','arc_rpc_lockstep_shortened')
            dims=f.get('c_dimensions',[])
            if math.prod(dims)!=f['count']:raise ValueError('array capacity drift: '+name)
            for level,dimension in enumerate(dims):
                array=expr[:expr.index(f'[j{level}]')]
                array=re.sub(r'\[j\d+\]','[0]',array)
                body.append(f'_Static_assert(sizeof({array})/sizeof(({array})[0])=={dimension}u,"classified array capacity: {name}");')
            loops=''.join(f'for(unsigned j{i}=0;j{i}<{d}u;++j{i})' for i,d in enumerate(dims))
            idx='0'
            for i,d in enumerate(dims):idx=f'(({idx})*{d}u+j{i})'
            index=f'{start}u+({idx})*{f["width"]}u'
            if f['type']=='allocation-offset':
                value=f'arc_rpc_lockstep_pointer(out+{index},(const void *)(uintptr_t)({expr}),{fid}u,{idx});'
                if name=='tlb.code':value='if(tlb[j0].valid){'+value+'}else{out['+index+']=out['+index+'+1]=0;}'
            elif f['type']=='index':
                value=f'out[{index}]=arc_rpc_lockstep_register_index({expr});'
            elif f.get('c_type')=='double':
                value=f'out[{index}]=arc_rpc_lockstep_double({expr});'
            elif f.get('c_type')=='char':
                value=f'out[{index}]=(uint64_t)(uint8_t)({expr});'
            else: value=f'out[{index}]=(uint64_t)({expr});'
            if f.get('c_guard'):
                value=f'if({f["c_guard"]}){{{value}}}else{{out[{index}]=0;}}'
            body.append(loops+'{'+value+'}')
        body.append('}')
        rows=layout_rows(schema,source,fields);layouts[source]=[{k:v for k,v in r.items() if k!='values'} for r in rows]
        layout_values=[]
        for row in rows:
            values=list(row['values'])
            if row['dimensions']>4:raise ValueError('layout fixture dimension bound')
            values.extend([0]*(13-len(values)));layout_values.extend(map(str,values))
        header.append('#define ARC_RPC_LS_LAYOUT_'+tu+' const uint64_t arc_lockstep_layout[]={'+','.join(layout_values)+'};')
        header.append(f'void arc_rpc_lockstep_collect_{tu}(uint64_t *);')
        header.append('#define ARC_RPC_LS_SERIALIZE_'+tu+' \\\n'+' \\\n'.join(body))
    header.append('#endif')
    (directory/'rpc_lockstep_generated.h').write_text('\n'.join(header)+'\n', encoding='utf-8')
    tables=['/* SPDX-License-Identifier: GPL-2.0-only; generated, do not commit. */',
            'typedef struct { unsigned offset,count,width,group,simple,modes,tu,wire; } LsField;',
            'static const LsField ls_fields[ARC_RPC_LS_FIELDS]={']
    for f in fields.values():
        simple=0 if f['condition'] is False or f['class']=='output-compared' else 1 if f['condition'] is True and not f.get('mode_conditions') else 2
        modes=sum(1<<schema['modes'].index(m) for m in f['mode_exclusions'])
        wire=int(f['condition'] is not False and f['class']!='output-compared')
        tables.append('{%du,%du,%du,%du,%du,%du,%du,%du},'%(f['offset'],f['count'],f['width'],schema['groups'].index(f['group']),simple,modes,tu_ids.get(f['source'],63),wire))
    tables+=['};','static const char *const ls_pointer_errors[ARC_RPC_LS_FIELDS]={']
    tables += [json.dumps('lockstep unmapped pointer: '+f['name'])+',' for f in fields.values()]
    tables+=['};','static bool ls_included(unsigned field,unsigned i,unsigned phase,unsigned mode,const uint64_t *v) {',
             '(void)i;(void)phase;(void)mode;(void)v;switch(field){']
    for fid,f in enumerate(fields.values()):
        expr='0u' if f['class']=='output-compared' else expression(f['condition'],fields)
        for mode in f['mode_exclusions']:expr=f'({expr} && mode!={schema["modes"].index(mode)}u)'
        for mode,cond in f.get('mode_conditions',{}).items():expr=f'({expr} && (mode!={schema["modes"].index(mode)}u || {expression(cond,fields)}))'
        tables.append(f'case {fid}u:return {expr};')
    tables+=['default:return false;}}',
        'static uint64_t ls_condition_signature(unsigned field,unsigned phase,unsigned mode,const uint64_t *v) {',
        'uint64_t h=mode;(void)phase;(void)v;switch(field){']
    def references(e):
        if not isinstance(e,dict):return set()
        if 'field' in e:return {e['field']}
        return set().union(*(references(x) for value in e.values()
            for x in (value if isinstance(value,list) else [value])))
    for fid,f in enumerate(fields.values()):
        refs=references(f['condition'])
        for cond in f.get('mode_conditions',{}).values():refs|=references(cond)
        phase_axis='"phase"' in json.dumps([f['condition'],f.get('mode_conditions',{})])
        if not refs and not phase_axis:continue
        tables.append(f'case {fid}u:')
        if phase_axis:tables.append('h^=(uint64_t)phase<<32;')
        for name in sorted(refs):
            dependency=fields[name]
            tables.append(f'for(unsigned j=0;j<{dependency["count"]*dependency["width"]}u;++j)h=(h^v[{dependency["offset"]}u+j])*UINT64_C(1099511628211);')
        tables.append('break;')
    tables+=['default:break;}return h;}', '#if !defined(ARC_RPC_LS_POLICY_ONLY)','static void ls_collect(uint64_t *v) {']
    sources=sorted({f['source'] for f in fields.values()}-{'','engine/rpc/rpc_profile.c','engine/rpc/rpc_crosscheck.c'})
    first=('rpc_session','arm','cmos','rpc_observers','vidc20')
    sources.sort(key=lambda s:(first.index(Path(s).stem) if Path(s).stem in first else len(first),s))
    for source in sources:
        tu=Path(source).stem
        tables.append(f'if(arc_rpc_lockstep_collect_due({tu_ids[source]}u,{str(tu not in cached).lower()}))arc_rpc_lockstep_collect_{tu}(v);')
    tables+=['}','#endif']
    for name,value in [('compatibility',compatibility(schema)),('schema',hashlib.sha256(canonical(schema)).digest())]:
        tables.append(f'static const uint8_t ls_{name}_hash[32]={{'+','.join(str(x) for x in value)+'};')
    (directory/'rpc_lockstep_tables.h').write_text('\n'.join(tables)+'\n', encoding='utf-8')
    outputs={name:hashlib.sha256((directory/name).read_bytes()).hexdigest()
        for name in ('rpc_lockstep_generated.h','rpc_lockstep_tables.h')}
    (directory/'rpc_lockstep_codegen.json').write_text(json.dumps(dict(license='GPL-2.0-only',
        words=offset,fields=len(fields),compatibility=compatibility(schema).hex(),
        metadata=hashlib.sha256(canonical(schema)).hexdigest(),outputs=outputs,layouts=layouts),indent=2)+'\n', encoding='utf-8')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path)
    p.add_argument('--schema',type=Path,default=ROOT/'tests/rpc_lockstep_schema.json')
    p.add_argument('--policy',type=Path,default=ROOT/'tests/rpc_lockstep_reviewed_policy.json')
    p.add_argument('--check',action='store_true')
    a=p.parse_args();schema=load(a.schema,a.policy)
    if a.check:
        manifest=json.loads((a.output/'rpc_lockstep_codegen.json').read_text(encoding='utf-8'))
        if manifest['metadata']!=hashlib.sha256(canonical(schema)).hexdigest():raise ValueError('stale schema metadata: regenerate C tables')
        for name,digest in manifest['outputs'].items():
            if hashlib.sha256((a.output/name).read_bytes()).hexdigest()!=digest:raise ValueError('generated C table drift: '+name)
    else:generate(schema,a.output)


if __name__=='__main__':main()
