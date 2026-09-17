/* ArcDroid CMOS records. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <string.h>

static void arc_put32(uint8_t *p, uint32_t value)
{
   p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
   p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

static uint8_t arc_machine_tag(ArcMachine machine)
{
   const ArcMachineDescriptor *descriptor = arc_machine_descriptor(machine);
   const char *name = descriptor ? descriptor->name : NULL;
   uint8_t result = 0u;
   if (name)
      while (*name) result = (uint8_t)(result * 33u + (uint8_t)*name++);
   return result;
}

bool arc_cmos_encode(uint8_t record[ARC_CMOS_RECORD_SIZE], const ArcLaunchPlan *plan,
                     const uint8_t payload[ARC_CMOS_PAYLOAD_SIZE])
{
   if (!record || !plan || !payload) return false;
   memset(record, 0, ARC_CMOS_RECORD_SIZE);
   memcpy(record, "ARCCMOS2", 8u);
   record[8] = 2u;
   record[9] = (uint8_t)plan->os;
   record[10] = (uint8_t)(plan->memory_kib / 1024u);
   record[11] = arc_machine_tag(plan->machine);
   arc_put32(record + 12u, arc_crc32(plan->content_id, 40u));
   memcpy(record + 16u, payload, ARC_CMOS_PAYLOAD_SIZE);
   return true;
}

bool arc_cmos_decode(const uint8_t record[ARC_CMOS_RECORD_SIZE], const ArcLaunchPlan *plan,
                     uint8_t payload[ARC_CMOS_PAYLOAD_SIZE])
{
   uint32_t content_tag;
   if (!record || !plan || !payload || memcmp(record, "ARCCMOS2", 8u) != 0) return false;
   content_tag = (uint32_t)record[12] | ((uint32_t)record[13] << 8) |
                 ((uint32_t)record[14] << 16) | ((uint32_t)record[15] << 24);
   if (record[8] != 2u || record[9] != (uint8_t)plan->os ||
       record[10] != (uint8_t)(plan->memory_kib / 1024u) ||
       record[11] != arc_machine_tag(plan->machine) ||
       content_tag != arc_crc32(plan->content_id, 40u)) return false;
   memcpy(payload, record + 16u, ARC_CMOS_PAYLOAD_SIZE);
   return true;
}
