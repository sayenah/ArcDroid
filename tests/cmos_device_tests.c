/* ArcDroid CMOS bus tests. GPL-2.0-only. */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "cmos.h"
#include "config.h"
#include "timer.h"

int machine_type = MACHINE_TYPE_NORMAL;
uint64_t TIMER_USEC = UINT64_C(1) << 32;
void timer_enable(emu_timer_t *timer) { timer->enabled = 1; }
void timer_add(emu_timer_t *timer, void (*callback)(void *), void *opaque, int start)
{ timer->callback = callback; timer->p = opaque; timer->enabled = start; }
int machine_is_a500(void) { return 0; }
uint8_t bmu_read(int address) { (void)address; return 0xffu; }
void bmu_write(int address, uint8_t value) { (void)address; (void)value; }
void rpclog(const char *format, ...) { (void)format; }
void fatal(const char *format, ...) { (void)format; assert(0); }

static unsigned ignored_count, changed_count, ignored_by_raw[256];
static uint8_t locked_raw[256];
static void ignored(void *opaque, unsigned raw, uint8_t attempted, uint8_t retained)
{
   (void)opaque;
   assert(raw < 256u && locked_raw[raw]);
   ++ignored_count;
   ++ignored_by_raw[raw];
   if (attempted != retained) ++changed_count;
}

static void bus_start(void)
{
   i2c_change(1, 1);
   i2c_change(1, 0);
}

static void bus_byte(uint8_t value)
{
   for (unsigned bit = 0u; bit < 8u; ++bit)
   {
      int level = (value & (0x80u >> bit)) != 0u;
      i2c_change(0, level);
      i2c_change(1, level);
   }
   i2c_change(0, 1);
   i2c_change(1, 1); /* device ACK */
   assert(i2c_data == 0);
   i2c_change(0, 0);
}

static void bus_stop(void)
{
   i2c_change(0, 0);
   i2c_change(1, 0);
   i2c_change(1, 1);
}

static void bus_write(unsigned raw, const uint8_t *bytes, unsigned count)
{
   bus_start();
   bus_byte(0xa0u);
   bus_byte((uint8_t)raw);
   for (unsigned i = 0u; i < count; ++i) bus_byte(bytes[i]);
   bus_stop();
}

static void bus_read_many(unsigned raw, uint8_t *output, unsigned count)
{
   uint8_t image[256];
   bus_start();
   bus_byte(0xa0u);
   bus_byte((uint8_t)raw);
   bus_stop();
   bus_start();
   bus_byte(0xa1u);
   for (unsigned i = 0u; i < count; ++i)
   {
      uint8_t result = 0u;
      for (unsigned bit = 0u; bit < 8u; ++bit)
      {
         i2c_change(0, 1);
         i2c_change(1, 1);
         result = (uint8_t)((result << 1u) | (i2c_data != 0));
      }
      output[i] = result;
      i2c_change(0, i + 1u == count);
      i2c_change(1, i + 1u == count); /* ACK until the final NACK */
   }
   cmos_get_image(image, sizeof(image));
   for (unsigned i = 0u; i < count; ++i)
      assert(output[i] == image[(raw + i) & 0xffu]);
}

static uint8_t bus_read(unsigned raw)
{
   uint8_t value;
   bus_read_many(raw, &value, 1u);
   return value;
}

int main(void)
{
   uint8_t image[256] = {0};
   uint8_t mask[32] = {0};
   image[0xedu] = 0x31u;
   image[0x46u] = 0x41u;
   cmos_set_image(image, sizeof(image));
   mask[0xedu >> 3u] = (uint8_t)(1u << (0xedu & 7u));
   mask[0x46u >> 3u] = (uint8_t)(1u << (0x46u & 7u));
   locked_raw[0xedu] = locked_raw[0x46u] = 1u;
   cmos_set_write_guard(mask, ignored, NULL);
   uint8_t bytes[] = {0x92u, 0x83u};
   bus_write(0xedu, bytes, 2u); /* locked then unlocked */
   cmos_get_image(image, sizeof(image));
   assert(image[0xedu] == 0x31u && image[0xeeu] == 0x83u);
   assert(bus_read(0xedu) == 0x31u);
   bytes[0] = 0x99u;
   bus_write(0x46u, bytes, 1u);
   assert(bus_read(0x46u) == 0x41u);
   bytes[0] = 0x55u;
   bus_write(0x0fu, bytes, 1u);
   assert(bus_read(0x0fu) == 0x55u);
   bytes[0] = 0x63u;
   bus_write(0x3fu, bytes, 1u); /* guest checksum unchanged */
   assert(bus_read(0x3fu) == 0x63u);
   bytes[0] = 0x72u; bytes[1] = 0x73u;
   bus_write(0xffu, bytes, 2u); /* raw address wraps to zero */
   assert(bus_read(0xffu) == 0x72u && bus_read(0x00u) == 0x73u);
   assert(ignored_count == 2u && changed_count == 2u);

   /* Four bit-banged sequential writes cross all locked runs, their
    * unlocked neighbours, the checksum byte, and the 0xFF -> 0x00 wrap. */
   static const unsigned locked_set[] = {0x46u, 0x47u, 0x52u, 0x53u,
                                         0xedu, 0xeeu, 0xefu, 0xf0u,
                                         0x29u, 0x2au, 0x2bu};
   static const struct { unsigned start, count; } spans[] = {
      {0x44u, 17u}, {0x27u, 6u}, {0xecu, 26u}, {0x3eu, 3u}
   };
   uint8_t locked_original[256] = {0};
   memset(image, 0, sizeof(image));
   memset(mask, 0, sizeof(mask));
   memset(locked_raw, 0, sizeof(locked_raw));
   memset(ignored_by_raw, 0, sizeof(ignored_by_raw));
   ignored_count = changed_count = 0u;
   for (unsigned i = 0u; i < sizeof(locked_set) / sizeof(locked_set[0]); ++i)
   {
      unsigned raw = locked_set[i];
      image[raw] = (uint8_t)(0x60u + i);
      locked_original[raw] = image[raw];
      locked_raw[raw] = 1u;
      mask[raw >> 3u] |= (uint8_t)(1u << (raw & 7u));
   }
   cmos_set_image(image, sizeof(image));
   cmos_set_write_guard(mask, ignored, NULL);
   for (unsigned span = 0u; span < sizeof(spans) / sizeof(spans[0]); ++span)
   {
      uint8_t values[26], readback[26];
      unsigned start = spans[span].start, count = spans[span].count;
      for (unsigned i = 0u; i < count; ++i)
         values[i] = (uint8_t)(0xa0u + span * 32u + i);
      bus_write(start, values, count);
      cmos_get_image(image, sizeof(image));
      for (unsigned i = 0u; i < count; ++i)
      {
         unsigned raw = (start + i) & 0xffu;
         if (locked_raw[raw])
            assert(image[raw] == locked_original[raw] && ignored_by_raw[raw] == 1u);
         else
            assert(image[raw] == values[i]);
      }
      bus_read_many(start, readback, count);
      for (unsigned i = 0u; i < count; ++i)
         assert(readback[i] == image[(start + i) & 0xffu]);
   }
   assert(ignored_count == 11u && changed_count == 11u);
   for (unsigned i = 0u; i < sizeof(locked_set) / sizeof(locked_set[0]); ++i)
      assert(ignored_by_raw[locked_set[i]] == 1u);
   memset(mask, 0, sizeof(mask)); /* RISC OS 2 class has no locked bytes. */
   cmos_set_write_guard(mask, ignored, NULL);
   bytes[0] = 0x22u;
   bus_write(0xedu, bytes, 1u);
   assert(bus_read(0xedu) == 0x22u);
   bytes[0] = 0x23u;
   bus_write(0x46u, bytes, 1u);
   assert(bus_read(0x46u) == 0x23u && ignored_count == 11u);
   cmos_set_write_guard(NULL, NULL, NULL);
   puts("cmos_device_tests: all checks passed");
   return 0;
}
