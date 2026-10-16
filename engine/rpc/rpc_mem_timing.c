/* ArcDroid timing-only RiscPC memory system, D-139. GPL-2.0-only.
 * Numeric costs and qualified fidelity limits: docs/riscpc-timing.md §§2–5.
 * Functional transfers happen atomically in the interpreter, never here. */
#include "rpc_mem_timing.h"
#include "rpc_profile.h"
#if defined(ARCDROID_RPC_CROSSCHECK)
#include "rpc_crosscheck.h"
#endif
#include "rpcemu.h"
#include "iomd.h"
#include <string.h>

#define COMMANDS 128u
#define WORDS 16u
#define BUFFER 8u
#define DRAIN 0u
#define DEMAND 1u
#define BUFFERED 2u

typedef struct { uint32_t va, physical; uint8_t valid, dirty; } Tag;
typedef struct {
    Tag tags[512];
    uint8_t victim[128];
    unsigned sets, ways, shift, populated;
} Cache;
#if defined(__GNUC__)
#define PACKET_ALIGN __attribute__((aligned(64)))
#else
#define PACKET_ALIGN
#endif
/* Scalar requests keep their metadata and first lane together. Host alignment
 * makes the ring stride a shift and avoids scattering each uncached word over
 * several cache lines; it has no guest bus or packet-capacity significance. */
typedef struct PACKET_ALIGN {
    unsigned kind, count, index;
    uint16_t writes;
    bool force_n, locked, fetch, fill;
    uint8_t width[WORDS];
    uint32_t address[WORDS];
} Packet;
#undef PACKET_ALIGN
static struct {
    ArcRpcScheduler *clock;
    unsigned model, rate;
    uint32_t control, opcode;
    bool switching, fast_clock, idle, restart;
    Cache instruction, data;
    Packet command[COMMANDS], buffer[BUFFER], active;
    unsigned head, count, bhead, bcount, bwords, grant_left;
    unsigned active_kind;
    bool active_valid, cycle_active, video_active;
    uint64_t cursor, bus_cursor, finish, queued_video, video_arrival, wait_start;
    uint64_t reciprocal;
    uint32_t last_rom;
    ArcRpcMemStats stats;
} m;
ArcRpcMemFast arc_rpc_mem_fast;
#if defined(ARCDROID_TEST_FAULTS)
ArcRpcExercise arc_rpc_exercise;
unsigned arc_rpc_exercise_path;
static struct { uint64_t start, integer, services; bool live, integral, batched; } exercise_batch;
void arc_rpc_mem_exercise_begin(bool batched)
{
    exercise_batch.start=arc_rpc_mem_fast.instructions;
    exercise_batch.integer=exercise_batch.services=0;
    exercise_batch.live=arc_rpc_mem_fast.enabled;
    exercise_batch.integral=arc_rpc_mem_fast.integer_batch;
    exercise_batch.batched=batched;
}
void arc_rpc_mem_exercise_end(void)
{
    if(!exercise_batch.live)return;
    uint64_t n=arc_rpc_mem_fast.instructions-exercise_batch.start;
    uint64_t integral=exercise_batch.integer;
    uint64_t rational=n ? n-1 : 0;
    if(integral) rational-=integral-1;
    if(exercise_batch.batched) rational-=exercise_batch.services;
    else rational=0;
    arc_rpc_exercise_add(ARC_RPC_COUNT_rational_unobserved+arc_rpc_exercise_path,rational);
    exercise_batch.live=false;
}
static uint64_t measure_first, measure_last;
static bool measure_drained;
static ArcRpcMemInterval interval;
static ArcRpcExercise interval_exercise_start, interval_exercise_end;
bool arc_rpc_mem_exercise_interval(ArcRpcExercise *out)
{
    if(!out || !interval.done || arc_rpc_mem_fast.test_measure)return false;
    for(unsigned i=0;i<ARC_RPC_COUNT_TOTAL;++i)out->values[i]=interval_exercise_end.values[i]-interval_exercise_start.values[i];
    return true;
}
/* Measurement state is deliberately outside both raw inspector structures. */
static bool endpoint_mode, previous_valid, pending_end;
static uint64_t previous_completion, end_completion;
static void endpoint_complete(uint64_t at);
static void endpoint_drain(uint64_t at);
void arc_rpc_mem_measure(uint64_t first, uint64_t last, bool drained)
{
    memset(&interval_exercise_start,0,sizeof(interval_exercise_start));
    memset(&interval_exercise_end,0,sizeof(interval_exercise_end));
    endpoint_mode = previous_valid = pending_end = false;
    previous_completion = end_completion = 0;
    arc_rpc_mem_fast.test_measure = first != 0;
    arc_rpc_mem_fast.test_stop = false;
    measure_first = first; measure_last = last; measure_drained = drained; memset(&interval, 0, sizeof(interval));
}
void arc_rpc_mem_interval(ArcRpcMemInterval *out) { if (out) *out = interval; }
bool arc_rpc_mem_endpoint(uint64_t first, uint64_t last, bool drained)
{
    if ((!first && last) || (first && (last < first || first <= arc_rpc_mem_fast.instructions))) return false;
    arc_rpc_mem_measure(first, last, drained);
    arc_rpc_mem_fast.test_measure = false;
    endpoint_mode = first != 0;
    previous_valid = first == 1;
    return true;
}
void arc_rpc_mem_measure_state(ArcRpcMeasureState *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->first = measure_first; out->last = measure_last;
    out->drained = measure_drained; out->endpoint = endpoint_mode;
    out->intrusive = arc_rpc_mem_fast.test_measure;out->stopped = arc_rpc_mem_fast.test_stop;
    out->previous_completion = previous_completion; out->previous_valid = previous_valid;
    out->end_completion = end_completion; out->pending_end = pending_end;
    out->interval = interval;
}
bool arc_rpc_mem_measure_equal(const ArcRpcMeasureState *a, const ArcRpcMeasureState *b)
{
    return a && b && a->first == b->first && a->last == b->last &&
        a->drained == b->drained && a->endpoint == b->endpoint && a->intrusive == b->intrusive && a->stopped == b->stopped &&
        a->previous_completion == b->previous_completion && a->previous_valid == b->previous_valid &&
        a->end_completion == b->end_completion && a->pending_end == b->pending_end &&
        a->interval.started == b->interval.started && a->interval.done == b->interval.done &&
        a->interval.start_ticks == b->interval.start_ticks && a->interval.end_ticks == b->interval.end_ticks &&
        a->interval.start_fetch_hits == b->interval.start_fetch_hits && a->interval.end_fetch_hits == b->interval.end_fetch_hits &&
        !memcmp(&a->interval.start, &b->interval.start, sizeof(a->interval.start)) &&
        !memcmp(&a->interval.end, &b->interval.end, sizeof(a->interval.end));
}
unsigned arc_rpc_mem_endpoint_admit(unsigned count)
{
    if (!endpoint_mode) return count;
    uint64_t n = arc_rpc_mem_fast.instructions;
    if (n >= measure_last) return 0;
    if (n == measure_first - 1 && !interval.started) {
        if (!previous_valid) fatal("RPC endpoint missing previous completion");
        arc_rpc_mem_stats(&interval.start);
        interval.start_fetch_hits=arc_rpc_mem_fast.fetch_hits;
        interval.start.completed_ticks = previous_completion;
        interval.start_ticks = previous_completion; interval.started = true;
        interval_exercise_start=arc_rpc_exercise;
    }
    uint64_t bound = measure_last;
    if (n < measure_first - 1) bound = measure_first - 1;
    return bound - n < count ? (unsigned)(bound - n) : count;
}
static void endpoint_drain(uint64_t at)
{
    if (!endpoint_mode || !pending_end || m.count || m.bcount) return;
    interval.end_fetch_hits=arc_rpc_mem_fast.fetch_hits;
    arc_rpc_mem_stats(&interval.end);
    interval.end.completed_ticks = at;
    interval.end_ticks = at; interval.done = true; pending_end = false;
    interval_exercise_end=arc_rpc_exercise;
}
static void endpoint_complete(uint64_t at)
{
    if (!endpoint_mode) return;
    uint64_t n = arc_rpc_mem_fast.instructions;
    if (n == measure_first - 1) { previous_completion = at; previous_valid = true; }
    if (n != measure_last || !interval.started || interval.done) return;
    end_completion = at; pending_end = true;
    if (!measure_drained || (!m.count && !m.bcount)) {
        interval.end_fetch_hits=arc_rpc_mem_fast.fetch_hits;
    arc_rpc_mem_stats(&interval.end); interval.end.completed_ticks = at;
        interval.end_ticks = at; interval.done = true; pending_end = false;
    interval_exercise_end=arc_rpc_exercise;
    }
}
void arc_rpc_mem_endpoint_return(void)
{
    if (!endpoint_mode) return;
    /* The interpreter has already performed its ordinary quota-return flush.
     * Rational issue_ticks and the materialised integer issue retain fractions. */
    if (!arc_rpc_mem_fast.pending) endpoint_complete(arc_rpc_mem_fast.issue_ticks);
    arc_rpc_mem_fast.test_stop = arc_rpc_mem_fast.instructions >= measure_last;
}
#endif
static void complete(uint64_t at)
{
    m.stats.instructions = arc_rpc_mem_fast.instructions;
    m.stats.completed_ticks = at;
#if defined(ARCDROID_TEST_FAULTS)
    if (!endpoint_mode && interval.started && !interval.done && m.stats.instructions == measure_last && (!measure_drained || !m.bcount)) {
        interval.end_fetch_hits=arc_rpc_mem_fast.fetch_hits;
    arc_rpc_mem_stats(&interval.end); interval.end_ticks = at; interval.done = true;
    }
    endpoint_complete(at);
#endif
}

static uint64_t add(uint64_t a, uint64_t b)
{
    if (UINT64_MAX - a < b) fatal("RPC memory time overflow");
    return a + b;
}
static uint64_t ticks(uint64_t ns)
{
    if (ns > UINT64_MAX / arc_rpc_mem_fast.denominator)
        fatal("RPC memory time overflow");
    return ns * arc_rpc_mem_fast.denominator;
}
static uint64_t divide(uint64_t value, unsigned *remainder)
{
    unsigned d = arc_rpc_mem_fast.denominator;
#if defined(__SIZEOF_INT128__)
    /* ceil(2^64/d) estimates floor(value/d) exactly or one too high. With
     * 28 <= d <= 2800, an overestimate leaves a wrapped remainder >= d;
     * correcting it restores the exact quotient and remainder, including
     * values near UINT64_MAX. All arithmetic here is unsigned. */
    uint64_t q = (uint64_t)(((__uint128_t)value * m.reciprocal) >> 64);
    uint64_t r = value - q * d;
    if (r >= d) { --q; r += d; }
    *remainder = (unsigned)r;
    return q;
#else
    *remainder = (unsigned)(value % d);
    return value / d;
#endif
}
static uint64_t visible(uint64_t value)
{
    unsigned remainder;
    uint64_t floor = divide(value, &remainder);
    return floor + (remainder != 0);
}
static void next_base(uint64_t from)
{
    ArcRpcMemFast *f = &arc_rpc_mem_fast;
    unsigned fraction;
    uint64_t floor;
    if (from == f->issue_ticks) { floor = f->issue_floor; fraction = f->issue_fraction; }
    else floor = divide(from, &fraction);
    fraction += f->base_fraction;
    f->due_floor = floor + f->base_whole + (fraction >= f->denominator);
    f->due_fraction = fraction >= f->denominator ? fraction - f->denominator : fraction;
    f->cpu_due_ns = f->due_floor + (f->due_fraction != 0);
    f->cpu_due_ticks = add(from, f->base_ticks);
}
static void memo_set(uint32_t line)
{
    arc_rpc_mem_fast.memo_valid = true; arc_rpc_mem_fast.memo_line = line;
}
static uint64_t quantum(void)
{
    uint64_t q = 875u * (uint64_t)m.rate;
    if (m.model == 3 && !(iomd.clkctrl & 2)) q *= 2;
    return q;
}
static unsigned region(uint32_t address)
{
    address &= 0x1fffffffu;
    if (address < 0x02000000u) return address >> 24; /* ROM banks */
    if (address < 0x03000000u) return 2; /* VRAM */
    if (address < 0x04000000u) return 3; /* I/O */
    if (address < 0x08000000u) return 4; /* reserved */
    if (address < 0x10000000u) return 5; /* EASI */
    return 6 + ((address - 0x10000000u) >> 26); /* four DRAM banks */
}
static bool memory_region(unsigned r) { return r == 2 || r >= 6; }
static bool cache_region(unsigned r) { return r <= 2 || r >= 6; }
static void memo_clear(void) { arc_rpc_mem_fast.memo_valid = false; }
static Cache *cache_for(bool fetch)
{
    return m.model == 2 && !fetch ? &m.data : &m.instruction;
}
#if defined(ARCDROID_TEST_FAULTS)
#define LOOKUP(c,va,maintenance) lookup(c,va,maintenance)
static void lookup_count(bool maintenance,unsigned result,unsigned probes,unsigned way)
{
    unsigned base=maintenance?ARC_RPC_COUNT_lookup_maintenance_empty:ARC_RPC_COUNT_lookup_access_empty;
    arc_rpc_exercise_add(base+result,1);
    arc_rpc_exercise_add(base+3,probes);
    if(result==2){
        arc_rpc_exercise_add(base+4,probes);
        if(way)arc_rpc_exercise_add(base+5,1);
        arc_rpc_exercise_add((maintenance?ARC_RPC_COUNT_MAINTENANCE_WAYS:ARC_RPC_COUNT_ACCESS_WAYS)+way,1);
    }
}
#else
#define LOOKUP(c,va,maintenance) lookup(c,va)
#endif
static Tag *lookup(Cache *c, uint32_t va
#if defined(ARCDROID_TEST_FAULTS)
    , bool maintenance
#endif
)
{
    /* Disabling allocation retains tags. Only a genuinely empty cache can
     * omit its way search, including reset-time uncached instruction fetch. */
    if (!c->populated) {
#if defined(ARCDROID_TEST_FAULTS)
        lookup_count(maintenance,0,0,0);
#endif
        return NULL;
    }
    uint32_t line = va >> c->shift;
    unsigned base = (line & (c->sets - 1u)) * c->ways;
    for (unsigned way = 0; way < c->ways; ++way) {
        Tag *t = &c->tags[base + way];
        if (t->valid && t->va == line) {
#if defined(ARCDROID_TEST_FAULTS)
            lookup_count(maintenance,2,way+1,way);
#endif
            return t;
        }
    }
#if defined(ARCDROID_TEST_FAULTS)
    lookup_count(maintenance,1,c->ways,0);
#endif
    return NULL;
}
static Packet *append(unsigned kind)
{
    arc_rpc_mem_fast.pending = true;
    if (m.count == COMMANDS) fatal("RPC memory request budget exceeded");
    Packet *p = &m.command[(m.head + m.count++) % COMMANDS];
    /* word() initializes exactly the populated lanes. Stale unused lanes
     * never participate in a cycle, so avoid clearing two whole lane arrays
     * for every uncached scalar request. */
    p->kind = kind; p->count = p->index = p->writes = 0;
    p->force_n = p->locked = p->fetch = p->fill = false;
    return p;
}
static void word(Packet *p, uint32_t address, unsigned width, bool write)
{
    if (p->count == WORDS) fatal("RPC memory packet budget exceeded");
    /* ARM word transfers address the aligned external word; unaligned LDR
     * rotation remains functional interpreter work. Bytes retain their lane. */
    p->address[p->count] = address & (width == 4 ? 0x1ffffffcu : 0x1fffffffu);
    if (write) p->writes |= (uint16_t)(1u << p->count);
    p->width[p->count++] = (uint8_t)width;
}
static void castout(Tag *t)
{
    for (unsigned half = 0; half < 2; ++half) if (t->dirty & (1u << half)) {
        Packet *p = append(BUFFERED);
        for (unsigned n = 0; n < 4; ++n) word(p, t->physical + half * 16 + n * 4, 4, true);
        ++m.stats.dirty_halves;
    }
    t->dirty = 0;
}
void arc_rpc_mem_bind(ArcRpcScheduler *clock, unsigned model, unsigned rate)
{
    memset(&m, 0, sizeof(m)); memset(&arc_rpc_mem_fast, 0, sizeof(arc_rpc_mem_fast));
#if defined(ARCDROID_RPC_CROSSCHECK)
    arc_rpc_crosscheck_reset();
#endif
#if defined(ARCDROID_TEST_FAULTS)
#if defined(ARCDROID_RPC_PROFILE)
    arc_rpc_profile_rank_reset();
#endif
    arc_rpc_mem_measure(0, 0, false);
    memset(&arc_rpc_exercise,0,sizeof(arc_rpc_exercise));arc_rpc_exercise_path=0;
    memset(&exercise_batch,0,sizeof(exercise_batch));
#endif
    if (!clock) return;
    if (model > 3 || !rate || rate > 100) fatal("Invalid RPC memory clock");
    m.clock = clock; m.model = model; m.rate = rate;
    arc_rpc_mem_fast.clock = clock;
    arc_rpc_mem_fast.enabled = true;
    arc_rpc_mem_fast.denominator = 28u * rate;
    m.reciprocal = UINT64_MAX / arc_rpc_mem_fast.denominator + 1u;
    m.stats.denominator = arc_rpc_mem_fast.denominator;
    m.instruction.shift = model == 2 ? 5 : 4;
    arc_rpc_mem_fast.memo_shift = m.instruction.shift;
    m.instruction.sets = model == 0 ? 4 : model == 1 ? 128 : model == 2 ? 16 : 64;
    m.instruction.ways = model == 0 ? 64 : model == 2 ? 32 : 4;
    m.data.shift = 5; m.data.sets = 16; m.data.ways = 32;
    arc_rpc_mem_control(0);
    next_base(0);
}
#if defined(ARCDROID_RPC_PROFILE)
#define arc_rpc_mem_control arc_rpc_mem_control_profile_body
#endif
void arc_rpc_mem_control(uint32_t control)
{
    if (!arc_rpc_mem_fast.enabled) return;
    m.control = control; memo_clear();
    uint64_t base = 28000;
    if (m.model == 2 && !m.fast_clock && base < 1750u * (uint64_t)m.rate)
        base = 1750u * (uint64_t)m.rate;
    arc_rpc_mem_fast.base_ticks = base;
    arc_rpc_mem_fast.base_whole = (unsigned)(base / arc_rpc_mem_fast.denominator);
    arc_rpc_mem_fast.base_fraction = (unsigned)(base % arc_rpc_mem_fast.denominator);
    if (!m.clock->now_ns && !arc_rpc_mem_fast.recording && !arc_rpc_mem_fast.instructions)
        next_base(0);
}
#if defined(ARCDROID_RPC_PROFILE)
#undef arc_rpc_mem_control
void arc_rpc_mem_control(uint32_t control)
{
    int previous=arc_rpc_profile_enter(ARC_RPC_COST_TIMING);
    arc_rpc_mem_control_profile_body(control);
    arc_rpc_profile_leave(previous);
}
#endif
#if defined(ARCDROID_RPC_PROFILE)
#define arc_rpc_mem_permission_change arc_rpc_mem_permission_change_profile_body
#endif
void arc_rpc_mem_permission_change(void)
{
    if (arc_rpc_mem_fast.enabled) memo_clear();
}
#if defined(ARCDROID_RPC_PROFILE)
#undef arc_rpc_mem_permission_change
void arc_rpc_mem_permission_change(void)
{
    int previous=arc_rpc_profile_enter(ARC_RPC_COST_TIMING);
    arc_rpc_mem_permission_change_profile_body();
    arc_rpc_profile_leave(previous);
}
#endif
#if defined(ARCDROID_RPC_PROFILE)
#define arc_rpc_mem_clock_switch arc_rpc_mem_clock_switch_profile_body
#endif
void arc_rpc_mem_clock_switch(bool enable)
{
    if (!arc_rpc_mem_fast.enabled || m.model != 2) return;
    m.switching = enable;
    if (enable) m.fast_clock = true;
    arc_rpc_mem_control(m.control);
}
#if defined(ARCDROID_RPC_PROFILE)
#undef arc_rpc_mem_clock_switch
void arc_rpc_mem_clock_switch(bool enable)
{
    int previous=arc_rpc_profile_enter(ARC_RPC_COST_TIMING);
    arc_rpc_mem_clock_switch_profile_body(enable);
    arc_rpc_profile_leave(previous);
}
#endif
void arc_rpc_mem_begin_slow(void)
{
    if (!arc_rpc_mem_fast.enabled) return;
    if (m.count) fatal("RPC instruction issued with pending memory work");
    m.stats.instructions = arc_rpc_mem_fast.instructions - 1;
#if defined(ARCDROID_TEST_FAULTS)
    if (m.stats.instructions + 1 == measure_first) {
        interval.start_fetch_hits=arc_rpc_mem_fast.fetch_hits;
        interval.start = m.stats; interval.start.hits += arc_rpc_mem_fast.fetch_hits;
        interval.start_ticks = m.stats.completed_ticks;
        interval.started = true;
    }
#endif
    ++m.stats.instructions;
}
static bool swap(void) { return (arc_rpc_mem_fast.opcode & 0x0fb00ff0u) == 0x01000090u; }
static bool multiple(void) { return (arc_rpc_mem_fast.opcode & 0x0e000000u) == 0x08000000u; }
static void uncached_clock(void)
{
    if (m.model == 2 && !m.switching) {
        if (m.fast_clock) {
            m.fast_clock = false; arc_rpc_mem_control(m.control);
        } else memo_clear();
    }
}
void arc_rpc_mem_fetch_slow(uint32_t va, uint32_t physical, unsigned attributes, bool backed)
{
    if (!arc_rpc_mem_fast.recording) return;
    bool allocate = m.model == 2 ? (m.control & 0x1000) && (!(m.control & 1) || (attributes & 8)) :
        (m.control & 5) == 5 && (attributes & 8);
    if (m.instruction.populated || allocate) {
        arc_rpc_mem_access(va,physical,4,false,true,attributes,backed);
        return;
    }
    /* Empty tags plus disabled allocation prove an external word fetch.
     * Retained tags after cache disable still use the full lookup above. */
    physical &= 0x1fffffffu;
    if (physical < 0x02000000u && ((physical >> 24 ? iomd.romcr1 : iomd.romcr0) & 24)) {
        arc_rpc_mem_fast.track_rom = true; arc_rpc_mem_fast.after_branch = false;
        arc_rpc_mem_fast.fetch_pc = va;
    }
    Packet *p = append(DEMAND);p->fetch = true;
    word(p,physical,4,false);uncached_clock();
}

#if defined(ARCDROID_RPC_PROFILE)
#define arc_rpc_mem_access arc_rpc_mem_access_profile_body
#endif
void arc_rpc_mem_access(uint32_t va, uint32_t physical, unsigned bytes,
                        bool write, bool fetch, unsigned attributes, bool backed)
{
    if (!arc_rpc_mem_fast.recording) return; /* no descriptor walks, DMA or component charge */
    physical &= 0x1fffffffu;
    if (!fetch && m.model == 2 && !m.data.populated && !(m.control & 12) && !swap()) {
        /* Empty SA data tags, C/W clear: no hit, allocation, buffering or
         * external burst is possible. SWP retains its locked-pair path. */
        Packet *p = append(DEMAND);p->force_n = true;
        word(p,physical,bytes,write);
        if (!write) uncached_clock();
        return;
    }
    unsigned r = region(physical);
    if (fetch && r < 2 && ((r ? iomd.romcr1 : iomd.romcr0) & 24)) {
        /* Only burst ROM can benefit from fetch-history tracking. Cache-hot
         * RAM and reset nonburst ROM avoid per-instruction PC bookkeeping. */
        arc_rpc_mem_fast.track_rom=true; arc_rpc_mem_fast.after_branch=false;
        arc_rpc_mem_fast.fetch_pc=va;
    }
    bool swp = !fetch && swap();
    Cache *c = cache_for(fetch);
    bool mapped_swap = swp && m.model == 2 && (attributes & 12) == 12 &&
        cache_region(r) && backed;
    bool eligible = backed && cache_region(r) && (!swp || mapped_swap);
    Tag *t = NULL;
    uint32_t line = va >> c->shift;
    if (eligible) t = LOOKUP(c, va, false);
    bool cached_swap = mapped_swap && (t || (m.control & 5) == 5);
    bool locked = swp && !cached_swap;
    if (locked) { eligible = false; t = NULL; }
    if (t) {
        ++m.stats.hits;
        if (fetch) {
            memo_set(line);
            return;
        }
        if (!write) return;
        if (m.model == 2) { t->dirty |= 1u << ((va >> 4) & 1); return; }
    }
    bool allocate = eligible && !t && (!write || cached_swap);
    if (fetch && m.model == 2)
        allocate = allocate && (m.control & 0x1000) && (!(m.control & 1) || (attributes & 8));
    else
        allocate = allocate && (m.control & 5) == 5 && (attributes & 8);
    if (allocate) {
        unsigned set = line & (c->sets - 1u), way = c->victim[set];
        t = &c->tags[set * c->ways + way];
        if (m.model == 2 && !fetch && t->valid) castout(t);
        if (!t->valid) ++c->populated;
        memo_clear();
        *t = (Tag){line, physical & ~((1u << c->shift) - 1u), 1, 0};
        c->victim[set] = (uint8_t)((way + 1) % c->ways);
        Packet *p = append(DEMAND); p->fill = true; p->fetch = fetch;
        for (unsigned n = 0; n < (1u << c->shift) / 4; ++n) word(p, t->physical + n * 4, 4, false);
        ++m.stats.fills; ARC_RPC_PROFILE(fill());
#if defined(ARCDROID_TEST_FAULTS)
        unsigned fill_index=fetch?ARC_RPC_COUNT_fill_fetch_memory:ARC_RPC_COUNT_fill_data_memory;
        arc_rpc_exercise_add(fill_index+(memory_region(r)?0:1),1);
#endif
        if (write) t->dirty |= 1u << ((va >> 4) & 1);
        if (fetch) { memo_set(line); }
    } else if (fetch) {
        Packet *p = append(DEMAND); p->fetch = true;
        word(p, physical, bytes, false);
    } else {
        bool buffered = write && !locked && memory_region(r) &&
            (m.control & 9) == 9 && (attributes & 4);
        unsigned kind = buffered ? BUFFERED : DEMAND;
        bool burst = multiple() && bytes == 4 && memory_region(r) &&
            (m.model != 2 || buffered);
        Packet *p = m.count ? &m.command[(m.head + m.count - 1) % COMMANDS] : NULL;
        unsigned cap = buffered ? (m.model == 2 ? 4 : 8) : 16;
        bool join = p && p->kind == kind && !p->fill && p->count < cap &&
            !p->fetch && p->locked == locked;
        if (locked) join = join && p->count == 1 && p->address[0] == physical;
        else join = join && burst && p->width[p->count - 1] == 4 &&
            p->address[p->count - 1] + 4 == physical && region(p->address[0]) == r &&
            (!buffered || m.model != 2 || (p->address[0] >> 4) == (physical >> 4));
        if (!join) { p = append(kind); p->locked = locked; p->fetch = fetch; p->force_n = !burst && !fetch; }
        word(p, physical, bytes, write);
    }
    if (!write) uncached_clock();
}
#if defined(ARCDROID_RPC_PROFILE)
#undef arc_rpc_mem_access

void
arc_rpc_mem_access(uint32_t va, uint32_t physical, unsigned bytes, bool write, bool fetch, unsigned attributes, bool backed)
{

    int previous=arc_rpc_profile_enter(ARC_RPC_COST_TIMING);
    arc_rpc_mem_access_profile_body(va,physical,bytes,write,fetch,attributes,backed);
    arc_rpc_profile_leave(previous);
}
#endif
static void invalidate(Cache *c)
{
    for (unsigned i = 0; i < c->sets * c->ways; ++i) c->tags[i].valid = c->tags[i].dirty = 0;
    c->populated = 0;
    memo_clear();
}
#if defined(ARCDROID_RPC_PROFILE)
#define arc_rpc_mem_maintenance arc_rpc_mem_maintenance_profile_body
#endif
void arc_rpc_mem_maintenance(unsigned crn, unsigned opc2, unsigned crm, uint32_t va)
{
    if (!arc_rpc_mem_fast.recording || crn != 7) return;
    if (m.model != 2) { invalidate(&m.instruction); return; }
    if (opc2 == 0) {
        if (crm == 5 || crm == 7) invalidate(&m.instruction);
        if (crm == 6 || crm == 7) invalidate(&m.data);
    } else if (opc2 == 1 && (crm == 6 || crm == 10)) {
        Tag *t = LOOKUP(&m.data, va, true);
        if (t) {
            if (crm == 10) castout(t);
            else { t->valid = t->dirty = 0; --m.data.populated; memo_clear(); }
        }
    } else if (opc2 == 4 && crm == 10) append(DRAIN);
}
#if defined(ARCDROID_RPC_PROFILE)
#undef arc_rpc_mem_maintenance
void arc_rpc_mem_maintenance(unsigned crn, unsigned opc2, unsigned crm, uint32_t va)
{
    int previous=arc_rpc_profile_enter(ARC_RPC_COST_TIMING);
    arc_rpc_mem_maintenance_profile_body(crn,opc2,crm,va);
    arc_rpc_profile_leave(previous);
}
#endif
void arc_rpc_mem_end_slow(void)
{
    if (!arc_rpc_mem_fast.recording) return;
    if (arc_rpc_mem_fast.integer_batch) arc_rpc_mem_batch_flush();
    arc_rpc_mem_fast.recording = false;
    m.cursor = arc_rpc_mem_fast.issue_ticks;
#if defined(ARCDROID_TEST_FAULTS)
    if (measure_last) arc_rpc_mem_fast.test_stop = arc_rpc_mem_fast.instructions >= measure_last;
#endif
    /* append() already marks every queued command pending. */
    if (!m.count) {
        if (arc_rpc_mem_fast.track_rom && arc_rpc_mem_fast.after_branch) arc_rpc_mem_fast.rom_sequential=false;
        complete(arc_rpc_mem_fast.issue_ticks);
        next_base(arc_rpc_mem_fast.issue_ticks);
    }
}

void arc_rpc_mem_batch_flush(void)
{
    ArcRpcMemFast *f = &arc_rpc_mem_fast;
    if (!f->integer_batch) return;
#if defined(ARCDROID_TEST_FAULTS)
    if(exercise_batch.live && exercise_batch.integral){
        exercise_batch.integer=f->instructions-exercise_batch.start;
        arc_rpc_exercise_add(ARC_RPC_COUNT_integer_unobserved+arc_rpc_exercise_path,exercise_batch.integer);
        exercise_batch.integral=false;
    }
#endif
    f->integer_batch = false;
    f->issue_floor = f->clock->now_ns - (f->issue_fraction != 0);
    f->issue_ticks = add(ticks(f->issue_floor), f->issue_fraction);
    next_base(f->issue_ticks);
}

/* Clock-edge alignment is rational and anchored at session time zero. */
static uint64_t align(uint64_t at, uint64_t period)
{
    uint64_t remainder = at % period;
    return remainder ? add(at, period - remainder) : at;
}
static uint64_t cycle(Packet *p, uint64_t start)
{
    unsigned i = p->index, r = region(p->address[i]);
    uint64_t q = quantum();
    if (!i && (memory_region(r) || r == 4)) {
        unsigned n = m.model == 3 && r >= 6 && p->width[i] != 1 &&
            (iomd.dramwid & (1u << (r - 6))) ? 7 : 5;
        arc_rpc_mem_fast.rom_sequential = false;
        return add(start, n * q);
    }
    if (r < 2 && m.model != 3 && p->width[i] <= 4) {
        unsigned cr = r ? iomd.romcr1 : iomd.romcr0;
        if (!(cr & 24)) {
            unsigned speed = cr & 7, initial = 7 - (speed > 5 ? 5 : speed);
            m.last_rom = p->address[i]; arc_rpc_mem_fast.rom_sequential = false;
            return add(start, initial * q);
        }
    }
    bool sequential = i && !m.restart && !p->force_n &&
        p->width[i] == 4 && p->width[i-1] == 4 &&
        p->address[i] == p->address[i-1] + 4 &&
        region(p->address[i-1]) == r && (p->address[i] & 1023u) != 0;
    if (m.model == 2 && (p->address[i] & 15u) == 0) sequential = false;
    if (r < 2) {
        unsigned cr = r ? iomd.romcr1 : iomd.romcr0;
        unsigned speed = cr & 7, burst = (cr >> 3) & 3;
        unsigned initial = 7 - (speed > 5 ? 5 : speed);
        unsigned chip_bytes = m.model == 3 && (cr & 64) ? 2 : 4;
        unsigned accesses = p->width[i] > chip_bytes ? p->width[i] / chip_bytes : 1;
        bool writing = (p->writes & (1u << i)) != 0;
        bool seq = !writing && !m.restart && (sequential || (p->fetch && !p->fill &&
            arc_rpc_mem_fast.rom_sequential && p->address[i] == m.last_rom + 4));
        uint64_t cost = 0;
        for (unsigned n = 0; n < accesses; ++n) {
            uint32_t chip = p->address[i] / chip_bytes + n;
            /* G7: ROM castouts use initial access time per chip, no write burst. */
            unsigned amount = !writing && burst && (seq || n) && (chip & 3) ? 5 - burst : initial;
            cost += amount * q;
        }
        if (m.model == 3 && !(cr & 32)) cost *= 2;
        m.last_rom = p->address[i]; arc_rpc_mem_fast.rom_sequential = p->fetch && burst;
        return add(start, cost);
    }
    arc_rpc_mem_fast.rom_sequential = false;
    if (memory_region(r) || r == 4) {
        unsigned n = sequential ? 2 : 5;
        if (m.model == 3 && r >= 6 && (iomd.dramwid & (1u << (r - 6))))
            n = p->width[i] == 1 ? 5 : sequential ? 4 : 7;
        return add(start, n * q);
    }
    uint32_t a = p->address[i];
    if (r == 3 && (((a & 0xc00000u) == 0 && ((a >> 16) & 7) == 0) ||
        (a & 0xc00000u) == 0x400000u)) return add(start, 4 * q);
    unsigned divisor = m.model == 3 && !(iomd.clkctrl & 1) ? 2 : 1;
    uint64_t period = 1750u * (uint64_t)m.rate * divisor; /* PC 62.5 ns */
    unsigned count = 9;
    if (r == 3 && (((a >> 16) & 7) == 4 || ((a >> 16) & 7) == 7)) {
        unsigned sel = (a >> 19) & 3;
        static const unsigned simple[4] = {7,6,5,5};
        period *= 2; count = simple[sel];
        if (sel == 3) start = align(start, period * 4);
    } else {
        unsigned sel = 0;
        if (r == 5) sel = iomd.ectcr & (1u << ((a >> 24) & 7)) ? 2 : 0;
        else if (a >= 0x03010000u && a <= 0x0302a000u) sel = (iomd.iotcr >> 2) & 3;
        else if ((a & 0xc00000u) == 0x800000u) sel = iomd.iotcr & 3;
        else if (m.model != 3 && (a & 0xc00000u) == 0xc00000u) sel = (iomd.iotcr >> 4) & 3;
        static const unsigned pc[4] = {9,7,5,4}; count = pc[sel];
    }
    return add(align(start, period), count * period);
}
static unsigned grant(Packet *p)
{
    if (p->locked) return p->count - p->index;
    unsigned a = p->address[p->index], r = region(a), cap = 1;
    if (memory_region(r)) cap = m.model == 0 || m.model == 1 ? 8 : 4;
    else if (r < 2) {
        unsigned cr = r ? iomd.romcr1 : iomd.romcr0;
        if ((cr & 24) && !(p->writes & (1u << p->index))) {
            unsigned bytes = m.model == 3 && (cr & 64) ? 2 : 4;
            unsigned chips = p->width[p->index] > bytes ? p->width[p->index] / bytes : 1;
            unsigned edge = (4 - ((a / bytes) & 3)) / chips;
            cap = 4 / chips;
            if (cap > edge) cap = edge;
        }
    }
    unsigned n = p->count - p->index;
    if (cap > n) cap = n;
    if (m.model == 2 && memory_region(r)) {
        unsigned edge = 4 - ((a >> 2) & 3);
        if (cap > edge) cap = edge;
    }
    return cap;
}
static void start_cycle(void)
{
    if (!m.grant_left) m.grant_left = grant(&m.active);
    if (m.cursor < m.bus_cursor) m.cursor = m.bus_cursor;
    m.finish = cycle(&m.active, m.cursor); m.restart = false; m.cycle_active = true;
    ++m.stats.words;
}
static bool capacity(const Packet *p)
{
    if (m.bcount == BUFFER) return false;
    if (m.model == 2) return true;
    unsigned addresses = m.model == 0 ? 2 : 4;
    return m.bcount < addresses && m.bwords + p->count <= 8;
}
static void commands(void)
{
    while (m.count && !m.video_active) {
        Packet *p = &m.command[m.head];
        if (p->kind == DEMAND) break;
        if (p->kind == DRAIN) { if (m.bcount || m.active_valid) break; }
        else {
            if (!capacity(p)) {
                if (!m.wait_start) m.wait_start = m.cursor;
                break;
            }
            if (m.wait_start) { m.stats.buffer_wait_ticks += m.cursor - m.wait_start; m.wait_start = 0; }
            m.buffer[(m.bhead + m.bcount++) % BUFFER] = *p;
            m.bwords += p->count; ++m.stats.packets;
        }
        m.head = (m.head + 1) % COMMANDS; --m.count;
    }
    if (!m.count && !m.video_active && arc_rpc_mem_fast.pending) {
        arc_rpc_mem_fast.pending = false;
        if (arc_rpc_mem_fast.track_rom && arc_rpc_mem_fast.after_branch) arc_rpc_mem_fast.rom_sequential=false;
        complete(m.cursor);
        next_base(m.cursor);
    }
}
void arc_rpc_mem_video_hold(uint64_t ns)
{
    if (!arc_rpc_mem_fast.enabled) return;
    if (!m.queued_video) m.video_arrival = ticks(m.clock->now_ns);
    m.queued_video = add(m.queued_video, ticks(ns));
    arc_rpc_mem_fast.bus_busy = true;
}
#if defined(ARCDROID_RPC_PROFILE)
#define arc_rpc_mem_run arc_rpc_mem_run_profile_body
#endif
void arc_rpc_mem_run(uint64_t now_ns)
{
    if (!arc_rpc_mem_fast.enabled) return;
    (void)ticks(now_ns);
    commands(); /* enqueue can overlap an already active write packet */
    for (;;) {
        if (m.video_active) {
            if (visible(m.finish) > now_ns) break;
            m.cursor = m.bus_cursor = m.finish; m.video_active = false; m.restart = true;
        }
        if (m.cycle_active) {
            if (visible(m.finish) > now_ns) break;
            m.cursor = m.bus_cursor = m.finish; m.cycle_active = false; ++m.active.index; --m.grant_left;
            if (m.active.index == m.active.count) {
                if (m.active_kind == BUFFERED) {
                    m.bwords -= m.active.count; m.bhead = (m.bhead + 1) % BUFFER; --m.bcount;
                } else {
                    m.head = (m.head + 1) % COMMANDS; --m.count;
                }
                m.active_valid = false;
#if defined(ARCDROID_TEST_FAULTS)
                if (!endpoint_mode && measure_drained && interval.started && !interval.done &&
                    m.stats.instructions == measure_last && !m.count && !m.bcount)
                    complete(m.cursor);
                endpoint_drain(m.cursor);
#endif
            } else if (m.grant_left || !m.queued_video) { start_cycle(); continue; }
        }
        if (m.queued_video && !m.grant_left) {
            if (m.cursor < m.video_arrival) m.cursor = m.video_arrival;
            if (m.cursor < m.bus_cursor) m.cursor = m.bus_cursor;
            m.video_active = true; m.finish = add(m.cursor, m.queued_video);
            if (!m.idle && !arc_rpc_mem_fast.pending) {
                uint64_t ns = m.queued_video / arc_rpc_mem_fast.denominator;
                arc_rpc_mem_fast.cpu_due_ticks = add(arc_rpc_mem_fast.cpu_due_ticks, m.queued_video);
                arc_rpc_mem_fast.cpu_due_ns += ns; arc_rpc_mem_fast.due_floor += ns;
            }
            m.queued_video = 0; arc_rpc_mem_fast.rom_sequential = false;
            break;
        }
        if (m.active_valid) { start_cycle(); continue; }
        commands();
        if (m.bcount) { m.active = m.buffer[m.bhead]; m.active_kind = BUFFERED; }
        else if (m.count && m.command[m.head].kind == DEMAND) {
            m.active = m.command[m.head]; m.active_kind = DEMAND;
        } else break;
        m.active_valid = true; m.grant_left = 0; start_cycle();
    }
    arc_rpc_mem_fast.bus_busy = m.active_valid || m.video_active || m.queued_video || m.bcount;
}
#if defined(ARCDROID_RPC_PROFILE)
#undef arc_rpc_mem_run

void
arc_rpc_mem_run(uint64_t now_ns)
{

    int previous=arc_rpc_profile_enter(ARC_RPC_COST_BUS);
    arc_rpc_mem_run_profile_body(now_ns);
    arc_rpc_profile_leave(previous);
}
#endif
uint64_t arc_rpc_mem_next(uint64_t limit_ns, bool idle)
{
#if defined(ARCDROID_TEST_FAULTS)
    if (measure_last && arc_rpc_mem_fast.instructions >= measure_last) idle = true;
#endif
    if (m.cycle_active || m.video_active) {
        uint64_t finish = visible(m.finish);
        if (finish < limit_ns) limit_ns = finish;
    }
    if (!idle && !arc_rpc_mem_fast.pending && !m.video_active) {
        uint64_t due = arc_rpc_mem_fast.cpu_due_ns;
        if (due < limit_ns) limit_ns = due;
    }
    return limit_ns;
}
bool arc_rpc_mem_ready(bool idle)
{
#if defined(ARCDROID_TEST_FAULTS)
    if (measure_last && arc_rpc_mem_fast.instructions >= measure_last) return false;
#endif
    if (idle || arc_rpc_mem_fast.pending || m.video_active ||
        arc_rpc_mem_fast.cpu_due_ns > m.clock->now_ns) return false;
    arc_rpc_mem_fast.issue_ticks = arc_rpc_mem_fast.cpu_due_ticks;
    arc_rpc_mem_fast.issue_floor = arc_rpc_mem_fast.due_floor;
    arc_rpc_mem_fast.issue_fraction = arc_rpc_mem_fast.due_fraction;
    return true;
}
#if defined(ARCDROID_RPC_PROFILE)
#define arc_rpc_mem_batch_service arc_rpc_mem_batch_service_profile_body
#endif
bool arc_rpc_mem_batch_service(ArcRpcCpuBatch *batch)
{
    /* Execute the same service transitions locally while no device or
     * frontend boundary can intervene. Equality still returns to the driver,
     * which delivers every tied event before release, grant or CPU issue. */
    uint64_t boundary = batch->clock->next_ns < batch->limit_ns ?
        batch->clock->next_ns : batch->limit_ns;
#if defined(ARCDROID_TEST_FAULTS)
    bool valid=m.cycle_active || m.video_active;
    arc_rpc_exercise_add(valid?ARC_RPC_COUNT_service_finish_valid:ARC_RPC_COUNT_service_finish_invalid,1);
    uint64_t due=arc_rpc_mem_fast.cpu_due_ns, now=batch->clock->now_ns;
    unsigned kind;
    if(arc_rpc_mem_fast.pending)kind=ARC_RPC_COUNT_service_pending;
    else if(m.video_active)kind=ARC_RPC_COUNT_service_video;
    else if(valid && due>=visible(m.finish))kind=ARC_RPC_COUNT_service_due_finish;
    else if(m.cycle_active && now<due && due<visible(m.finish) && due<boundary)kind=ARC_RPC_COUNT_service_s1;
    else if(arc_rpc_mem_fast.bus_busy)kind=ARC_RPC_COUNT_service_busy;
    else kind=ARC_RPC_COUNT_service_other;
    arc_rpc_exercise_add(kind,1);
#endif
    if (batch->clock->now_ns >= boundary) return false;
    if (m.count && !m.bcount && !m.active_valid && !m.video_active && !m.queued_video &&
        m.cursor <= UINT64_MAX / 2 && m.bus_cursor <= UINT64_MAX / 2) {
        /* A demand-only run with no intervening deadline needs no resumable
         * grant state. Use the ordinary word-cycle calculation, then commit
         * only if service AND the next issue precede the boundary. Roll back
         * ROM history if a deadline requires the resumable path instead.
         * Keep the resumable path near overflow: bounded commands cannot
         * exhaust the upper half, so prediction never faults prematurely. */
        ARC_RPC_COUNT(shortcut_attempts,1);
        unsigned words = 0, n = 0;
        uint64_t at = m.cursor < m.bus_cursor ? m.bus_cursor : m.cursor;
        bool restart = m.restart, sequential = arc_rpc_mem_fast.rom_sequential;
        uint32_t last = m.last_rom;
        for (; n < m.count; ++n) {
            Packet *p = &m.command[(m.head + n) % COMMANDS];
            if (p->kind != DEMAND) break;
            for (p->index = 0; p->index < p->count; ++p->index) {
                at = cycle(p, at); m.restart = false; ++words;
            }
            p->index = 0;
        }
        if (n == m.count) {
            ArcRpcMemFast *f = &arc_rpc_mem_fast;
            uint64_t due = add(at, f->base_ticks);
            unsigned fraction;
            uint64_t floor = divide(due, &fraction);
            uint64_t ns = floor + (fraction != 0);
            if (ns < boundary) {
                ARC_RPC_COUNT(shortcut_commits,1);
#if defined(ARCDROID_TEST_FAULTS)
                for(unsigned k=0;k<m.count;++k){
                    Packet *packet=&m.command[(m.head+k)%COMMANDS];
                    if(packet->fill && memory_region(region(packet->address[0])))ARC_RPC_COUNT(shortcut_memory_fills,1);
                }
#endif
                m.cursor = m.bus_cursor = m.finish = at;
                m.stats.words += words; m.head = (m.head + m.count) % COMMANDS; m.count = 0;
                m.grant_left = 0; f->pending = false;
                if (f->track_rom && f->after_branch) f->rom_sequential = false;
                complete(at);
                f->cpu_due_ticks = due; f->due_floor = floor; f->due_fraction = fraction;
                batch->clock->now_ns = f->cpu_due_ns = ns;
                bool ready=arc_rpc_mem_ready(false);
#if defined(ARCDROID_TEST_FAULTS)
                if(ready)++exercise_batch.services;
#endif
                return ready;
            }
        }
        ARC_RPC_COUNT(shortcut_rollbacks,1);
        m.restart = restart; m.last_rom = last; arc_rpc_mem_fast.rom_sequential = sequential;
    }
    arc_rpc_mem_run(batch->clock->now_ns);
    for (;;) {
        if (arc_rpc_mem_ready(false)) {
#if defined(ARCDROID_TEST_FAULTS)
            ++exercise_batch.services;
#endif
            return true;
        }
        uint64_t next = arc_rpc_mem_next(boundary, false);
        if (next >= boundary) return false;
        batch->clock->now_ns = next;
        arc_rpc_mem_run(next);
    }
}
#if defined(ARCDROID_RPC_PROFILE)
#undef arc_rpc_mem_batch_service

bool
arc_rpc_mem_batch_service(ArcRpcCpuBatch *batch)
{

    int previous=arc_rpc_profile_enter(ARC_RPC_COST_BUS);
    bool result = arc_rpc_mem_batch_service_profile_body(batch);
    arc_rpc_profile_leave(previous);
    return result;
}
#endif
void arc_rpc_mem_idle(bool idle)
{
    if (!arc_rpc_mem_fast.enabled || idle == m.idle) return;
    m.idle = idle;
    if (!idle) next_base(ticks(m.clock->now_ns));
}
void arc_rpc_mem_stats(ArcRpcMemStats *out)
{
    if (!out) return;
    *out = m.stats; out->instructions = arc_rpc_mem_fast.instructions; out->hits += arc_rpc_mem_fast.fetch_hits; out->buffered_words = m.bwords; out->buffered_packets = m.bcount;
}

#if defined(ARCDROID_TEST_FAULTS)
/* Snapshot private timing state without flushing or servicing any request. */
size_t arc_rpc_mem_observation_snapshot(void *out,size_t capacity)
{
    size_t n=sizeof(m)+sizeof(arc_rpc_mem_fast);
    if(!out || capacity<n)return n;
    memcpy(out,&m,sizeof(m));memcpy((uint8_t *)out+sizeof(m),&arc_rpc_mem_fast,sizeof(arc_rpc_mem_fast));return n;
}
#endif
