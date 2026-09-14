/* ArcDroid CMOS records. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <string.h>

static void arc_put32(uint8_t *p, uint32_t value)
{
   p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
   p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

bool arc_cmos_encode(uint8_t record[ARC_CMOS_RECORD_SIZE], const ArcLaunchPlan *plan,
                     const uint8_t payload[ARC_CMOS_PAYLOAD_SIZE])
{
   if (!record || !plan || !payload) return false;
   memset(record, 0, ARC_CMOS_RECORD_SIZE);
   memcpy(record, "ARCCMOS1", 8u);
   arc_put32(record + 8u, 1u);
   arc_put32(record + 12u, (uint32_t)plan->os);
   arc_put32(record + 16u, plan->memory_kib);
   memcpy(record + 20u, plan->content_id, 40u);
   memcpy(record + 64u, payload, ARC_CMOS_PAYLOAD_SIZE);
   arc_put32(record + 60u, arc_crc32(record + 64u, ARC_CMOS_PAYLOAD_SIZE));
   return true;
}

bool arc_cmos_decode(const uint8_t record[ARC_CMOS_RECORD_SIZE], const ArcLaunchPlan *plan,
                     uint8_t payload[ARC_CMOS_PAYLOAD_SIZE])
{
   uint32_t os, memory, crc;
   if (!record || !plan || !payload || memcmp(record, "ARCCMOS1", 8u) != 0) return false;
   if (record[8] != 1u || record[9] || record[10] || record[11]) return false;
   os = (uint32_t)record[12] | ((uint32_t)record[13] << 8) | ((uint32_t)record[14] << 16) | ((uint32_t)record[15] << 24);
   memory = (uint32_t)record[16] | ((uint32_t)record[17] << 8) | ((uint32_t)record[18] << 16) | ((uint32_t)record[19] << 24);
   crc = (uint32_t)record[60] | ((uint32_t)record[61] << 8) | ((uint32_t)record[62] << 16) | ((uint32_t)record[63] << 24);
   if (os != (uint32_t)plan->os || memory != plan->memory_kib ||
       memcmp(record + 20u, plan->content_id, 40u) != 0 ||
       crc != arc_crc32(record + 64u, ARC_CMOS_PAYLOAD_SIZE)) return false;
   memcpy(payload, record + 64u, ARC_CMOS_PAYLOAD_SIZE);
   return true;
}
