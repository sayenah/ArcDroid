/* Helper-owned launch plans; no guest effects. GPL-2.0-only. */
#include "rpc_launch_plan.h"
#include <stdio.h>
#include <string.h>
static void put32(uint8_t *p, uint32_t n)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (i * 8u)); }
static uint32_t get32(const uint8_t *p)
{ uint32_t n = 0; for (unsigned i = 0; i < 4; ++i) n |= (uint32_t)p[i] << (i * 8u); return n; }
static bool nonzero(const uint8_t *p)
{ for (unsigned i = 0; i < 32; ++i) if (p[i]) return true; return false; }
static bool safe(uint8_t c)
{
   return c > 0x20u && c != 0x7fu && c != '.' && c != ':' && c != '*' &&
      c != '#' && c != '$' && c != '&' && c != '@' && c != '^' && c != '%' &&
      c != '\\' && c != '|' && c != '"' && c != '<';
}
static ArcRpcPlanResult validate(const ArcRpcLaunchPlan *p, size_t *length)
{
   if (!p) return ARC_RPC_PLAN_INVALID;
   if (p->action < ARC_RPC_PLAN_BOOT || p->action > ARC_RPC_PLAN_PRODUCT)
      return ARC_RPC_PLAN_ACTION_UNKNOWN;
   if ((p->flags & ~ARC_RPC_PLAN_RAMFS) || !nonzero(p->media)) return ARC_RPC_PLAN_INVALID;
   size_t n = 0; while (n <= ARC_RPC_PLAN_TARGET_MAX && p->target[n]) ++n;
   if (n > ARC_RPC_PLAN_TARGET_MAX) return ARC_RPC_PLAN_INVALID;
   const char *prefix = p->action == ARC_RPC_PLAN_PRODUCT ? "ADFS::4.$." : "ADFS::0.$.";
   size_t at = strlen(prefix);
   if (n <= at || memcmp(p->target, prefix, at)) return ARC_RPC_PLAN_INVALID;
   if (p->action == ARC_RPC_PLAN_BOOT)
   {
      if (strcmp(p->target, "ADFS::0.$.!Boot")) return ARC_RPC_PLAN_INVALID;
   }
   else
   {
      unsigned components = 0; size_t start = at;
      for (size_t i = at; i <= n; ++i)
      {
         if (i != n && p->target[i] != '.')
         { if (!safe((uint8_t)p->target[i])) return ARC_RPC_PLAN_INVALID; continue; }
         size_t size = i - start;
         if (!size || size > ARC_GUEST_NAME_MAX || ++components > 2u)
            return ARC_RPC_PLAN_INVALID;
         if (i == n && (size < 2u || p->target[start] != '!')) return ARC_RPC_PLAN_INVALID;
         start = i + 1u;
      }
   }
   if (length) *length = n;
   return ARC_RPC_PLAN_OK;
}
static ArcRpcPlanResult build(const ArcLaunchDiscovery *d, int index,
   ArcRpcPlanAction action, bool ramfs, const uint8_t media[32], ArcRpcLaunchPlan *out)
{
   if (!d || !out || !media || !d->readable ||
       d->candidate_count > ARC_LAUNCH_MAX_CANDIDATES) return ARC_RPC_PLAN_INVALID;
   ArcRpcLaunchPlan p = {0}; p.action = action; p.flags = ramfs ? ARC_RPC_PLAN_RAMFS : 0;
   memcpy(p.media, media, 32);
   if (action == ARC_RPC_PLAN_BOOT)
   {
      if (!d->root_boot || d->root_boot_count != 1u || !d->root_boot_is_file)
         return ARC_RPC_PLAN_INVALID;
      memcpy(p.target, "ADFS::0.$.!Boot", sizeof("ADFS::0.$.!Boot"));
   }
   else if (action == ARC_RPC_PLAN_APPLICATION || action == ARC_RPC_PLAN_PRODUCT)
   {
      if (index == -1 && arc_launch_recovery_decide(d, &index) != ARC_LAUNCH_RECOVERY_HELPER)
         return ARC_RPC_PLAN_AMBIGUOUS;
      if (index < 0 || (unsigned)index >= d->candidate_count ||
          !arc_launch_candidate_path(&d->candidates[index],
              action == ARC_RPC_PLAN_PRODUCT ? 4u : 0u, p.target, sizeof(p.target)))
         return ARC_RPC_PLAN_INVALID;
   }
   else return ARC_RPC_PLAN_ACTION_UNKNOWN;
   ArcRpcPlanResult r = validate(&p, NULL); if (r == ARC_RPC_PLAN_OK) *out = p; return r;
}
ArcRpcPlanResult arc_riscpc_plan_build(const ArcLaunchDiscovery *d, int index,
   ArcRpcPlanAction action, bool ramfs, const uint8_t media[32], ArcRpcLaunchPlan *out)
{
   if (action == ARC_RPC_PLAN_PRODUCT) return ARC_RPC_PLAN_STALE_PRODUCT;
   return build(d, index, action, ramfs, media, out);
}
ArcRpcPlanResult arc_riscpc_plan_build_product(const ArcLaunchDiscovery *d, int index,
   bool ramfs, const uint8_t media[32], const uint8_t captured[32],
   const uint8_t current[32], ArcRpcLaunchPlan *out)
{
   if (!captured || !current || !nonzero(captured) || memcmp(captured, current, 32))
      return ARC_RPC_PLAN_STALE_PRODUCT;
   return build(d, index, ARC_RPC_PLAN_PRODUCT, ramfs, media, out);
}
ArcRpcPlanResult arc_riscpc_plan_encode(const ArcRpcLaunchPlan *p, uint8_t *out,
                                    size_t capacity, size_t *written)
{
   if (written) *written = 0;
   size_t n; ArcRpcPlanResult r = validate(p, &n); if (r != ARC_RPC_PLAN_OK) return r;
   if (!out || capacity < 64u + n) return ARC_RPC_PLAN_BUFFER;
   memset(out, 0, 64); memcpy(out, "ARCRLPLN", 8); put32(out + 8, ARC_RPC_PLAN_VERSION);
   put32(out + 12, (uint32_t)(64 + n)); put32(out + 16, p->action); put32(out + 20, p->flags);
   memcpy(out + 24, p->media, 32); put32(out + 56, (uint32_t)n); memcpy(out + 64, p->target, n);
   if (written) *written = 64 + n;
   return ARC_RPC_PLAN_OK;
}
ArcRpcPlanResult arc_riscpc_plan_decode(const uint8_t *data, size_t size, ArcRpcLaunchPlan *out)
{
   if (!data || !out || size < 65u || size > ARC_RPC_PLAN_MAX_BYTES ||
       memcmp(data, "ARCRLPLN", 8)) return ARC_RPC_PLAN_INVALID;
   if (get32(data + 8) != ARC_RPC_PLAN_VERSION) return ARC_RPC_PLAN_VERSION_UNKNOWN;
   uint32_t n = get32(data + 56);
   if (!n || n > ARC_RPC_PLAN_TARGET_MAX || size != 64u + n ||
       get32(data + 12) != size || get32(data + 60) || memchr(data + 64, 0, n))
      return ARC_RPC_PLAN_INVALID;
   ArcRpcLaunchPlan p = {0}; p.action = get32(data + 16); p.flags = get32(data + 20);
   memcpy(p.media, data + 24, 32); memcpy(p.target, data + 64, n);
   ArcRpcPlanResult r = validate(&p, NULL); if (r == ARC_RPC_PLAN_OK) *out = p; return r;
}
ArcRpcPlanResult arc_riscpc_plan_command(const ArcRpcLaunchPlan *p, char *out, size_t capacity)
{
   ArcRpcPlanResult r = validate(p, NULL); if (r != ARC_RPC_PLAN_OK) return r;
   if (!out || !capacity) return ARC_RPC_PLAN_BUFFER;
   const char *verb = p->action == ARC_RPC_PLAN_BOOT ? "%Run " : "%Filer_Run ";
   size_t n = strlen(verb) + strlen(p->target);
   if (n >= capacity) return ARC_RPC_PLAN_BUFFER;
   memcpy(out, verb, strlen(verb)); memcpy(out + strlen(verb), p->target, strlen(p->target) + 1);
   return ARC_RPC_PLAN_OK;
}
bool arc_riscpc_plan_digest(const ArcRpcLaunchPlan *p, uint8_t out[32])
{
   uint8_t bytes[ARC_RPC_PLAN_MAX_BYTES]; size_t n;
   if (!out || arc_riscpc_plan_encode(p, bytes, sizeof(bytes), &n) != ARC_RPC_PLAN_OK) return false;
   ArcSha256 hash; arc_sha256_init(&hash); arc_sha256_update(&hash, bytes, n); arc_sha256_final(&hash, out); return true;
}
bool arc_riscpc_plan_label(const ArcLaunchCandidate *c, char *out, size_t capacity)
{
   char parent[41], leaf[41];
   if (!c || !out || c->parent.length > ARC_GUEST_NAME_MAX ||
       c->name.length > ARC_GUEST_NAME_MAX ||
       !arc_launch_escape_name(&c->parent, parent, sizeof(parent)) ||
       !arc_launch_escape_name(&c->name, leaf, sizeof(leaf))) return false;
   int n = snprintf(out, capacity, "%s%s%s", parent, c->parent.length ? "." : "", leaf);
   return n >= 0 && (size_t)n < capacity;
}
