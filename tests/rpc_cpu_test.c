/* ArcDroid authored CPU/MMU/memory component checks. GPL-2.0-only.
 * These device seams deliberately fail if the component executes MMIO. They
 * are not substitutes for the contained backend or guest device qualification. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include "rpc_namespace.h"
#include "rpc_memory.h"
#include "rpcemu.h"
#include "arm.h"
#include "cp15.h"
#include "mem.h"
#include "ide.h"
#include "podules.h"
#include "rpc_authored_rom.h"

#undef NDEBUG
#include <assert.h>

extern int arc_rpc_arm_exec_count(unsigned count);
Machine machine;
Config config;
struct iomd iomd;
static uint8_t dirty[2048];
uint8_t *dirtybuffer = dirty;
static jmp_buf boundary;
static unsigned fail_at, allocations;
static int protected;

void *arc_rpc_alloc(size_t size)
{
    return ++allocations == fail_at ? NULL : malloc(size);
}
void fatal(const char *format, ...)
{
    (void)format;
    assert(protected);
    longjmp(boundary, 1);
}
void rpclog(const char *format, ...) { (void)format; }
void resetcodeblocks(void) { pccache = 0xffffffff; }
void cacheclearpage(uint32_t a) { (void)a; }
uint32_t iomd_read(uint32_t a) { (void)a; assert(0); return 0; }
void iomd_write(uint32_t a, uint32_t v) { (void)a; (void)v; assert(0); }
uint32_t iomd_mouse_buttons_read(void) { assert(0); return 0; }
uint8_t superio_read(uint32_t a) { (void)a; assert(0); return 0; }
void superio_write(uint32_t a, uint32_t v) { (void)a; (void)v; assert(0); }
void writevidc20(uint32_t v) { (void)v; assert(0); }
uint8_t fdc_dma_read(uint32_t a) { (void)a; assert(0); return 0; }
void fdc_dma_write(uint32_t a, uint8_t v) { (void)a; (void)v; assert(0); }
uint8_t readide(uint16_t a) { (void)a; assert(0); return 0; }
uint16_t readidew(void) { assert(0); return 0; }
void writeide(uint16_t a, uint8_t v) { (void)a; (void)v; assert(0); }
void writeidew(uint16_t v) { (void)v; assert(0); }
#define PODULE_READ(bits) uint##bits##_t podules_read##bits(int n, PoduleIoType t, uint32_t a) \
    { (void)n; (void)t; (void)a; assert(0); return 0; }
#define PODULE_WRITE(bits) void podules_write##bits(int n, PoduleIoType t, uint32_t a, uint##bits##_t v) \
    { (void)n; (void)t; (void)a; (void)v; assert(0); }
PODULE_READ(8) PODULE_READ(16) PODULE_READ(32)
PODULE_WRITE(8) PODULE_WRITE(16) PODULE_WRITE(32)

static uint32_t cp_read(unsigned reg) { return cp15_read(reg << 16); }
static void cp_write(unsigned reg, uint32_t val) { cp15_write(reg << 16, val); }
static void flush(CPUModel model) { cp_write(model == CPUModel_SA110 ? 8 : 5, 0); }
static void cpu_reset(CPUModel model)
{
    cp15_reset(model); arm_reset(model);
}
static const char *model_name(CPUModel model)
{
    const char *names[] = {"arm610", "arm710", "sa110", "arm7500"};
    return names[model];
}
static void mode32(uint32_t pc, int user)
{
    updatemode(user ? 0x10 : 0x13);
    arm.reg[16] = user ? 0x10 : 0x13;
    arm.reg[15] = pc + 8;
    arm.event = 0;
}
static void enable(CPUModel model, unsigned extra)
{
    cp_write(2, 0x10004000); cp_write(3, 1);
    cp_write(1, 0x31 | extra | (model == CPUModel_ARM610 ? 0 : 0x40));
}
static void fault(unsigned code, uint32_t addr)
{
    assert((arm.event & 0x40) && cp_read(5) == code && cp_read(6) == addr);
    arm.event &= ~0x40u;
}

static void memory_tests(void)
{
    for (unsigned mb = 2; mb <= 8; mb *= 2) {
        size_t size = mb * 1024u * 1024u;
        uint8_t *bytes = malloc(size); assert(bytes);
        for (size_t i = 0; i < size; ++i) bytes[i] = (uint8_t)(i ^ (i >> 11) ^ (i >> 19));
        assert(arc_rpc_memory_load_rom(bytes, size));
        /* Every byte, including the final byte of each mirror, is defined. */
        for (size_t i = 0; i < ROMSIZE; ++i) assert(romb[i] == bytes[i % size]);
        free(bytes);
    }
    assert(!arc_rpc_memory_load_rom(romb, 6u * 1024u * 1024u));
    assert(!arc_rpc_memory_load_rom(NULL, ROMSIZE));
    mem_write32(0x10000000, 0x12345678); assert(mem_read32(0x12000000) == 0x12345678);
    mem_write32(0x14000000, 0x87654321); assert(mem_read32(0x16000000) == 0x87654321);
    /* A7000 DRAM dirty indexing must remain in the 4 MiB tracking window. */
    iomd.vidstart = 0x11c00000; mem_write32(0x11ffffff, 0);
    mem_reset(64, config.vram_size);
    assert(!mem_read32(0x10000000) && !mem_read32(0x14000000));
    if (!config.vram_size) assert(mem_read32(0x02000000) == 0xffffffff);
}

static void cpu_tests(CPUModel model)
{
    memset(rom, 0, ROMSIZE); memcpy(rom, rpc_rom_ram, sizeof(rpc_rom_ram));
    cpu_reset(model); arc_rpc_arm_exec_count(5);
    assert(arm.mode == 3 && arm.reg[2] == 43 && mem_phys_read32(0x10000000) == 42);
    printf("cpu model=%s phase=ram26 pc=%08x mode=%02x r1=%08x r2=%08x event=%02x\n",
           model_name(model), PC, arm.mode, arm.reg[1], arm.reg[2], arm.event);
    cpu_reset(model); mode32(0, 0); arc_rpc_arm_exec_count(5);
    assert(arm.mode == 0x13 && arm.reg[2] == 43);
    printf("cpu model=%s phase=ram32 pc=%08x mode=%02x r1=%08x r2=%08x event=%02x\n",
           model_name(model), PC, arm.mode, arm.reg[1], arm.reg[2], arm.event);
    memcpy(rom, rpc_rom_cp15_id, sizeof(rpc_rom_cp15_id));
    cpu_reset(model); arc_rpc_arm_exec_count(1);
    const uint32_t ids[] = {0x41560610, 0x41007100, 0x4401a102, 0x41027100};
    assert(arm.reg[3] == ids[model]);
    printf("cpu model=%s phase=id r3=%08x\n", model_name(model), arm.reg[3]);
    cpu_reset(model); mode32(0x10000000, 0);
    memcpy(ram00, rpc_rom_smc_first, sizeof(rpc_rom_smc_first)); arc_rpc_arm_exec_count(1);
    assert(arm.reg[3] == 7);
    memcpy(ram00, rpc_rom_smc_second, sizeof(rpc_rom_smc_second));
    arm.reg[15] = 0x10000008; arc_rpc_arm_exec_count(1); assert(arm.reg[3] == 9);
    memcpy(rom, rpc_rom_undefined_cp15, sizeof(rpc_rom_undefined_cp15));
    cpu_reset(model); mode32(0, 0); arm.reg[4] = 0x55; arc_rpc_arm_exec_count(1);
    assert(arm.mode == 0x1b && PC == 4 && arm.reg[14] == 4 && arm.reg[4] == 0x55);
    memcpy(rom, rpc_rom_halfword, sizeof(rpc_rom_halfword));
    cpu_reset(model); mode32(0, 0); arm.reg[0] = 0x10000000; ram00[0] = 0xabcd1234;
    arc_rpc_arm_exec_count(1);
    if (model == CPUModel_SA110) assert(arm.reg[3] == 0x1234 && arm.mode == 0x13);
    else assert(arm.mode == 0x1b);
}

static void permission_tests(CPUModel model)
{
    /* Independent AP/S/R truth table: columns supervisor read/write,
     * user read/write. AP0 with both S/R set is deliberately no access. */
    static const unsigned allowed[4][4] = {{0,0,0,0},{1,0,0,0},{1,0,1,0},{0,0,0,0}};
    uint32_t *table = ram00 + (0x4000 >> 2);
    const uint32_t va = 0x00400100;
    for (unsigned ap = 0; ap < 4; ++ap)
    for (unsigned sr = 0; sr < 4; ++sr)
    for (unsigned op = 0; op < 4; ++op) {
        cpu_reset(model); table[4] = 0x10000002 | (ap << 10); enable(model, sr << 8);
        memmode = op < 2;
        int write = op & 1;
        int permit = ap == 0 ? (int)allowed[sr][op] : ap == 1 ? op < 2 : ap == 2 ? op != 3 : 1;
        translateaddress(va, write, 0);
        if (permit) assert(!(arm.event & 0x40)); else fault(0xd, va);
    }
    for (unsigned domain = 0; domain < 16; ++domain)
    for (unsigned access = 0; access < 4; ++access) {
        cpu_reset(model); table[4] = 0x10000002 | (domain << 5); enable(model, 0);
        cp_write(3, access << (domain * 2)); memmode = 0;
        translateaddress(va, 1, 0);
        if (access == 3) assert(!(arm.event & 0x40));
        else fault((domain << 4) | (access == 1 ? 0xd : 9), va);
    }
    cpu_reset(model); table[4] = 0x10000402; enable(model, 0);
    memmode = 1; ram00[0x100 >> 2] = 0x11223344;
    assert(mem_read32(va) == 0x11223344);
    /* A privileged hit cannot grant an LDRT/STRT, byte or ordinary User hit. */
    assert(mem_user_read32(va) == 0); fault(0xd, va);
    mem_user_write32(va, 0xabcdef01); fault(0xd, va);
    mem_user_read8(va); fault(0xd, va); mem_user_write8(va, 0xfe); fault(0xd, va);
    memmode = 0; mem_read32(va); fault(0xd, va); mem_write32(va, 0); fault(0xd, va);
    assert(ram00[0x100 >> 2] == 0x11223344);
    uint32_t far = cp_read(6), fsr = cp_read(5);
    assert(!getpccache(va)); assert(cp_read(6) == far && cp_read(5) == fsr);
    memmode = 1; assert(getpccache(va));
    table[4] = 0; /* stale TLB mapping persists until guest invalidates */
    assert(mem_read32(va) == 0x11223344); flush(model); mem_read32(va); fault(5, va);
    table[4] = 3; flush(model); mem_read32(va); fault(5, va);
    table[4] = 0x10000c02; flush(model); assert(mem_read32(va) == 0x11223344);
    cp_write(2, 0x14004000); mem_read32(va); fault(5, va);
    cp_write(2, 0x10004000); assert(mem_read32(va) == 0x11223344);
    cp_write(3, 0); mem_read32(va); fault(9, va);
    cp_write(3, 1); assert(mem_read32(va) == 0x11223344);
    cp_write(1, cp_read(1) & ~1u); assert(mem_read32(0x10000100) == 0x11223344);
}

static void page_tests(CPUModel model)
{
    uint32_t *table = ram00 + (0x4000 >> 2), *pages = ram00 + (0x8000 >> 2);
    cpu_reset(model); table[4] = 0x10008001; enable(model, 0);
    /* APs 3,2,1,0 from low to high subpage. */
    pages[0] = 0x10010000 | (3 << 4) | (2 << 6) | (1 << 8) | 2;
    for (unsigned sub = 0; sub < 4; ++sub) {
        uint32_t va = 0x00400000 | (sub << 10);
        memmode = 0; translateaddress(va, 0, 0);
        if (sub < 2) assert(!(arm.event & 0x40)); else fault(0xf, va);
        translateaddress(va, 1, 0);
        if (!sub) assert(!(arm.event & 0x40)); else fault(0xf, va);
    }
    pages[0] = 0; flush(model); translateaddress(0x00400000, 0, 0); fault(7, 0x00400000);
    pages[0] = 3; flush(model); translateaddress(0x00400000, 0, 0); fault(7, 0x00400000);
    for (unsigned i = 0; i < 16; ++i) pages[i] = 0x10010000 | (3 << 4) | (2 << 6) | (1 << 8) | 1;
    flush(model);
    for (unsigned sub = 0; sub < 4; ++sub) {
        uint32_t va = 0x00400000 | (sub << 14);
        memmode = 0; translateaddress(va, 0, 0);
        if (sub < 2) assert(!(arm.event & 0x40)); else fault(0xf, va);
    }
    cp_write(3, 0); translateaddress(0x00400000, 0, 0); fault(0xb, 0x00400000);
    cp_write(3, 3); assert(translateaddress(0x0040c000, 1, 0) == 0x1001c000);
}

static void guest_abort_tests(CPUModel model)
{
    uint32_t *table = ram00 + (0x4000 >> 2), *pages = ram00 + (0x8000 >> 2);
    const uint32_t *programs[] = {rpc_rom_data_abort, rpc_rom_ldrt, rpc_rom_strt};
    for (unsigned i = 0; i < 3; ++i) {
        memcpy(rom, programs[i], 4); cpu_reset(model);
        table[0] = 0xc02; table[4] = 0x10000402; enable(model, 0);
        mode32(0, i == 0); arm.reg[0] = 0x00400100; arm.reg[1] = 0x5678;
        ram00[0x100 >> 2] = 0x1234; arc_rpc_arm_exec_count(1);
        assert(arm.mode == 0x17 && PC == 0x10 && arm.reg[14] == 8);
        assert(arm.spsr[7] == (i ? 0x13u : 0x10u) && (arm.reg[16] & 0x80));
        assert(cp_read(5) == 0xd && cp_read(6) == 0x00400100);
        assert(arm.reg[1] == 0x5678 && ram00[0x100 >> 2] == 0x1234);
        printf("cpu model=%s phase=abort%u pc=%08x mode=%02x lr=%08x spsr=%08x fsr=%08x far=%08x event=%02x\n",
               model_name(model), i, PC, arm.mode, arm.reg[14], arm.spsr[7], cp_read(5), cp_read(6), arm.event);
    }
    /* Fetch crosses a 1 KiB subpage within one cached 4 KiB page. */
    cpu_reset(model); table[0] = 0xc02; table[4] = 0x10008001;
    pages[0] = 0x10010000 | (3 << 4) | (1 << 6) | 2; enable(model, 0);
    ram00[(0x10000 + 0x3fc) >> 2] = rpc_rom_nop[0];
    mode32(0x004003fc, 1); arc_rpc_arm_exec_count(1); assert(PC == 0x00400400);
    uint32_t fsr = cp_read(5), far = cp_read(6); arc_rpc_arm_exec_count(1);
    assert(arm.mode == 0x17 && PC == 0xc && arm.reg[14] == 0x00400404);
    assert(arm.spsr[7] == 0x10 && cp_read(5) == fsr && cp_read(6) == far);
    assert(!(arm.event & 0x40));
    printf("cpu model=%s phase=prefetch pc=%08x mode=%02x lr=%08x spsr=%08x fsr=%08x far=%08x event=%02x\n",
           model_name(model), PC, arm.mode, arm.reg[14], arm.spsr[7], cp_read(5), cp_read(6), arm.event);
}

int main(void)
{
    arm_init();
    const CPUModel models[] = {CPUModel_ARM610, CPUModel_ARM710, CPUModel_ARM7500, CPUModel_SA110};
    for (unsigned m = 0; m < 4; ++m) {
        config.mem_size = 64; config.vram_size = models[m] == CPUModel_ARM7500 ? 0 : 2;
        machine.cpu_model = models[m];
        const Model machines[] = {Model_RPCARM610, Model_RPCARM710, Model_A7000, Model_RPCSA110};
        machine.model = machines[m];
        protected = 1;
        for (fail_at = 1; fail_at <= (config.vram_size ? 4u : 3u); ++fail_at) {
            allocations = 0;
            if (!setjmp(boundary)) { mem_init(); assert(0); }
            arc_rpc_memory_close(); arc_rpc_memory_close();
            assert(!rom && !vram && !ram00 && !ram01 && !ram1 && !romb);
        }
        fail_at = allocations = 0;
        if (setjmp(boundary)) { fprintf(stderr, "Unexpected CPU component fault\n"); return 1; }
        mem_init();
        if (!setjmp(boundary)) { arc_rpc_arm_exec_count(201); assert(0); }
        cpu_reset(models[m]);
        if (!setjmp(boundary)) { updatemode(4); assert(0); }
        cpu_reset(models[m]);
        if (setjmp(boundary)) { fprintf(stderr, "Unexpected recovered CPU component fault\n"); return 1; }
        memory_tests(); cpu_tests(models[m]); permission_tests(models[m]);
        page_tests(models[m]); guest_abort_tests(models[m]);
        arc_rpc_memory_close();
        for (unsigned cycle = 0; cycle < 50; ++cycle) {
            mem_init(); cpu_reset(models[m]); assert(!cp_read(2) && !cp_read(3) && !cp_read(5) && !cp_read(6));
            assert(!inscount && !arm.event); arc_rpc_memory_close();
        }
        printf("result model=%s cpu=pass mmu=pass memory=pass video=pending audio=pending\n", model_name(models[m]));
    }
    return 0;
}
