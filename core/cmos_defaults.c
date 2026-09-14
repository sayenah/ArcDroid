/* ArcDroid embedded CMOS defaults. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "cmos_defaults_data.h"

#include <string.h>

static const uint8_t arthur[ARC_CMOS_PAYLOAD_SIZE] = { ARC_CMOS_ARTHUR_BYTES };
static const uint8_t riscos2[ARC_CMOS_PAYLOAD_SIZE] = { ARC_CMOS_RISCOS2_BYTES };
static const uint8_t riscos3_old[ARC_CMOS_PAYLOAD_SIZE] = { ARC_CMOS_RISCOS3_OLD_BYTES };

bool arc_cmos_default(ArcOsClass os, uint8_t output[ARC_CMOS_PAYLOAD_SIZE])
{
   const uint8_t *source = NULL;
   if (!output) return false;
   switch (os)
   {
      case ARC_OS_TEST:
         memset(output, 0, ARC_CMOS_PAYLOAD_SIZE);
         return true;
      case ARC_OS_ARTHUR_120:
      case ARC_OS_ARTHUR_030:
         source = arthur;
         break;
      case ARC_OS_RISCOS_201:
      case ARC_OS_RISCOS_200:
         source = riscos2;
         break;
      case ARC_OS_RISCOS_300:
      case ARC_OS_RISCOS_310:
      case ARC_OS_RISCOS_311:
      case ARC_OS_RISCOS_319:
         source = riscos3_old;
         break;
      default:
         return false;
   }
   memcpy(output, source, ARC_CMOS_PAYLOAD_SIZE);
   return true;
}
