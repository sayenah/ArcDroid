/* Authored deterministic RTC and bit-level PCF8583 checks. GPL-2.0-only. */
#include <assert.h>
#include "../engine/arc_fault_jump.h"
#include <stdio.h>
#include <string.h>
#include "rpc_namespace.h"
#include "rpc_rtc.h"
#include "rpcemu.h"
#include "cmos.h"
#undef NDEBUG
#include <assert.h>

static uint64_t epoch, elapsed;
static jmp_buf boundary;
bool arc_rpc_rtc_read(ArcRpcCalendar *out) { return arc_rpc_calendar(epoch, elapsed, out); }
void fatal(const char *format, ...) { (void)format; longjmp(boundary, 1); }
void rpclog(const char *format, ...) { (void)format; }

static void start(void) { cmosi2cchange(1,1); cmosi2cchange(1,0); }
static void stop(void) { cmosi2cchange(0,0); cmosi2cchange(1,0); cmosi2cchange(1,1); }
static bool send(uint8_t byte)
{
    for (int bit = 7; bit >= 0; --bit) {
        int value = (byte >> bit) & 1;
        cmosi2cchange(0, value); cmosi2cchange(1, value);
    }
    cmosi2cchange(0,1); bool ack = !i2cdata; cmosi2cchange(1,1);
    return ack;
}
static uint8_t receive(bool ack)
{
    uint8_t byte = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        cmosi2cchange(0,1); byte = (byte << 1) | i2cdata; cmosi2cchange(1,1);
    }
    cmosi2cchange(0,1); cmosi2cchange(1,!ack);
    return byte;
}
static void address(unsigned reg)
{
    start(); assert(send(0xa0)); assert(send((uint8_t)reg)); stop();
}
static uint8_t read_reg(unsigned reg)
{
    address(reg); start(); assert(send(0xa1)); uint8_t result = receive(false); stop();
    return result;
}

int main(void)
{
    if (setjmp(boundary)) { fprintf(stderr,"Unexpected CMOS component fault\n"); return 1; }
    ArcRpcCalendar calendar;
    assert(arc_rpc_calendar(0,0,&calendar));
    assert(calendar.year == 1970 && calendar.month == 1 && calendar.day == 1 && calendar.weekday == 4);
    assert(arc_rpc_calendar(951782399, UINT64_C(1000000000), &calendar));
    assert(calendar.year == 2000 && calendar.month == 2 && calendar.day == 29 && calendar.weekday == 2);
    assert(arc_rpc_calendar(4107542399, UINT64_C(1000000000), &calendar));
    assert(calendar.year == 2100 && calendar.month == 3 && calendar.day == 1);
    assert(arc_rpc_calendar(13569465599,999999999,&calendar));
    assert(calendar.year == 2399 && calendar.month == 12 && calendar.day == 31 && calendar.centisecond == 99);
    assert(!arc_rpc_calendar(13569465599,UINT64_C(1000000000),&calendar));
    assert(!arc_rpc_calendar(UINT64_MAX,0,&calendar)); assert(!arc_rpc_calendar(0,0,NULL));
    uint8_t seed[256], snapshot[256];
    for (unsigned i = 0; i < 256; ++i) seed[i] = (uint8_t)(i ^ 0xa5);
    cmos_init(); arc_rpc_cmos_seed(seed); reseti2c(I2C_PCF8583);
    arc_rpc_cmos_snapshot(snapshot); assert(!memcmp(snapshot,seed,256) && !arc_rpc_cmos_writes());
    assert(read_reg(0x40) == seed[0x40]);
    start(); assert(send(0xa0)); assert(send(0x40)); assert(send(0x5a)); stop();
    assert(read_reg(0x40) == 0x5a && arc_rpc_cmos_writes() == 1);
    /* Reset during a partly transmitted byte discards the transaction. */
    start(); assert(send(0xa0)); cmosi2cchange(0,0); cmosi2cchange(1,0);
    cmos_reset(); reseti2c(I2C_PCF8583);
    assert(read_reg(0x40) == 0x5a && arc_rpc_cmos_writes() == 1);
    /* Unsupported bus addresses are NACKed, and the next valid start works. */
    start(); assert(!send(0xbe)); stop(); assert(read_reg(0x40) == 0x5a);
    /* Sequential writes wrap register 255 to zero within the chip. */
    start(); assert(send(0xa0)); assert(send(255)); assert(send(0x12)); assert(send(0x34)); stop();
    assert(read_reg(255) == 0x12 && read_reg(0) == 0x34 && arc_rpc_cmos_writes() == 2);
    epoch = 951782400; elapsed = UINT64_C(3661980000000);
    assert(read_reg(1) == 0x98 && read_reg(2) == 0x01 && read_reg(3) == 0x01 && read_reg(4) == 0x01);
    assert(read_reg(5) == 0x29 && read_reg(6) == 0x42);
    arc_rpc_cmos_snapshot(snapshot);
    assert(snapshot[0xc0] == seed[0xc0] && snapshot[0xc1] == seed[0xc1]);
    arc_rpc_cmos_seed(seed); reseti2c(I2C_PCF8583); arc_rpc_cmos_snapshot(snapshot);
    assert(!memcmp(seed,snapshot,256) && !arc_rpc_cmos_writes());
    epoch = UINT64_MAX;
    if (!setjmp(boundary)) { (void)read_reg(1); assert(0); }
    reseti2c(I2C_PCF8583); epoch = elapsed = 0; assert(read_reg(2) == 0);
    puts("rtc=gregorian,epoch+elapsed,bounds cmos=seed,write,reset,wrap,nack,reload,fault");
    return 0;
}
