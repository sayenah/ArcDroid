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
   result(1u, 0x18u, 0u); /* Two sides, track zero, not ready. */
   /* Reset during parameters/results restores the entire controller. */
   command(0x46u, data, 3u);
   iomd.fiq.status |= IOMD_FIQ_FLOPPY_DMA_REQUEST;
   fdc_reset();
   assert(!fdccallback && !motoron && fdc_read(0x3f4u) == 0x80u);
   assert(!(iomd.fiq.status & IOMD_FIQ_FLOPPY_DMA_REQUEST));
   command(0x4au, drive, sizeof(drive));
   result(7u, 0x40u, 5u);
   /* Callback-driven commands stop accepting parameters after their quota. */
   const uint8_t specify[] = {0xdfu, 2u};
   command(0x03u, specify, sizeof(specify));
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
   puts("rpc-fdc-empty: no-media results, no stale DMA, invalid traffic and reset passed");
   return 0;
}
