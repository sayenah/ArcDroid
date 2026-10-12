/* Independent D-139 service boundary oracle. GPL-2.0-only.
 * Authored opcodes/requests drive the same timing service as the ROM tests.
 * Expectations below are equations from riscpc-timing.md, never core output.
 * Covers initial states deliberately inaccessible as production RAM options. */
#include "rpc_mem_timing.h"
#include "iomd.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct iomd iomd;
void fatal(const char *format,...)
{ va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);abort(); }
static ArcRpcScheduler clock_state;
static unsigned model,rate;
static uint64_t b,q;
static uint64_t next_hold=UINT64_MAX,next_frontend=UINT64_MAX;
static unsigned remaining_holds,frontend_kind;
static uint64_t hold_ns=350;
static void init(unsigned cpu,unsigned speed)
{
    model=cpu;rate=speed;b=28000;q=875u*rate;
    next_hold=next_frontend=UINT64_MAX;remaining_holds=0;
    hold_ns=350;
    memset(&iomd,0,sizeof(iomd));iomd.clkctrl=7;iomd.romcr0=cpu==3?64:0;
    assert(arc_rpc_scheduler_init(&clock_state,rate));arc_rpc_mem_bind(&clock_state,model,rate);
    arc_rpc_mem_clock_switch(true);arc_rpc_mem_control(0x100d);
    arc_rpc_mem_measure(1,UINT64_MAX,false);
}
static void step_time(bool idle)
{
    uint64_t next=arc_rpc_mem_next(UINT64_MAX,idle);assert(next>clock_state.now_ns);
    if(next_hold<next)next=next_hold;
    if(next_frontend<next)next=next_frontend;
    clock_state.now_ns=next;
    if(next==next_hold) {
        arc_rpc_mem_video_hold(hold_ns);
        next_hold=--remaining_holds?next_hold+687500350:UINT64_MAX;
    }
    if(next==next_frontend)next_frontend+=frontend_kind&&next_frontend%2?333333:16666667;
    arc_rpc_mem_run(next);
}
static void begin(uint32_t opcode)
{
    while(!arc_rpc_mem_ready(false))step_time(false);
    arc_rpc_mem_instruction_begin();arc_rpc_mem_opcode(opcode);
}
static uint64_t finish(void)
{
    arc_rpc_mem_instruction_end(arc_rpc_mem_fast.fetch_pc+4u);arc_rpc_mem_run(clock_state.now_ns);
    while(arc_rpc_mem_fast.pending)step_time(false);
    ArcRpcMemStats s;arc_rpc_mem_stats(&s);return s.completed_ticks;
}
static uint64_t drain(void)
{
    while(arc_rpc_mem_fast.bus_busy)step_time(true);
    return clock_state.now_ns*(uint64_t)(28u*rate);
}
static void access(uint32_t address,bool write,unsigned attributes,unsigned bytes)
{ arc_rpc_mem_access(address,address,bytes,write,false,attributes,true); }
static uint64_t burst(unsigned words,bool sa)
{ return sa?words*q*5:q*(5+2*(words-1)); }
static void scalar_and_map(void)
{
    const uint32_t regions[]={0x02000000,0x02f00000,0x04000000,0x10000000,0x14000000,0x18000000,0x1c000000,0x13fffffc};
    for(unsigned cpu=0;cpu<4;++cpu)for(unsigned i=0;i<sizeof(regions)/sizeof(*regions);++i) {
        init(cpu,100);begin(0xe5901000);access(regions[i],false,0,4);assert(finish()==b+5*q);
    }
    for(unsigned cpu=0;cpu<4;++cpu) {
        init(cpu,100);begin(0xe890ffff);
        for(unsigned i=0;i<16;++i)access(0x10000000+4*i,false,0,4);
        assert(finish()==b+burst(16,cpu==2));
        /* A bank/1 KiB transition starts N even within a register packet. */
        init(cpu,100);begin(0xe890000f);
        for(unsigned i=0;i<4;++i)access(0x13fffff8+4*i,false,0,4);
        assert(finish()==b+(cpu==2?20:14)*q);
    }
    /* Harness populations never change timing; an absent target is still a
     * DRAM attempt. Functional mirroring/population is checked by R1 memory. */
    for(unsigned mb=4;mb<=64;mb*=2) {
        init(2,100);begin(0xe5901000);arc_rpc_mem_access(0x10000000,0x10000000,4,false,false,0,mb!=4);
        assert(finish()==b+5*q);
    }
}
static void io_phase(void)
{
    for(unsigned cpu=0;cpu<4;++cpu)for(unsigned selection=0;selection<4;++selection) {
        init(cpu,35);iomd.iotcr=selection<<2;begin(0xe5901000);access(0x03010000,false,12,1);
        uint64_t period=1750u*rate;unsigned counts[]={9,7,5,4};
        uint64_t end=((b+period-1)/period+counts[selection])*period;
        assert(finish()==end);
        init(cpu,35);begin(0xe5901000);access(0x03040000+(selection<<19),false,0,4);
        period=3500u*rate;unsigned slow[]={7,6,5,5};
        uint64_t alignment=selection==3?period*4:period;
        end=((b+alignment-1)/alignment)*alignment+slow[selection]*period;
        assert(finish()==end);
    }
    for(unsigned fast=0;fast<2;++fast) {
        init(2,99);iomd.ectcr=fast;begin(0xe5901000);access(0x08000000,false,0,4);
        uint64_t p=1750u*rate;assert(finish()==((b+p-1)/p+(fast?5:9))*p);
    }
}
static void ordinary_rom_io_stores(void)
{
    for(unsigned cpu=0;cpu<4;++cpu)for(unsigned byte=0;byte<2;++byte) {
        init(cpu,100);iomd.romcr0=24|(cpu==3?64:0);
        begin(0xe5801000);access(0x00008000,true,12,byte?1:4);
        /* ROM burst bits cannot shorten any write. ARM7500 has two
         * 16-bit chip accesses and the ROM clock remains divided by two. */
        uint64_t chips=cpu==3&&!byte?2:1,scale=cpu==3?2:1;
        assert(finish()==b+7*q*chips*scale);
        ArcRpcMemStats s;arc_rpc_mem_stats(&s);
        assert(!s.packets&&!s.fills&&!s.buffered_packets);
        init(cpu,100);begin(0xe5801000);access(0x03200094,true,12,byte?1:4);
        assert(finish()==b+4*q);arc_rpc_mem_stats(&s);
        assert(!s.packets&&!s.fills&&!s.buffered_packets);
    }
}
static void width_and_clock(void)
{
    for(unsigned selector=0;selector<2;++selector) {
        init(3,35);begin(0xe89000ff);
        for(unsigned i=0;i<8;++i)access(0x10000000+4*i,false,0,4);
        arc_rpc_mem_instruction_end(arc_rpc_mem_fast.fetch_pc+4u);arc_rpc_mem_run(clock_state.now_ns);
        if(selector)iomd.clkctrl&=~2u;else iomd.dramwid=1;
        while(arc_rpc_mem_fast.pending)step_time(false);
        ArcRpcMemStats s;arc_rpc_mem_stats(&s);assert(s.completed_ticks==b+33*q);
    }
    for(unsigned width=0;width<2;++width)for(unsigned byte=0;byte<2;++byte) {
        init(3,35);iomd.dramwid=width;begin(0xe5901000);access(0x10000000,false,0,byte?1:4);
        assert(finish()==b+(width&&!byte?7:5)*q);
    }
}
static uint64_t fifo_oracle(unsigned cpu,unsigned speed,unsigned count)
{
    uint64_t ends[64],eligible=0,last=0;unsigned head=0,tail=0;
    unsigned capacity=cpu==0?2:cpu==2?8:4;
    for(unsigned i=0;i<count;++i) {
        eligible+=28000;
        while(head<tail&&ends[head]<=eligible)++head;
        if(tail-head==capacity)eligible=ends[head++];
        last=(last>eligible?last:eligible)+4375u*speed;ends[tail++]=last;
    }
    return last;
}
static void buffer_capacity_and_order(void)
{
    for(unsigned cpu=0;cpu<4;++cpu) {
        init(cpu,99);
        for(unsigned i=0;i<32;++i) { begin(0xe5801000);access(0x10000000+512*i,true,4,4);finish(); }
        uint64_t expected=fifo_oracle(cpu,rate,32);
        uint64_t observed=drain();assert(observed>=expected&&observed-expected<28u*rate);
        ArcRpcMemStats s;arc_rpc_mem_stats(&s);assert(s.packets==32&&s.words==32&&s.buffer_wait_ticks>0);
        init(cpu,100);begin(0xe88000ff);for(unsigned i=0;i<8;++i)access(0x10000000+4*i,true,4,4);finish();
        begin(0xe5901000);access(0x10001000,false,0,4);
        assert(finish()==b+(cpu==2?27:24)*q); /* full FIFO before demand */
        /* W=0 stops admission, never discards prior packets. */
        init(cpu,100);begin(0xe5801000);access(0x10000000,true,4,4);finish();arc_rpc_mem_control(5);
        begin(0xe5801000);access(0x10000004,true,4,4);assert(finish()==b+10*q);
    }
}
static void cache_eviction_and_maintenance(void)
{
    for(unsigned halves=1;halves<=2;++halves) {
        init(2,100);begin(0xe5901000);access(0x10000000,false,12,4);finish();
        for(unsigned h=0;h<halves;++h) {begin(0xe5801000);access(0x10000000+16*h,true,12,4);finish();}
        for(unsigned i=1;i<=32;++i) {begin(0xe5901000);access(0x10000000+512*i,false,12,4);finish();}
        ArcRpcMemStats s;arc_rpc_mem_stats(&s);
        assert(s.completed_ticks==(33+halves)*b+(33*22+11*halves)*q);
        assert(s.fills==33&&s.dirty_halves==halves&&s.words==264+4*halves);
    }
    for(unsigned cpu=0;cpu<4;++cpu) {
        init(cpu,100);begin(0xe5901000);access(0x10000000,false,8,4);uint64_t at=finish();
        arc_rpc_mem_control(0);begin(0xe5901000);access(0x10000000,false,0,4);assert(finish()==at+b);
        begin(0xee050f10);arc_rpc_mem_maintenance(cpu==2?8:5,0,0,0);assert(finish()==at+2*b);
        begin(0xe5901000);access(0x10000000,false,0,4);assert(finish()==at+3*b);
        begin(0xee070f10);arc_rpc_mem_maintenance(7,0,cpu==2?6:0,0);assert(finish()==at+4*b);
        begin(0xe5901000);access(0x10000000,false,0,4);assert(finish()==at+5*b+5*q);
    }
    /* Explicit clean enqueues a dirty half with W clear; invalidate alone
     * discards its timing dirty state without introducing writeback. */
    for(unsigned clean=0;clean<2;++clean) {
        init(2,100);begin(0xe5901000);access(0x10000000,false,12,4);finish();
        begin(0xe5801000);access(0x10000000,true,12,4);finish();
        begin(0xee070f30);arc_rpc_mem_maintenance(7,1,clean?10:6,0x10000000);finish();
        begin(0xee070f9a);arc_rpc_mem_maintenance(7,4,10,0);uint64_t end=finish();
        assert(end==(clean?3*b+33*q:4*b+22*q));
    }
}
/* Q-3: isolate each input to the fetch eligibility boundary. Real MMU
 * eviction/re-walk is covered by rpc-fetch-memo-rewalk; this seam additionally
 * prevents one predicate masking a defect in the other. Retained virtual tags
 * still hit with descriptor C clear, including after an eligible remap. */
static void fetch_memo_eligibility(void)
{
    /* ROM/VRAM and DRAM admit tags; I/O, reserved and EASI do not (§2.2).
     * The last three rows exercise the 29-bit physical aperture aliases. */
    const struct {uint32_t address;bool cache_region;} physical[] = {
        {0,true},{0x01000000,true},{0x02000000,true},{0x02fffffc,true},
        {0x03000000,false},{0x03fffffc,false},{0x04000000,false},{0x07fffffc,false},
        {0x08000000,false},{0x0ffffffc,false},{0x10000000,true},{0x14000000,true},
        {0x18000000,true},{0x1ffffffc,true},{0x20000000,true},{0x23000000,false},{0x30000000,true}};
    const uint32_t va = 0x00400100;
    for(unsigned cpu=0;cpu<4;++cpu)for(unsigned backed=0;backed<2;++backed)
    for(unsigned i=0;i<sizeof(physical)/sizeof(*physical);++i) {
        init(cpu,100);begin(0xe1a00000);
        arc_rpc_mem_fetch(va,0x10000100,8,true);finish();
        assert(arc_rpc_mem_fast.memo_valid);
        ArcRpcMemStats before,after;arc_rpc_mem_stats(&before);
        begin(0xe1a00000);arc_rpc_mem_fetch(va,physical[i].address,0,backed!=0);finish();
        arc_rpc_mem_stats(&after);
        bool eligible=backed&&physical[i].cache_region;
        assert(after.hits-before.hits==(unsigned)eligible);
        assert(arc_rpc_mem_fast.fetch_hits==(unsigned)eligible); /* retained memo, even with C=0 */
        assert(after.words-before.words==(unsigned)!eligible && after.fills==before.fills);
    }
    /* All existing memo-clearing paths must continue to force a real lookup.
     * Warm I, allocate D (SA too), c7 invalidate, control/TLB changes and an
     * uncached read while SA switching is disabled. */
    for(unsigned cpu=0;cpu<4;++cpu)for(unsigned operation=0;operation<6;++operation) {
        if(operation==5&&cpu!=2)continue;
        init(cpu,100);begin(0xe1a00000);arc_rpc_mem_fetch(va,0x10000100,8,true);finish();
        assert(arc_rpc_mem_fast.memo_valid);
        if(operation==0) {begin(0xe5901000);access(0x10001000,false,8,4);finish();}
        if(operation==1) {begin(0xee070f10);arc_rpc_mem_maintenance(7,0,cpu==2?5:0,0);finish();}
        if(operation==2) {begin(0xee070f10);arc_rpc_mem_maintenance(7,0,cpu==2?6:0,0);finish();}
        if(operation==3)arc_rpc_mem_control(0);
        if(operation==4)arc_rpc_mem_permission_change();
        if(operation==5) {
            arc_rpc_mem_clock_switch(false);
            begin(0xe1a00000);arc_rpc_mem_fetch(va,0x10000100,8,true);finish();
            assert(arc_rpc_mem_fast.memo_valid);
            begin(0xe5901000);access(0x10001000,false,0,4);finish();
        }
        assert(!arc_rpc_mem_fast.memo_valid);
        ArcRpcMemStats before,after;arc_rpc_mem_stats(&before);
        begin(0xe1a00000);arc_rpc_mem_fetch(va,0x10000100,8,true);finish();arc_rpc_mem_stats(&after);
        bool invalidated=operation==1||(cpu!=2&&operation==2);
        assert(after.hits-before.hits==(unsigned)!invalidated);
        assert(after.words-before.words==(invalidated?(cpu==2?8u:4u):0u));
    }
    puts("Q-3: independent backed/region boundaries and memo invalidation paths passed");
}
static void video_grants(void)
{
    for(unsigned cpu=0;cpu<4;++cpu) {
        init(cpu,100);begin(0xe890ffff);for(unsigned i=0;i<16;++i)access(0x10000000+4*i,false,0,4);
        arc_rpc_mem_instruction_end(arc_rpc_mem_fast.fetch_pc+4u);arc_rpc_mem_run(clock_state.now_ns);
        clock_state.now_ns+=100;arc_rpc_mem_video_hold(350);arc_rpc_mem_run(clock_state.now_ns);
        /* An unrelated event inside the hold must not retire pending service
         * or advance the next base from the beginning of that hold. */
        uint64_t arrival=clock_state.now_ns+700;
        while(arc_rpc_mem_fast.pending) {
            uint64_t next=arc_rpc_mem_next(UINT64_MAX,false);
            if(arrival<next&&arrival>clock_state.now_ns)next=arrival;
            clock_state.now_ns=next;arc_rpc_mem_run(next);
        }
        ArcRpcMemStats s;arc_rpc_mem_stats(&s);
        assert(s.completed_ticks==b+(cpu==2?80:38)*q+350u*28u*rate);
    }
}
static void warm_code_data(void)
{
    begin(0xe1a00000);arc_rpc_mem_fetch(0x10001000,0x10001000,8,true);finish();
    begin(0xe5901000);access(0x10002000,false,12,4);finish();
}
static void held_hot(unsigned cpu,unsigned kind)
{
    init(cpu,100);warm_code_data();
    ArcRpcMemStats before,after;arc_rpc_mem_stats(&before);
    uint64_t start=before.completed_ticks;
    /* One contained D quad and V half-SAM at a declared device edge. The
     * raster tests independently exercise production transfer generation. */
    hold_ns=350+156;next_hold=(start+28u*rate-1)/(28u*rate)+5;
    remaining_holds=1;
    unsigned count=kind==2?1:1024;
    for(unsigned i=0;i<count;++i){
        begin(kind==0?0xe1a00000:kind==1?0xe890000f:0xe880000f);
        arc_rpc_mem_fetch(0x10001000,0x10001000,8,true);
        if(kind)for(unsigned lane=0;lane<4;++lane)access(0x10002000+4*lane,kind==2,12,4);
        finish();
    }
    arc_rpc_mem_stats(&after);
    uint64_t service=kind==2&&cpu!=2?11*q:0;
    /* Older unified hot stores are write-through; B/W enqueue overlaps base.
     * Observe their drained endpoint, never mistake it for CPU retirement. */
    if(service){drain();arc_rpc_mem_stats(&after);service=11*q;}
    uint64_t expected=count*b+hold_ns*28u*rate+service;
    if(service){
        uint64_t end=clock_state.now_ns*28u*rate;
        assert(end>=start+expected&&end-start-expected<28u*rate);
    }else assert(after.completed_ticks-start==expected);
    assert(after.fills==before.fills && remaining_holds==0);
}
static void held_buffer(unsigned cpu,bool idle)
{
    init(cpu,100);unsigned capacity=cpu==0?2:cpu==2?8:4;
    for(unsigned i=0;i<capacity;++i){begin(0xe5801000);access(0x10000000+i*512,true,4,4);finish();}
    ArcRpcMemStats full;arc_rpc_mem_stats(&full);assert(full.buffered_packets==capacity);
    hold_ns=350+156;next_hold=clock_state.now_ns+1;remaining_holds=1;
    if(idle)arc_rpc_mem_idle(true);
    else {
        begin(0xe5801000);access(0x10004000,true,4,4);
        assert(finish()==b+5*q+hold_ns*28u*rate);
    }
    uint64_t end=drain(),expected=b+(capacity+(idle?0:1))*5*q+hold_ns*28u*rate;
    assert(end>=expected&&end-expected<28u*rate && !remaining_holds);
    ArcRpcMemStats done;arc_rpc_mem_stats(&done);
    assert(done.instructions==capacity+(idle?0:1)&&!done.buffered_packets);
    if(idle){arc_rpc_mem_idle(false);begin(0xe1a00000);assert(finish()==end+b);}
}
static void hot_video_boundaries(void)
{
    for(unsigned cpu=0;cpu<4;++cpu){
        for(unsigned kind=0;kind<3;++kind)held_hot(cpu,kind);
        held_buffer(cpu,false);held_buffer(cpu,true);
    }
    puts("RPC hot ALU/LDM/STM, full FIFO and idle: K*b + 350*D + 156*V with independent grants passed");
}
static void long_queue(unsigned speed,unsigned pacing)
{
    const unsigned k=16000000;
    init(2,speed);frontend_kind=pacing;next_frontend=pacing?333333:16666667;
    uint64_t denominator=28u*speed,initial=(2*b+denominator-1)/denominator;
    next_hold=initial+687500000;remaining_holds=10;
    for(unsigned i=0;i<k;++i) {
        begin(0xe3330000);finish();
        begin(0x188000ff);for(unsigned word=0;word<8;++word)access(0x10000000+4*word,true,4,4);finish();
        begin(0x1afffffc);finish();
    }
    uint64_t observed=drain();
    uint64_t expected=(initial+UINT64_C(11000000000)+3500)*denominator;
    assert(observed==expected);
    ArcRpcMemStats s;arc_rpc_mem_stats(&s);
    assert(s.instructions==3u*(uint64_t)k&&s.words==8u*(uint64_t)k&&s.packets==2u*(uint64_t)k);
    assert(s.buffer_wait_ticks>0&&!remaining_holds&&!s.buffered_packets);
    printf("long rate=%u pacing=%u instructions=%llu words=%llu packets=%llu blocked_ticks=%llu elapsed_ns=%llu exact oracle passed\n",speed,pacing,(unsigned long long)s.instructions,(unsigned long long)s.words,(unsigned long long)s.packets,(unsigned long long)s.buffer_wait_ticks,(unsigned long long)(observed/denominator));
}
static void batch_boundaries(void)
{
    uint64_t bus_until = 0;
    for (unsigned tied = 0; tied < 2; ++tied) {
        init(2,100);
        ArcRpcCpuBatch batch = {&clock_state,&bus_until,tied ? 177 : 1000};
        begin(0xe5901000); access(0x10000000,false,0,4);
        arc_rpc_mem_instruction_end(4);
        assert(arc_rpc_mem_batch_next(&batch,false) == !tied);
        if (tied) {
            /* Service may finish before the boundary, but an issue whose
             * rational time rounds to that boundary belongs to the driver. */
            assert(clock_state.now_ns < 177);
            clock_state.now_ns=177;arc_rpc_mem_run(177);
            assert(arc_rpc_mem_ready(false));
        } else assert(clock_state.now_ns==177);
        ArcRpcMemStats s;arc_rpc_mem_stats(&s);
        assert(s.words==1 && s.completed_ticks==b+5*q);
        assert(arc_rpc_mem_fast.issue_ticks==2*b+5*q);
    }
    init(2,100);arc_rpc_mem_measure(0,0,false);
    while(!arc_rpc_mem_ready(false))step_time(false);
    ArcRpcCpuBatch batch={&clock_state,&bus_until,100000};
    arc_rpc_mem_batch_begin(&batch);assert(arc_rpc_mem_fast.integer_batch);
    for (unsigned i=0;i<200;++i) {
        arc_rpc_mem_instruction_begin();arc_rpc_mem_instruction_end(4);
        if(i!=199)assert(arc_rpc_mem_batch_next(&batch,false));
    }
    arc_rpc_mem_batch_flush();
    assert(!arc_rpc_mem_fast.integer_batch && clock_state.now_ns==2000);
    assert(arc_rpc_mem_fast.issue_ticks==200*b && arc_rpc_mem_fast.cpu_due_ticks==201*b);
    assert(arc_rpc_mem_fast.cpu_due_ns==2010 && arc_rpc_mem_fast.instructions==200);
    init(2,100);arc_rpc_mem_measure(0,0,false);
    begin(0xe5901000);access(0x10000000,false,0,4);arc_rpc_mem_instruction_end(4);
    assert(arc_rpc_mem_batch_next(&batch,false));assert(clock_state.now_ns==177);
    arc_rpc_mem_batch_begin(&batch);assert(arc_rpc_mem_fast.integer_batch);
    for (unsigned i=0;i<200;++i) {
        arc_rpc_mem_instruction_begin();arc_rpc_mem_instruction_end(4);
        if(i!=199)assert(arc_rpc_mem_batch_next(&batch,false));
    }
    arc_rpc_mem_batch_flush();
    assert(clock_state.now_ns==2167 && arc_rpc_mem_fast.cpu_due_ns==2177);
    assert(arc_rpc_mem_fast.issue_ticks==201*b+5*q && arc_rpc_mem_fast.cpu_due_ticks==202*b+5*q);
}
static void high_time_arithmetic(void)
{
    /* Independent native division oracle at all admitted rates, near the
     * largest representable rational tick. Include nonzero remainders. */
    for (unsigned speed=1;speed<=100;++speed) for (unsigned offset=0;offset<32;++offset) {
        init(2,speed);arc_rpc_mem_measure(0,0,false);
        uint64_t d=28u*speed, ns=UINT64_MAX/d-4096-offset;
        clock_state.now_ns=ns;arc_rpc_mem_idle(true);arc_rpc_mem_idle(false);
        uint64_t expected=ns*d+b;
        assert(arc_rpc_mem_fast.cpu_due_ns==expected/d+(expected%d!=0));
        clock_state.now_ns=arc_rpc_mem_fast.cpu_due_ns;
        begin(0xe5901000);access(0x10000000,false,0,4);
        assert(finish()==expected+5*q);
        expected+=5*q+b;
        assert(arc_rpc_mem_fast.cpu_due_ticks==expected);
        assert(arc_rpc_mem_fast.cpu_due_ns==expected/d+(expected%d!=0));
    }
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--fetch-memo")){fetch_memo_eligibility();return 0;}
    if(argc==2 && !strcmp(argv[1],"--hot-video")){hot_video_boundaries();return 0;}
    if(argc==3) {long_queue((unsigned)atoi(argv[1]),(unsigned)atoi(argv[2]));return 0;}
    scalar_and_map();io_phase();ordinary_rom_io_stores();width_and_clock();buffer_capacity_and_order();
    cache_eviction_and_maintenance();video_grants();batch_boundaries();high_time_arithmetic();
    puts("RPC timing service: independent bus, phase, FIFO, cache, clock/width and grant equations passed");
    return 0;
}
