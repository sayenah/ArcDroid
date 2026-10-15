/* ArcDroid R3a pure diagnostic selection; D-164/D-165. GPL-2.0-only. */
#include "rpc_selection.h"
#include <stdio.h>
#include <string.h>

static const ArcRpcFirmwareFingerprint catalogue[] = {
   {ARC_RPC_OS_350, 2097152u, UINT32_C(0x541b1415), "c31318e712ee7de92e46f9df6c791b614d4c9304"},
   {ARC_RPC_OS_360, 4194304u, UINT32_C(0xa9822c2c), "3cf75aa4d4dc8fe57110a124fbd1560d46c549b1"},
   {ARC_RPC_OS_370, 4194304u, UINT32_C(0x63fc131a), "71cbc9f12debaa7aa2aeb91d38682e6c5c4a34a5"},
   {ARC_RPC_OS_371, 4194304u, UINT32_C(0x211cf888), "c5fe0645e48894fb4b245abeefdc9a65d659af59"}
};
const ArcRpcFirmwareFingerprint *arc_riscpc_firmware_catalogue(size_t *count)
{ if (count) *count = sizeof(catalogue)/sizeof(catalogue[0]); return catalogue; }
const ArcRpcFirmwareFingerprint *arc_riscpc_firmware_recognize(const ArcDigest *d,
   const ArcRpcFirmwareFingerprint *table, size_t count)
{
   if (!d || !table) return NULL;
   for (size_t i=0; i<count; ++i)
      if (table[i].os >= ARC_RPC_OS_350 && table[i].os <= ARC_RPC_OS_371 &&
          d->size == table[i].size && d->crc32 == table[i].crc32 &&
          !strcmp(d->sha1, table[i].sha1)) return &table[i];
   return NULL;
}
ArcRpcCompatibility arc_riscpc_compatibility(bool extended, uint32_t flags)
{
   ArcRpcCompatibility c = {0,0};
   if (extended) { c.cpu = (flags >> 8u) & 0x3fu; c.os = (flags >> 16u) & 0x7fu; }
   return c;
}
bool arc_riscpc_notice_required(ArcRpcCompatibility disc)
{
   return disc.cpu && disc.os && !(disc.cpu & 3u) && !(disc.os & 3u);
}
bool arc_riscpc_selection_qualified(ArcRpcSelectionModel m, ArcRpcSelectionOs os)
{
   /* Recognition is not qualification; these are exactly D-164(3)'s seven. */
   switch (m) {
      case ARC_RPC_SELECT_ARM610: return os == ARC_RPC_OS_350;
      case ARC_RPC_SELECT_ARM710: return os == ARC_RPC_OS_360 || os == ARC_RPC_OS_371;
      case ARC_RPC_SELECT_SA110: return os == ARC_RPC_OS_370 || os == ARC_RPC_OS_371;
      case ARC_RPC_SELECT_ARM7500: return os == ARC_RPC_OS_360 || os == ARC_RPC_OS_371;
      default: return false;
   }
}
ArcRpcSelection arc_riscpc_select(const ArcRpcCompatibility *discs, size_t count,
   const ArcRpcFirmwareInventory *inventory, ArcRpcSelectionModel manual_model,
   ArcRpcSelectionOs manual_os)
{
   ArcRpcSelection r = {0};
   r.inventory_skipped = !inventory || inventory->state == ARC_RPC_INVENTORY_SKIPPED;
   r.incomplete = inventory && (inventory->vfs_failure || inventory->bounded || inventory->resource_failure);
   unsigned cpu = 0x3fu, os = 0x7fu;
   bool rpc_notice = false;
   for (size_t i=0; i<count; ++i) {
      if (arc_riscpc_notice_required(discs[i])) rpc_notice = true;
      if (discs[i].cpu) cpu &= discs[i].cpu;
      if (discs[i].os) os &= discs[i].os;
   }
   bool manual = manual_model != ARC_RPC_SELECT_AUTO || manual_os != ARC_RPC_OS_AUTO;
   /* Exactly the existing per-disc notice gate in load preparation. Do not
    * infer a new RPC route from separately unknown CPU/OS hints or from
    * incompatible legacy categories spread across discs (D-165(2)). */
   if (!manual && !rpc_notice) return r;
   if (manual_model) cpu = 0x3fu;
   if (manual_os) os = 0x7fu;
   if (!cpu || !os) {
      r.reason = ARC_RPC_SELECTION_CONFLICTING_DISCS; return r;
   }
   /* JFD category layout: MAME src/lib/formats/jfd_dsk.cpp, bits 8..22.
    * CPU and OS are independent feasibility categories, not qualifications. */
   static const ArcRpcSelectionModel rank[] = { ARC_RPC_SELECT_ARM710,
      ARC_RPC_SELECT_ARM610, ARC_RPC_SELECT_SA110, ARC_RPC_SELECT_ARM7500 };
   static const unsigned cpu_bit[] = {0,4u,4u,16u,8u};
   bool feasible = false, qualified = false;
   for (unsigned i=0; i<4; ++i) {
      ArcRpcSelectionModel m = rank[i];
      if ((manual_model && m != manual_model) || !(cpu & cpu_bit[m])) continue;
      for (unsigned j=ARC_RPC_OS_371; j>=ARC_RPC_OS_350; --j) {
         ArcRpcSelectionOs f = (ArcRpcSelectionOs)j;
         unsigned os_bit = f <= ARC_RPC_OS_360 ? 4u : 8u;
         if ((manual_os && f != manual_os) || !(os & os_bit)) continue;
         feasible = true;
         if (!arc_riscpc_selection_qualified(m,f)) continue;
         qualified = true; r.needed |= ARC_RPC_OS_BIT(f);
         if (r.reason != ARC_RPC_SELECTION_SELECTED && inventory &&
             (inventory->installed & ARC_RPC_OS_BIT(f))) {
            r.reason = ARC_RPC_SELECTION_SELECTED; r.model = m; r.os = f;
         }
      }
   }
   /* These outcomes depend only on disc hints and qualification. Limits
    * override every inventory-dependent answer, including a prefix candidate. */
   if (feasible && qualified && inventory && inventory->bounded) {
      r.reason = ARC_RPC_SELECTION_INCOMPLETE_SCAN;
      r.model = ARC_RPC_SELECT_AUTO; r.os = ARC_RPC_OS_AUTO; return r;
   }
   if (r.reason == ARC_RPC_SELECTION_SELECTED) return r;
   r.reason = !feasible ? ARC_RPC_SELECTION_UNSUPPORTED :
      !qualified ? ARC_RPC_SELECTION_UNQUALIFIED :
      inventory && inventory->vfs_failure ? ARC_RPC_SELECTION_UNREADABLE_INVENTORY :
      ARC_RPC_SELECTION_MISSING_FIRMWARE;
   return r;
}
const char *arc_riscpc_selection_reason_name(ArcRpcSelectionReason r)
{
   static const char *const names[] = {"legacy", "selected", "conflicting-discs",
      "unsupported-requirement", "unqualified-pair", "missing-firmware", "unreadable-inventory",
      "incomplete-scan"};
   return (unsigned)r < sizeof(names)/sizeof(names[0]) ? names[r] : "unsupported-requirement";
}
const char *arc_riscpc_selection_model_name(ArcRpcSelectionModel m)
{
   static const char *const names[] = {"none", "ARM610", "ARM710", "SA-110", "ARM7500"};
   return (unsigned)m < 5 ? names[m] : "none";
}
const char *arc_riscpc_selection_os_name(ArcRpcSelectionOs os)
{
   static const char *const names[] = {"none", "3.50", "3.60", "3.70", "3.71"};
   return (unsigned)os < 5 ? names[os] : "none";
}
void arc_riscpc_selection_diagnostic(const ArcRpcSelection *s, char *text, size_t capacity)
{
   char families[32] = {0};
   for (unsigned i=1; i<=4; ++i)
      if (s->needed & ARC_RPC_OS_BIT(i)) {
         if (families[0]) strcat(families, ",");
         strcat(families, arc_riscpc_selection_os_name((ArcRpcSelectionOs)i));
      }
   snprintf(text, capacity, "RPC selection: diagnostic-only reason=%s model=%s os=%s needed=%s inventory=%s\n",
      arc_riscpc_selection_reason_name(s->reason), arc_riscpc_selection_model_name(s->model),
      arc_riscpc_selection_os_name(s->os), families[0] ? families : "none",
      s->inventory_skipped ? "skipped" : s->incomplete ? "incomplete" : "complete");
}
