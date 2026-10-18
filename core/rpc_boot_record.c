/* Canonical RPC observation envelope. GPL-2.0-only. */
#include "rpc_boot_record.h"
#include "rpc_selection.h"
#include <string.h>
static void put32(uint8_t *p, uint32_t n)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (i * 8u)); }
static uint32_t get32(const uint8_t *p)
{ uint32_t n = 0; for (unsigned i = 0; i < 4; ++i) n |= (uint32_t)p[i] << (i * 8u); return n; }
static bool digest_valid(const uint8_t *p)
{ for (unsigned i = 0; i < 32; ++i) if (p[i]) return true; return false; }
static bool key_valid(const ArcRpcBootKey *k)
{
   return k && k->cpu >= ARC_RPC_SELECT_ARM610 && k->cpu <= ARC_RPC_SELECT_ARM7500 &&
      k->plan_revision == ARC_RPC_PLAN_VERSION && k->profile_revision &&
      k->action >= ARC_RPC_PLAN_BOOT && k->action <= ARC_RPC_PLAN_PRODUCT &&
      digest_valid(k->firmware) && digest_valid(k->configuration) && digest_valid(k->content) &&
      digest_valid(k->media) && digest_valid(k->store) && digest_valid(k->plan) && digest_valid(k->helper);
}
static bool observation_valid(ArcRpcAttemptOutcome o)
{ return o == ARC_RPC_OUTCOME_RETURNED || o == ARC_RPC_OUTCOME_TERMINATED || o == ARC_RPC_OUTCOME_DESKTOP; }
static void key_encode(const ArcRpcBootKey *k, uint8_t *p)
{
   put32(p + 20, k->cpu); put32(p + 24, k->plan_revision);
   put32(p + 28, k->profile_revision); put32(p + 32, k->action);
   memcpy(p + 40, k->firmware, 32); memcpy(p + 72, k->configuration, 32);
   memcpy(p + 104, k->content, 32); memcpy(p + 136, k->media, 32);
   memcpy(p + 168, k->store, 32); memcpy(p + 200, k->plan, 32); memcpy(p + 232, k->helper, 32);
}
static bool key_equal(const ArcRpcBootKey *a, const ArcRpcBootKey *b)
{
   if (!key_valid(a) || !key_valid(b)) return false;
   uint8_t x[264] = {0}, y[264] = {0}; key_encode(a, x); key_encode(b, y);
   return !memcmp(x, y, sizeof(x));
}
bool arc_riscpc_boot_record_capture(const ArcRpcLaunchAttempt *a, const ArcRpcBootKey *captured,
                                 const ArcRpcBootKey *current, ArcRpcBootRecord *out)
{
   if (!out || !arc_riscpc_attempt_established(a) || !key_equal(captured, current) ||
       !arc_riscpc_attempt_matches(a, &a->token) || memcmp(a->token.plan, current->plan, 32) ||
       a->action != current->action || memcmp(a->media, current->media, 32)) return false;
   *out = (ArcRpcBootRecord){*current, a->outcome}; return true;
}
bool arc_riscpc_boot_record_encode(const ArcRpcBootRecord *r, uint8_t *out, size_t capacity)
{
   if (!r || !out || capacity < ARC_RPC_BOOT_RECORD_BYTES || !key_valid(&r->key) ||
       !observation_valid(r->observation)) return false;
   memset(out, 0, ARC_RPC_BOOT_RECORD_BYTES); memcpy(out, "ARCRBOOT", 8);
   put32(out + 8, ARC_RPC_BOOT_RECORD_VERSION); put32(out + 12, ARC_RPC_BOOT_RECORD_REVISION);
   put32(out + 16, ARC_RPC_BOOT_RECORD_BYTES); key_encode(&r->key, out);
   put32(out + 36, r->observation); put32(out + 284, arc_crc32(out, 284)); return true;
}
bool arc_riscpc_boot_record_decode(const uint8_t *p, size_t size, ArcRpcBootRecord *out)
{
   if (!p || !out || size != ARC_RPC_BOOT_RECORD_BYTES || memcmp(p, "ARCRBOOT", 8) ||
       get32(p + 8) != ARC_RPC_BOOT_RECORD_VERSION || get32(p + 12) != ARC_RPC_BOOT_RECORD_REVISION ||
       get32(p + 16) != size || get32(p + 284) != arc_crc32(p, 284)) return false;
   for (unsigned i = 264; i < 284; ++i) if (p[i]) return false;
   ArcRpcBootRecord r = {0}; r.key.cpu = get32(p + 20); r.key.plan_revision = get32(p + 24);
   r.key.profile_revision = get32(p + 28); r.key.action = get32(p + 32);
   r.observation = (ArcRpcAttemptOutcome)get32(p + 36);
   memcpy(r.key.firmware, p + 40, 32); memcpy(r.key.configuration, p + 72, 32);
   memcpy(r.key.content, p + 104, 32); memcpy(r.key.media, p + 136, 32);
   memcpy(r.key.store, p + 168, 32); memcpy(r.key.plan, p + 200, 32); memcpy(r.key.helper, p + 232, 32);
   if (!key_valid(&r.key) || !observation_valid(r.observation)) return false;
   *out = r; return true;
}
bool arc_riscpc_boot_record_matches(const ArcRpcBootRecord *r, const ArcRpcBootKey *current)
{ return r && observation_valid(r->observation) && key_equal(&r->key, current); }
