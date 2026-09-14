/* ArcDroid embedded CMOS defaults. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "cmos_defaults_data.h"

#include <string.h>

static const uint8_t arthur[ARC_CMOS_MACHINE_SIZE] = { ARC_CMOS_ARTHUR_BYTES };
static const uint8_t riscos2[ARC_CMOS_MACHINE_SIZE] = { ARC_CMOS_RISCOS2_BYTES };
static const uint8_t riscos3_old[ARC_CMOS_MACHINE_SIZE] = { ARC_CMOS_RISCOS3_OLD_BYTES };

static const uint8_t *arc_cmos_source(ArcOsClass os)
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
         return riscos3_old;
      default:
         return NULL;
   }
}

bool arc_cmos_default(ArcOsClass os, uint8_t output[ARC_CMOS_PAYLOAD_SIZE])
{
   const uint8_t *source;
   if (!output) return false;
   if (os == ARC_OS_TEST)
   {
      memset(output, 0, ARC_CMOS_PAYLOAD_SIZE);
      return true;
   }
   source = arc_cmos_source(os);
   if (!source) return false;
   memcpy(output, source + 16u, ARC_CMOS_PAYLOAD_SIZE);
   return true;
}

bool arc_cmos_machine_image(ArcOsClass os,
                            const uint8_t payload[ARC_CMOS_PAYLOAD_SIZE],
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
      source = arc_cmos_source(os);
      if (!source) return false;
      memcpy(output, source, ARC_CMOS_MACHINE_SIZE);
   }
   memcpy(output + 16u, payload, ARC_CMOS_PAYLOAD_SIZE);
   risc_os_3 = os == ARC_OS_RISCOS_300 || os == ARC_OS_RISCOS_310 ||
               os == ARC_OS_RISCOS_311 || os == ARC_OS_RISCOS_319;
   if (risc_os_3)
   {
      /* The PCF8583 maps RISC OS byte n at raw address n+0x40, wrapping
       * past 255 to 16. PRMs: FS=ADFS(8), drive=0, Boot, one floppy. */
      output[69] = 8u;                         /* logical byte 5 */
      output[75] &= (uint8_t)~0x07u;           /* logical byte 11 */
      output[80] |= 0x10u;                     /* logical byte 16 */
      output[199] = (output[199] & (uint8_t)~0x07u) | 1u; /* byte 135 */
   }
   /* RPCEmu's PCF8583 implementation documents the RISC OS checksum:
    * sum logical bytes 0..238 and store (sum+1) modulo 256 at byte 239. */
   for (unsigned i = 0; i < 239u; ++i)
   {
      unsigned raw = i + 0x40u;
      if (raw > 255u) raw -= 240u;
      checksum += output[raw];
   }
   output[63] = (uint8_t)(checksum + 1u);       /* logical byte 239 */
   return true;
}
