/* Empty-drive FDC command/IRQ/DMA tests, no guest assets. GPL-2.0-only. */
#include "rpc_namespace.h"
#include "rpcemu.h"
#include "fdc.h"

/* rpcemu.h defaults NDEBUG on in release builds; test assertions stay live. */
#undef NDEBUG
#include <assert.h>

struct iomd iomd;
void updateirqs(void) {}

static void command(uint8_t opcode, const uint8_t *parameters, unsigned count)
{
   fdc_write(0x3f5u, opcode);
   for (unsigned i = 0u; i < count; ++i) fdc_write(0x3f5u, parameters[i]);
}

static void result(unsigned count, uint8_t first, uint8_t second)
{
   assert((fdc_read(0x3f4u) & 0xc0u) == 0xc0u);
   for (unsigned i = 0u; i < count; ++i)
   {
      uint8_t byte = fdc_read(0x3f5u);
      if (!i) assert(byte == first);
      if (i == 1u) assert(byte == second);
   }
   assert(fdc_read(0x3f4u) == 0x80u);
   assert(!(iomd.fiq.status & IOMD_FIQ_FLOPPY_DMA_REQUEST));
}

int main(void)
{
   const uint8_t data[] = {0u, 2u, 0u, 1u, 2u, 1u, 0x1bu, 0xffu};
   const uint8_t format[] = {0u, 2u, 9u, 0x1bu, 0xffu};
   const uint8_t drive[] = {0u};
   const uint8_t reads[] = {0x46u, 0x45u, 0x56u};
   for (unsigned i = 0u; i < sizeof(reads); ++i)
   {
      fdc_reset();
      command(reads[i], data, sizeof(data));
      assert(iomd.irqb.status & IOMD_IRQB_FLOPPY);
      assert(!fdccallback);
      /* A refused write must never raise DMA after ending the command. */
      assert(!(iomd.fiq.status & IOMD_FIQ_FLOPPY_DMA_REQUEST));
      result(7u, 0x40u, 5u);
   }
   fdc_reset();
   command(0x4du, format, sizeof(format));
   result(7u, 0x40u, 5u);
   fdc_reset();
   command(0x4au, drive, sizeof(drive));
   result(7u, 0x40u, 5u);
   fdc_reset();
   command(0x0au, drive, sizeof(drive));
   assert(fdccallback == 4000);
   fdccallback = 0;
   fdc_callback();
   result(7u, 0x40u, 5u);
   /* Invalid commands return one byte; traffic while a result waits is ignored. */
   fdc_reset();
   command(0xffu, NULL, 0u);
   command(0xfeu, NULL, 0u);
   result(1u, 0x80u, 0u);
   command(0x04u, drive, sizeof(drive));
   fdccallback = 0;
   fdc_callback();
   result(1u, 0x38u, 0u); /* SMC fixed bits 3/5 and mechanical track zero. */
   /* Reset during parameters/results restores the entire controller. */
   command(0x46u, data, 3u);
   iomd.fiq.status |= IOMD_FIQ_FLOPPY_DMA_REQUEST;
   fdc_reset();
   assert(!fdccallback && !motoron && fdc_read(0x3f4u) == 0x80u);
   assert(!(iomd.fiq.status & IOMD_FIQ_FLOPPY_DMA_REQUEST));
   command(0x4au, drive, sizeof(drive));
   result(7u, 0x40u, 5u);
   /* Callback-driven commands stop accepting parameters after their quota. */
   const uint8_t seek[] = {0u, 2u};
   command(0x0fu, seek, sizeof(seek));
   for (unsigned i = 0u; i < 1000u; ++i) fdc_write(0x3f5u, i);
   fdc_callback();
   assert(fdc_read(0x3f4u) == 0x80u && !fdccallback);
   /* No lifetime counters can overflow; the bounded result ring wraps. */
   for (unsigned i = 0u; i < 10000u; ++i)
   {
      command(0xffu, NULL, 0u);
      result(1u, 0x80u, 0u);
   }
   command(0x46u, data, 3u);
   fdc_write(0x3f2u, 0u);
   assert(fdc_read(0x3f4u) == 0x80u && !fdccallback);
   command(0x4au, drive, sizeof(drive));
   result(7u, 0x40u, 5u);
   /* SMC reset/readiness: all FIFO writes in the 500 us reset window are
    * rejected, not allowed to consume the scheduled reset callback. */
   const uint8_t control[] = {0x03, 0x04, 0x13, 0x08, 0x0e};
   for (unsigned i = 0; i < sizeof(control); ++i) {
      fdc_reset(); fdc_write(0x3f2, 0x1c);
      assert(fdccallback == 500 && !(fdc_read(0x3f4) & 0x80));
      command(control[i], data, 3);
      assert(fdccallback == 500 && !(iomd.irqb.status & IOMD_IRQB_FLOPPY));
      fdc_callback();
      assert(fdc_read(0x3f4) == 0x80 && !fdccallback);
      assert(iomd.irqb.status & IOMD_IRQB_FLOPPY);
      for (unsigned drive_index = 0; drive_index < 4; ++drive_index) {
         command(0x08, NULL, 0); result(2, 0xc0 | drive_index, 0);
      }
      command(0x04, drive, sizeof(drive)); result(1, 0x38, 0);
      command(0x03, data, 2); assert(fdc_read(0x3f4) == 0x80);
      command(0x13, data, 3); assert(fdc_read(0x3f4) == 0x80);
   }
   /* DSR aborts parameters/results/DMA, preserves DOR/motors/physical heads,
    * self-clears, then produces all four reset SIS causes. */
   fdc_reset(); fdc_write(0x3f2, 0x1c); fdc_callback();
   const uint8_t position[] = {0, 9};
   command(0x0f, position, 2); fdc_callback();
   command(0x46, data, 3);
   iomd.fiq.status |= IOMD_FIQ_FLOPPY_DMA_REQUEST;
   fdc_write(0x3f4, 0x80);
   assert(fdc_read(0x3f2) == 0x1c && motoron == 0x10);
   assert(!iomd.fiq.status && fdccallback == 500 && fdc_read(0x3f4) == 0);
   command(0x0e, NULL, 0); assert(fdccallback == 500);
   fdc_callback();
   for (unsigned i = 0; i < 4; ++i) { command(0x08, NULL, 0); result(2, 0xc0 | i, 0); }
   command(0x04, drive, 1); result(1, 0x28, 0); /* head still at cylinder 9 */
   command(0xff, NULL, 0); fdc_write(0x3f4, 0x80); fdc_callback();
   command(0x08, NULL, 0); result(2, 0xc0, 0); /* old result was discarded */
   fdc_write(0x3f2, 0); fdc_write(0x3f4, 0x80);
   assert(!fdccallback && fdc_read(0x3f4) == 0); /* DOR holds reset */
   fdc_write(0x3f2, 0x0c); assert(fdccallback == 500); fdc_callback();
   command(0x08, NULL, 0); result(2, 0xc0, 0);
   puts("rpc-fdc-empty: no-media results, no stale DMA, invalid traffic and reset passed");
   return 0;
}
