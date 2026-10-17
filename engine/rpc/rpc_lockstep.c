/* SPDX-License-Identifier: GPL-2.0-only */
/* D-200: non-installed lockstep transport. Never calls a guest mutator. */
#include "rpc_session.h"
#include "rpc_lockstep.h"
#include "rpc_lockstep_generated.h"
#include "arcdroid_core.h"
#include "rpc_mem_timing.h"
#include "arm.h"
#include "mem.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if !defined(ARCDROID_RPC_LOCKSTEP)
#error "The emitter belongs only to the lockstep core"
#endif

static struct {
    ArcRpcLockstepSink sink; void *opaque; ArcRpcLockstepConfig config;
    const char *error;
    char error_text[128];
    bool ended, writing, prepared, open, pending_epoch, null_batch;
    unsigned depth, phase, edge, site, sequence, next_sequence, mode, agent;
    uint64_t generation, epoch, next_k, k, events, counts[3], phases[2];
    uint64_t boundaries[5], outside_helpers, base_executed, completed, epoch_completed;
    uint64_t base_work, limit, video_pixels, video_fields, event_work;
    uint64_t continuations;
    ArcSha256 output;
    bool field_completed, stop_at_field;
    unsigned observed_mode, linecyc;
    uint64_t dirty_tus,valid_tus,collected_tus;
    struct { const uint8_t *base; size_t size; } allocations[64];
    struct { void *base,*shifted; } shifted[8];
    uint8_t *shadow[3];
    uint8_t dirty[2][16896], page_valid[16896], page_hash[16896][32];
    uint8_t saved_dirty[2][16896];
    uint64_t raw[ARC_RPC_LS_WORDS], projected[ARC_RPC_LS_WORDS], saved[ARC_RPC_LS_WORDS];
    uint64_t cached[ARC_RPC_LS_WORDS], inclusion[ARC_RPC_LS_FIELDS];
    uint64_t field_hash[ARC_RPC_LS_FIELDS], identity[ARC_RPC_LS_FIELDS];
    uint64_t group_hash[ARC_RPC_LS_GROUPS];
    bool field_valid[ARC_RPC_LS_FIELDS], identity_valid[ARC_RPC_LS_FIELDS];
    bool group_valid[ARC_RPC_LS_GROUPS];
    uint8_t frame[16u*1024u*1024u+5u];
} ls;

#include "rpc_lockstep_tables.h"
#include "rpc_lockstep_digest.h"

static bool active(void)
{
    if(ls.writing){if(!ls.error)ls.error="lockstep sink reentrancy";return false;}
    return ls.sink && !ls.ended && !ls.error;
}
static void fail(const char *error) { if (!ls.error) ls.error=error; }
void arc_rpc_lockstep_dirty(unsigned tu) { ls.dirty_tus|=UINT64_C(1)<<tu; }
bool arc_rpc_lockstep_collect_due(unsigned tu,bool always)
{
    uint64_t bit=UINT64_C(1)<<tu;
    if(always || !(ls.valid_tus&bit) || (ls.dirty_tus&bit)){
        ls.collected_tus|=bit;ls.valid_tus|=bit;ls.dirty_tus&=~bit;return true;
    }
    return false;
}
void *arc_rpc_lockstep_alloc(size_t bytes)
{
    size_t padding=(size_t)ls.config.allocation_shift*64u;
    if(!padding)return malloc(bytes);
    if(bytes>SIZE_MAX-padding)return NULL;
    for(unsigned i=0;i<8;++i)if(!ls.shifted[i].base){
        uint8_t *base=malloc(bytes+padding);if(!base)return NULL;
        ls.shifted[i].base=base;ls.shifted[i].shifted=base+padding;return base+padding;
    }
    fail("lockstep shifted allocation capacity");return NULL;
}
void arc_rpc_lockstep_free(void *pointer)
{
    for(unsigned i=0;i<8;++i)if(pointer && pointer==ls.shifted[i].shifted){
        free(ls.shifted[i].base);ls.shifted[i].base=ls.shifted[i].shifted=NULL;return;
    }
    free(pointer);
}
static void put64(uint8_t *out, uint64_t n)
{ for(unsigned i=0;i<8;++i)out[i]=(uint8_t)(n>>(8*i)); }
static uint64_t get64(const uint8_t *in)
{ uint64_t n=0;for(unsigned i=0;i<8;++i)n|=(uint64_t)in[i]<<(8*i);return n; }
static void put32(uint8_t *out, uint32_t n)
{ for(unsigned i=0;i<4;++i)out[i]=(uint8_t)(n>>(8*i)); }
void arc_rpc_lockstep_output_word(uint64_t value)
{ uint8_t bytes[8];put64(bytes,value);arc_sha256_update(&ls.output,bytes,8); }
static void hash_words(ArcSha256 *h, const uint64_t *p, size_t n)
{ uint8_t bytes[8];for(size_t i=0;i<n;++i){put64(bytes,p[i]);arc_sha256_update(h,bytes,8);} }
static uint64_t finish_hash(const ArcSha256 *h)
{ uint8_t digest[32];arc_sha256_final(h,digest);return get64(digest); }
void arc_rpc_lockstep_content_begin(void) { arc_sha256_init(&ls.output); }
void arc_rpc_lockstep_content_bytes(const void *bytes,size_t size)
{ if(bytes && size)arc_sha256_update(&ls.output,bytes,size); }
uint64_t arc_rpc_lockstep_content_end(void) { return finish_hash(&ls.output); }
static uint64_t text_hash(const char *p)
{ ArcSha256 h;arc_sha256_init(&h);arc_sha256_update(&h,p,strlen(p));return finish_hash(&h); }
static bool send_frame(unsigned kind, size_t size)
{
    if(!active())return false;
    if(ls.writing || size>sizeof(ls.frame)-5u){fail("lockstep reentrancy/frame bound");return false;}
    ls.frame[0]=(uint8_t)kind;put32(ls.frame+1,(uint32_t)size);ls.writing=true;
    bool ok=ls.sink(ls.opaque,ls.frame,size+5);ls.writing=false;
    if(!ok)fail("lockstep sink error");
    return ok;
}
static void key(uint8_t *p, unsigned phase)
{
    uint64_t words[6]={ls.generation,ls.epoch,ls.k,phase,
        phase==1 || phase==5?ls.site:0,phase==1 || phase==5?ls.sequence:0};
    for(unsigned i=0;i<6;++i)put64(p+i*8,words[i]);
}
static uint8_t *payload(void) { return ls.frame+5; }

void arc_rpc_lockstep_register(unsigned id, const void *base, size_t size)
{
    if(id>=64){fail("lockstep allocation identity bound");return;}
    ls.allocations[id].base=base;ls.allocations[id].size=size;
}
void arc_rpc_lockstep_pointer(uint64_t *out,const void *pointer,unsigned field,unsigned index)
{
    out[0]=out[1]=0;if(!pointer)return;
    /* Owner callback/context identity is semantic, assigned by the API slot. */
    if(field<ARC_RPC_LS_FIELDS){
        unsigned off=ls_fields[field].offset;
        if(off==LS_fdc__media__drives__file){out[0]=7;out[1]=index;return;}
        if(off==LS_ide__ide__backend__opaque){out[0]=7;out[1]=2;return;}
        if(off==LS_fdc__fdc__sector__view__data){out[0]=7;out[1]=3;return;}
        if(off==LS_observer__owners__filter__swis){out[0]=6;out[1]=index*64u;return;}
        if(off==LS_observer__owners__filter__callback || off==LS_observer__owners__filter__opaque ||
            (off>=LS_observer__handlers__valid && off<=LS_observer__handlers__opaque) ||
            off==LS_rpc_observers__state__resolve || off==LS_rpc_observers__state__observe ||
            off==LS_rpc_observers__state__opaque || off==LS_rpc_floppy__raw_refusal ||
            off==LS_cmos__arc_rpc_cmos_write_clock ||
            off==LS_ide__ide__backend__read || off==LS_ide__ide__backend__write ||
            off==LS_ide__ide__backend__failed || off==LS_ide__ide__backend__unsupported){
            out[0]=11;out[1]=index;return;
        }
    }
    uintptr_t p=(uintptr_t)pointer;
    for(unsigned id=1;id<64;++id){
        uintptr_t base=(uintptr_t)ls.allocations[id].base;
        if(base && p>=base && p-base<ls.allocations[id].size){out[0]=id;out[1]=p-base;return;}
    }
    (void)index;fail(field<ARC_RPC_LS_FIELDS?ls_pointer_errors[field]:"lockstep unmapped pointer");
}
uint64_t arc_rpc_lockstep_register_index(const uint32_t *p)
{
    uintptr_t value=(uintptr_t)p,base=(uintptr_t)&arm.reg[0];
    if(value<base || value-base>=sizeof(arm.reg) || (value-base)%4){fail("lockstep register pointer");return 0;}
    return (value-base)/4;
}
uint64_t arc_rpc_lockstep_double(double value)
{ uint64_t bits;_Static_assert(sizeof(bits)==sizeof(value),"binary64 required");memcpy(&bits,&value,8);return bits; }
uint64_t arc_rpc_lockstep_identity(unsigned field,const void *data,size_t bytes)
{
    if(!ls.identity_valid[field]){
        ArcSha256 h;arc_sha256_init(&h);arc_sha256_update(&h,data,bytes);
        ls.identity[field]=finish_hash(&h);ls.identity_valid[field]=true;
    }
    return ls.identity[field];
}
uint64_t arc_rpc_lockstep_string_index(const char *p,const char *const *choices,unsigned count)
{
    if(!p)return 0;
    for(unsigned i=1;i<count;++i)if(!strcmp(p,choices[i]))return i;
    fail("lockstep unassigned string identity");return 0;
}
static void memory_register(void)
{
    arc_rpc_lockstep_register(1,rom,8u<<20);arc_rpc_lockstep_register(2,ram00,32u<<20);
    arc_rpc_lockstep_register(3,ram01,32u<<20);arc_rpc_lockstep_register(4,vram,vram?2u<<20:0);
}
#include "rpc_lockstep_arithmetic.h"
static void project(void)
{
    uint64_t *v=ls.projected;
    for(unsigned f=0;f<ARC_RPC_LS_FIELDS;++f){const LsField *field=ls_fields+f;
        if(ls.collected_tus&(UINT64_C(1)<<field->tu))
            memcpy(v+field->offset,ls.raw+field->offset,field->count*field->width*8u);}
    v[LS_driver__linecyc]=ls.phase==2?ls.linecyc-1:ls.linecyc;
    v[LS_driver__successful_continuations]=ls.continuations;
    v[LS_driver__instruction_work]=ls.base_work-(ls.null_batch?1u:0u)+ls.completed;
    v[LS_driver__event_work]=ls.event_work;v[LS_driver__video_start__pixels]=ls.video_pixels;
    v[LS_driver__video_start__fields]=ls.video_fields;v[LS_driver__limit]=ls.limit;
    v[LS_driver__completed]=ls.field_completed;v[LS_driver__stop_at_field]=ls.stop_at_field;
    v[LS_driver__attempts_completed]=ls.epoch_completed;
    if(ls.mode==1 && ls.phase!=3)v[LS_arm__inscount]=(uint32_t)(v[LS_arm__inscount]+ls.completed); /* R-M2b-O3 */
    v[LS_rpc_session__arc_rpc_executed]=ls.base_executed+ls.completed;
    if(ls.phase==0 || ls.phase==1 || ls.phase==2){
        uint64_t issue=v[LS_fast__issue_ticks],d=v[LS_fast__denominator];
        if(v[LS_fast__integer_batch]){
            uint64_t fraction=v[LS_fast__issue_fraction];
            uint64_t now=v[LS_scheduler__now_ns];
            if(now<(fraction!=0)){fail("lockstep projection underflow");return;}
            uint64_t floor=now-(fraction!=0);
            if(!ticks(floor,fraction,d,&issue))return;
            v[LS_fast__issue_ticks]=issue;v[LS_fast__issue_floor]=floor;
        }
        if(d && !(ls.phase==2 && v[LS_fast__pending])){
            uint64_t due=issue;
            if(ls.phase==2 && !add(issue,v[LS_fast__base_ticks],&due))return;
            v[LS_fast__cpu_due_ticks]=due;v[LS_fast__due_floor]=due/d;
            v[LS_fast__due_fraction]=due%d;v[LS_fast__cpu_due_ns]=due/d+(due%d!=0);
        }
    }
    if(ls.mode==1 || ls.mode==3){
        if(v[LS_observer__batch__live] && v[LS_rpc_observer__observers_active])
            v[LS_observer__instruction]+=v[LS_fast__instructions]-v[LS_observer__batch__start]-v[LS_observer__batch__accounted];
    }
}
static void checkpoint(unsigned phase,unsigned edge)
{
    if(!active())return;
    ls.phase=phase;ls.edge=edge;memory_register();
    ls.collected_tus=0;ls_collect(ls.raw);
    if(ls.error)return;
    uint64_t collected=ls.collected_tus;
    for(unsigned f=0;f<ARC_RPC_LS_FIELDS;++f){const LsField *field=ls_fields+f;
        if(collected&(UINT64_C(1)<<field->tu))
            memcpy(ls.saved+field->offset,ls.raw+field->offset,field->count*field->width*8u);}
    uint64_t dirty_before=ls.dirty_tus;
    memcpy(ls.saved_dirty,ls.dirty,sizeof(ls.dirty));
    project();
    /* Projection must not change any serialized state, including dead slots. */
    ls_collect(ls.raw);
    if(ls.dirty_tus!=dirty_before){fail("lockstep projection changed device generation");return;}
    if(memcmp(ls.saved_dirty,ls.dirty,sizeof(ls.dirty))){fail("lockstep projection changed RAM dirty sets");return;}
    /* The complete state vector includes cached device chunks. Their dirty
     * generations were checked above; only collected chunks can differ. */
    for(unsigned f=0;f<ARC_RPC_LS_FIELDS;++f){const LsField *field=ls_fields+f;
        if((collected&(UINT64_C(1)<<field->tu)) &&
            memcmp(ls.raw+field->offset,ls.saved+field->offset,field->count*field->width*8u)){
            fail("lockstep mutating projection");return;
        }
    }
    uint64_t groups[ARC_RPC_LS_GROUPS];ArcSha256 h[ARC_RPC_LS_GROUPS];
    bool group_changed[ARC_RPC_LS_GROUPS]={false};
    for(unsigned g=0;g<ARC_RPC_LS_GROUPS;++g)group_changed[g]=!ls.group_valid[g];
    for(unsigned f=0;f<ARC_RPC_LS_FIELDS;++f){
        const LsField *field=ls_fields+f;
        if(!field->wire)continue;
        bool enabled=!(field->modes&(1u<<ls.mode)) && field->simple;
        uint64_t signature=enabled?1:0;
        if(enabled && field->simple==2)
            signature=ls_condition_signature(f,phase,ls.mode,ls.projected);
        size_t bytes=field->count*field->width*sizeof(uint64_t);
        bool changed=!ls.field_valid[f] || signature!=ls.inclusion[f] ||
            (enabled && (field->tu==63 || (collected&(UINT64_C(1)<<field->tu))) &&
                memcmp(ls.cached+field->offset,ls.projected+field->offset,bytes));
        if(changed){
            uint64_t value=arc_rpc_lockstep_digest_field(f,ls.projected,phase,ls.mode);
            if(!ls.field_valid[f] || value!=ls.field_hash[f])group_changed[field->group]=true;
            ls.field_hash[f]=value;ls.field_valid[f]=true;ls.inclusion[f]=signature;
        }
    }
    for(unsigned g=0;g<ARC_RPC_LS_GROUPS;++g)if(group_changed[g])arc_sha256_init(h+g);
    for(unsigned f=0;f<ARC_RPC_LS_FIELDS;++f){const LsField *field=ls_fields+f;
        if(field->wire && group_changed[field->group])hash_words(h+field->group,ls.field_hash+f,1);}
    for(unsigned f=0;f<ARC_RPC_LS_FIELDS;++f){const LsField *field=ls_fields+f;
        if(field->tu==63 || (collected&(UINT64_C(1)<<field->tu)))
            memcpy(ls.cached+field->offset,ls.projected+field->offset,field->count*field->width*8u);}
    for(unsigned g=0;g<ARC_RPC_LS_GROUPS;++g){
        if(group_changed[g]){ls.group_hash[g]=finish_hash(h+g);ls.group_valid[g]=true;}
        groups[g]=ls.group_hash[g];
    }
    uint8_t *p=payload();key(p,phase);put64(p+48,edge);
    bool detail=(ls.config.flags&ARC_RPC_LOCKSTEP_DETAIL) && ls.k>=ls.config.detail_first && ls.k<=ls.config.detail_last &&
        (!ls.config.detail_generation || ls.generation==ls.config.detail_generation) &&
        (!ls.config.detail_epoch || ls.epoch==ls.config.detail_epoch);
    size_t detail_words=0;
    if(detail){for(unsigned f=0;f<ARC_RPC_LS_FIELDS;++f){const LsField *field=ls_fields+f;
        if(field->wire)for(unsigned i=0;i<field->count*field->width;++i)
            put64(p+56+8*detail_words++,ls.projected[field->offset+i]);}}
    else {for(unsigned i=0;i<ARC_RPC_LS_GROUPS;++i)put64(p+56+8*i,groups[i]);}
    send_frame(detail?1:2,56+8*(detail?detail_words:ARC_RPC_LS_GROUPS));
    ++ls.counts[0];if(phase==0 || phase==2)++ls.phases[phase/2];
}
static uint8_t *bank(unsigned b)
{ return b==0?(uint8_t *)ram00:b==1?(uint8_t *)ram01:(uint8_t *)vram; }
static unsigned bank_size(unsigned b) { return b<2?32u<<20:vram?2u<<20:0; }
static void pages(unsigned boundary,bool full)
{
    if(!active())return;
    unsigned phase=boundary==0?3:boundary==4?5:0;
    uint64_t saved_k=ls.k;if(phase==0)ls.k=ls.next_k;
    uint64_t occurrence=ls.boundaries[boundary]++;
    uint8_t *p=payload();key(p,phase);put64(p+48,boundary);put64(p+56,occurrence);
    put64(p+64,0);put64(p+72,ls.prepared?1:0);
    arc_sha256_init(&ls.output);
    if(boundary!=0){arc_rpc_lockstep_video_output();arc_rpc_lockstep_audio_output();}
    arc_sha256_final(&ls.output,p+80);
    send_frame(4,112);++ls.counts[2];
    if(ls.prepared)for(unsigned b=0;b<3;++b){
        uint8_t *data=bank(b);unsigned size=bank_size(b),base=b==0?0:b==1?8192:16384;
        if(!data || !size)continue;
        if((ls.config.flags&ARC_RPC_LOCKSTEP_AUDIT) && boundary==0 && ls.shadow[b]){
            if(memcmp(data,ls.shadow[b],size)){
                unsigned page=0;while(page<size/4096 && !memcmp(data+page*4096,ls.shadow[b]+page*4096,4096))++page;
                snprintf(ls.error_text,sizeof(ls.error_text),"lockstep unlogged write: allocation %u page %u",b+2,page);
                fail(ls.error_text);break;
            }
        }
        for(unsigned page=0;page<size/4096;++page){
            unsigned n=base+page;
            if(!full && !ls.dirty[boundary==1][n])continue;
            if(!ls.page_valid[n]){
                ArcSha256 hash;arc_sha256_init(&hash);arc_sha256_update(&hash,data+page*4096,4096);
                arc_sha256_final(&hash,ls.page_hash[n]);ls.page_valid[n]=true;
            }
            key(p,phase);put64(p+48,boundary);put64(p+56,occurrence);
            put64(p+64,b+2);put64(p+72,page);memcpy(p+80,ls.page_hash[n],32);
            send_frame(4,112);++ls.counts[2];
        }
    }
    if(boundary<2)memset(ls.dirty[boundary],0,sizeof(ls.dirty[boundary]));
    ls.k=saved_k;
}
void arc_rpc_lockstep_batch(uint64_t executed,unsigned work,bool null_batch)
{ ls.base_executed=executed;ls.base_work=work;ls.completed=0;ls.null_batch=null_batch;ls.linecyc=0; }
void arc_rpc_lockstep_driver(uint64_t limit,unsigned events,uint64_t pixels,uint64_t fields,bool completed,bool stop)
{ ls.limit=limit;ls.event_work=events;ls.video_pixels=pixels;ls.video_fields=fields;ls.field_completed=completed;ls.stop_at_field=stop; }
void arc_rpc_lockstep_p0(unsigned mode)
{
    if(!active())return;
    ls.k=ls.next_k++;ls.site=ls.sequence=ls.next_sequence=0;
    ls.open=true;ls.observed_mode=mode;checkpoint(0,0);
}
void arc_rpc_lockstep_p1(unsigned site,unsigned edge)
{
    if(!active())return;
    if(!ls.open){++ls.outside_helpers;return;}
    if(site==8){if(ls.observed_mode==arm.mode)return;ls.observed_mode=arm.mode;}
    if(edge!=2 && edge!=4){ls.site=site;ls.sequence=ls.next_sequence++;}
    /* edge=3 reserves a fetch identity before the descriptor walk. */
    if(edge==3 || (ls.config.flags&ARC_RPC_LOCKSTEP_P1_OFF))return;
    checkpoint(1,edge==4?0:edge);
}
void arc_rpc_lockstep_p2(void)
{
    if(!active())return;
    ++ls.completed;++ls.epoch_completed;++ls.linecyc;checkpoint(2,0);ls.open=false;
}
void arc_rpc_lockstep_continuation(bool batch) { if(batch)++ls.continuations; }
void arc_rpc_lockstep_p3(unsigned executed)
{ if(active() && executed){ls.completed=executed;ls.linecyc=executed;pages(0,(ls.config.flags&ARC_RPC_LOCKSTEP_AUDIT)!=0);checkpoint(3,0);} }
void arc_rpc_lockstep_prepared(bool prepared)
{
    ls.prepared=prepared;
    if(prepared && active() && (ls.config.flags&ARC_RPC_LOCKSTEP_AUDIT))for(unsigned b=0;b<3;++b){
        unsigned size=bank_size(b);if(!size)continue;
        if(!ls.shadow[b])ls.shadow[b]=malloc(size);
        if(!ls.shadow[b]){fail("lockstep audit allocation");return;}
        memcpy(ls.shadow[b],bank(b),size);
    }
    memset(ls.page_valid,0,sizeof(ls.page_valid));
}
void arc_rpc_lockstep_epoch_begin(void)
{
    if(!ls.depth && active()){pages(2,true);ls.pending_epoch=true;ls.prepared=false;}
    ++ls.depth;
}
void arc_rpc_lockstep_epoch_end(uint64_t generation,bool prepared)
{
    if(ls.depth)--ls.depth;if(ls.depth)return;
    if(ls.pending_epoch){++ls.epoch;ls.next_k=ls.k=ls.events=ls.epoch_completed=ls.continuations=0;
        ls.open=false;ls.site=ls.sequence=ls.next_sequence=0;ls.pending_epoch=false;
        memset(ls.identity_valid,0,sizeof(ls.identity_valid));
        ls.valid_tus=0;
        memset(ls.dirty,0,sizeof(ls.dirty));}
    ls.generation=generation;arc_rpc_lockstep_prepared(prepared);
    pages(3,true);
}
void arc_rpc_lockstep_epoch_abort(uint64_t generation)
{
    if(ls.pending_epoch && ls.depth){ls.depth=1;arc_rpc_lockstep_epoch_end(generation,false);}
}
void arc_rpc_lockstep_agent(unsigned agent) { ls.agent=agent; }
static uint64_t issue_time(void)
{
    ArcRpcMemFast f=arc_rpc_mem_fast;uint64_t at=f.issue_ticks;
    if(f.integer_batch && f.clock){uint64_t floor=f.clock->now_ns-(f.issue_fraction!=0);ticks(floor,f.issue_fraction,f.denominator,&at);}
    return at;
}
static void event(uint32_t address,unsigned size,bool write,uint64_t value,unsigned kind)
{
    if(!active())return;
    uint64_t words[14]={ls.generation,ls.epoch,ls.events++,ls.open?ls.k:ls.next_k,
        ls.open?1:4,ls.open?ls.site:0,ls.open?ls.sequence:0,
        issue_time(),ls.agent,kind,address,size,write,value};
    for(unsigned i=0;i<14;++i)put64(payload()+8*i,words[i]);send_frame(3,112);++ls.counts[1];
}
void arc_rpc_lockstep_ram_write(uint32_t address,unsigned size,uint64_t value)
{
    if(!active())return;
    event(address,size,true,value,0);
    uint32_t pa=address&0x1fffffffu;unsigned b;
    if(pa>=0x10000000u && pa<0x14000000u)b=0;
    else if(pa>=0x14000000u && pa<0x18000000u)b=1;
    else if(pa>=0x02000000u && pa<0x03000000u && vram)b=2;
    else {fail("lockstep write outside backing");return;}
    unsigned offset=pa&(bank_size(b)-1u);if(size==4)offset&=~3u;
    unsigned n=(b<2?b*8192:16384)+offset/4096;
    ls.dirty[0][n]=ls.dirty[1][n]=true;ls.page_valid[n]=false;
    if(ls.shadow[b])for(unsigned i=0;i<size;++i)ls.shadow[b][offset+i]=(uint8_t)(value>>(8*i));
}
void arc_rpc_lockstep_mmio(uint32_t address,unsigned size,bool write,uint64_t value)
{ event(address,size,write,value,1); }
void arc_rpc_lockstep_frame(void) { pages(1,false); }
bool arc_rpc_lockstep_inject(unsigned kind)
{ return active() && ls.config.inject_kind==kind && (ls.open?ls.k:ls.next_k)==ls.config.inject_k; }
void arc_rpc_lockstep_fatal(const char *format,const char *text)
{
    if(!active())return;
    if(ls.pending_epoch){
        ++ls.epoch;ls.pending_epoch=false;ls.depth=0;ls.next_k=ls.k=ls.events=0;
        ls.open=false;ls.site=ls.sequence=ls.next_sequence=0;
    }
    if(!ls.open)ls.k=ls.next_k;
    ls.phase=5;pages(4,true);uint8_t *p=payload();key(p,5);put64(p+48,text_hash(format));put64(p+56,text_hash(text));
    send_frame(5,64);(void)arc_rpc_test_lockstep_end();
}
void arc_rpc_lockstep_fatal_guard(void)
{
    /* Q-9 has no post-fatal digest. Assert the emission boundary before
     * publishing the fault and retiring state that the stream cannot retain. */
    if(ls.sink && !ls.ended && !ls.error)fail("lockstep late fatal emission");
}
RETRO_API bool arc_rpc_test_lockstep_sink(ArcRpcLockstepSink sink,void *opaque,const ArcRpcLockstepConfig *config)
{
    if(!sink || !config || config->version!=1 || config->size!=sizeof(*config) || config->model>=4 ||
        config->flags&~0x31fu || (config->flags>>8)>3 || active())return false;
    __typeof__(ls.shifted) shifted;memcpy(shifted,ls.shifted,sizeof(shifted));
    bool prepared=ls.prepared;unsigned depth=ls.depth;
    for(unsigned b=0;b<3;++b)free(ls.shadow[b]);
    memset(&ls,0,sizeof(ls));ls.sink=sink;ls.opaque=opaque;ls.config=*config;ls.mode=config->flags>>8;
    memcpy(ls.shifted,shifted,sizeof(shifted));ls.depth=depth;ls.pending_epoch=depth!=0;
    ls.generation=arc_rpc_session_generation();ls.epoch=1;
    const uint8_t magic[8]={'A','R','C','L','K','v','1',0};
    ls.writing=true;bool magic_ok=sink(opaque,magic,8);ls.writing=false;
    if(!magic_ok || ls.error){fail("lockstep sink error");return false;}
    uint8_t *p=payload();put32(p,1);memcpy(p+4,ls_compatibility_hash,32);put32(p+36,config->model);
    memcpy(p+40,config->plan,32);put64(p+72,config->seed);memcpy(p+80,config->schedule,32);
    memcpy(p+112,config->configuration,32);put32(p+144,config->flags);
    memcpy(p+148,ls_schema_hash,32);
    bool ok=send_frame(0,180);arc_rpc_lockstep_prepared(prepared);return ok;
}
RETRO_API const char *arc_rpc_test_lockstep_status(void) { return ls.error; }
RETRO_API bool arc_rpc_test_lockstep_end(void)
{
    if(ls.ended)return !ls.error;
    if(!active())return false;
    if(ls.phase!=5)pages(2,true);
    uint64_t words[8]={ls.counts[0],ls.counts[1],ls.counts[2],ls.phases[0],ls.phases[1],0,0,0};
    for(unsigned i=0;i<8;++i)put64(payload()+8*i,words[i]);bool ok=send_frame(6,64);ls.ended=true;
    for(unsigned b=0;b<3;++b){free(ls.shadow[b]);ls.shadow[b]=NULL;}
    return ok;
}
