/* ArcDroid embedded CMOS defaults. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "cmos_defaults_data.h"

#include <string.h>

static const uint8_t arthur[ARC_CMOS_MACHINE_SIZE] = { ARC_CMOS_ARTHUR_BYTES };
static const uint8_t riscos2[ARC_CMOS_MACHINE_SIZE] = { ARC_CMOS_RISCOS2_BYTES };
static const uint8_t riscos3_old[ARC_CMOS_MACHINE_SIZE] = { ARC_CMOS_RISCOS3_OLD_BYTES };
static const uint8_t riscos3_new[ARC_CMOS_MACHINE_SIZE] = { ARC_CMOS_RISCOS3_NEW_BYTES };

static const unsigned unplug_bytes[] = {6u, 7u, 18u, 19u, 173u, 174u, 175u, 176u, 217u, 218u, 219u};

unsigned arc_cmos_raw_address(unsigned logical)
{
   unsigned raw = logical + 0x40u;
   return raw > 255u ? raw - 240u : raw;
}

unsigned arc_cmos_logical_address(unsigned raw)
{
   return raw >= 0x40u ? raw - 0x40u : raw + 176u;
}

static bool arc_cmos_locks_unplug(ArcOsClass os)
{
   return os == ARC_OS_TEST || os == ARC_OS_RISCOS_300 ||
          os == ARC_OS_RISCOS_310 || os == ARC_OS_RISCOS_311 ||
          os == ARC_OS_RISCOS_319;
}

void arc_cmos_locked_raw_mask(ArcOsClass os, uint8_t mask[32])
{
   if (!mask) return;
   memset(mask, 0, 32u);
   if (!arc_cmos_locks_unplug(os)) return;
   for (unsigned i = 0u; i < sizeof(unplug_bytes) / sizeof(unplug_bytes[0]); ++i)
   {
      unsigned raw = arc_cmos_raw_address(unplug_bytes[i]);
      mask[raw >> 3u] |= (uint8_t)(1u << (raw & 7u));
   }
}

unsigned arc_cmos_clear_unplug_bytes(uint8_t payload[ARC_CMOS_PAYLOAD_SIZE], ArcOsClass os)
{
   unsigned cleared = 0u;
   if (!payload || !arc_cmos_locks_unplug(os)) return 0u;
   for (unsigned i = 0u; i < sizeof(unplug_bytes) / sizeof(unplug_bytes[0]); ++i)
   {
      unsigned offset = arc_cmos_raw_address(unplug_bytes[i]) - 16u;
      if (payload[offset]) ++cleared;
      payload[offset] = 0u;
   }
   return cleared;
}

static const uint8_t *arc_cmos_source(ArcOsClass os, ArcIoClass io)
{
   switch (os)
   {
      case ARC_OS_ARTHUR_120:
      case ARC_OS_ARTHUR_030:
         return arthur;
      case ARC_OS_RISCOS_201:
      case ARC_OS_RISCOS_200:
         return riscos2;
      case ARC_OS_RISCOS_300:
      case ARC_OS_RISCOS_310:
      case ARC_OS_RISCOS_311:
      case ARC_OS_RISCOS_319:
         return io == ARC_IO_NEW_82C711 ? riscos3_new : riscos3_old;
      default:
         return NULL;
   }
}

bool arc_cmos_default(ArcOsClass os, ArcIoClass io, uint8_t output[ARC_CMOS_PAYLOAD_SIZE])
{
   const uint8_t *source;
   if (!output) return false;
   if (os == ARC_OS_TEST)
   {
      memset(output, 0, ARC_CMOS_PAYLOAD_SIZE);
      return true;
   }
   source = arc_cmos_source(os, io);
   if (!source) return false;
   memcpy(output, source + 16u, ARC_CMOS_PAYLOAD_SIZE);
   if (os == ARC_OS_RISCOS_310 || os == ARC_OS_RISCOS_311 ||
       os == ARC_OS_RISCOS_319)
      output[arc_cmos_raw_address(195u) - 16u] &= (uint8_t)~0x04u;
   return true;
}

bool arc_cmos_machine_image(ArcOsClass os, ArcIoClass io,
                            const uint8_t payload[ARC_CMOS_PAYLOAD_SIZE],
                            bool launch_helper,
                            uint8_t output[ARC_CMOS_MACHINE_SIZE])
{
   const uint8_t *source;
   bool risc_os_3;
   unsigned checksum = 0u;
   if (!payload || !output) return false;
   if (os == ARC_OS_TEST)
      memset(output, 0, ARC_CMOS_MACHINE_SIZE);
   else
   {
      source = arc_cmos_source(os, io);
      if (!source) return false;
      memcpy(output, source, ARC_CMOS_MACHINE_SIZE);
   }
   memcpy(output + 16u, payload, ARC_CMOS_PAYLOAD_SIZE);
   arc_cmos_clear_unplug_bytes(output + 16u, os);
   risc_os_3 = os == ARC_OS_RISCOS_300 || os == ARC_OS_RISCOS_310 ||
               os == ARC_OS_RISCOS_311 || os == ARC_OS_RISCOS_319;
   if (risc_os_3)
   {
      /* The PCF8583 maps RISC OS byte n at raw address n+0x40, wrapping
       * past 255 to 16. Keep the guest's Boot flag; power-on Shift supplies
       * the boot inversion when it is clear. */
      if (output[69] != 8u) output[69] = 8u;              /* byte 5 */
   }
   output[75] = (uint8_t)((output[75] & ~0x07u) |
                          (launch_helper ? 1u : 0u));       /* byte 11 */
   output[199] = (uint8_t)((output[199] & ~0x07u) |
                           (launch_helper ? 2u : 1u));      /* byte 135 */
   if (io == ARC_IO_NEW_82C711)
      output[199] &= 0x07u; /* no ST506 or IDE drives */
   /* RPCEmu's PCF8583 implementation documents the RISC OS checksum:
    * sum logical bytes 0..238 and store (sum+1) modulo 256 at byte 239. */
   for (unsigned i = 0; i < 239u; ++i)
   {
      checksum += output[arc_cmos_raw_address(i)];
   }
   output[63] = (uint8_t)(checksum + 1u);       /* logical byte 239 */
   return true;
}

bool arc_cmos_power_on_shift(ArcOsClass os,
                             const uint8_t image[ARC_CMOS_MACHINE_SIZE])
{
   return image && (os == ARC_OS_RISCOS_300 || os == ARC_OS_RISCOS_310 ||
                    os == ARC_OS_RISCOS_311 || os == ARC_OS_RISCOS_319) &&
                    !(image[arc_cmos_raw_address(16u)] & 0x10u);
}

void arc_cmos_normalise_payload(uint8_t payload[ARC_CMOS_PAYLOAD_SIZE], ArcOsClass os, ArcIoClass io)
{
   if (!payload) return;
   arc_cmos_clear_unplug_bytes(payload, os);
   if (os == ARC_OS_RISCOS_300 || os == ARC_OS_RISCOS_310 ||
       os == ARC_OS_RISCOS_311 || os == ARC_OS_RISCOS_319)
      payload[arc_cmos_raw_address(16u) - 16u] &= (uint8_t)~0x10u;
   /* Logical CMOS bytes 11 and 135 live at raw PCF8583 addresses 75 and
    * 199, hence payload offsets 59 and 183. */
   payload[59] &= (uint8_t)~0x07u;
   payload[183] = (uint8_t)((payload[183] & ~0x07u) | 1u);
   if (io == ARC_IO_NEW_82C711) payload[183] &= 0x07u;
}

bool arc_cmos_boot_stamp_payload(ArcOsClass os, ArcIoClass io,
                                 const uint8_t prepared[ARC_CMOS_PAYLOAD_SIZE],
                                 bool launch_helper,
                                 uint8_t output[ARC_CMOS_PAYLOAD_SIZE])
{
   uint8_t image[ARC_CMOS_MACHINE_SIZE];
   if (!output || !arc_cmos_machine_image(os, io, prepared, launch_helper, image))
      return false;
   memcpy(output, image + 16u, ARC_CMOS_PAYLOAD_SIZE);
   arc_cmos_normalise_payload(output, os, io);
   /* The launch-owned checksum varies with the helper drive. IDE attachment
    * is also excluded: it is applied only to the live install machine. */
   output[arc_cmos_raw_address(239u) - 16u] = 0u;
   return true;
}

void arc_cmos_machine_attach_ide(uint8_t image[ARC_CMOS_MACHINE_SIZE])
{
   if (!image) return;
   /* Logical byte 135 lives at raw byte 199. Only the machine image gets
    * IDE count one; the persisted payload remains normalized to zero. */
   image[199] = (uint8_t)((image[199] & ~0xc0u) | 0x40u);
   unsigned checksum = 0u;
   for (unsigned i = 0u; i < 239u; ++i) {
      checksum += image[arc_cmos_raw_address(i)];
   }
   image[63] = (uint8_t)(checksum + 1u);
}
