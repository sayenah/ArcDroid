/* ArcDroid FileCore disc-request observer. GPL-2.0-only. */
#include "arc_disc_request.h"
#include <limits.h>
#include <string.h>

static unsigned fold(unsigned c)
{ return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static bool equal(const char *a, const char *b, size_t n)
{
   for (size_t i = 0u; i < n; ++i)
      if (fold((unsigned char)a[i]) != fold((unsigned char)b[i])) return false;
   return true;
}
void arc_disc_request_clear(ArcDiscRequest *request)
{ if (request) memset(request, 0, sizeof(*request)); }
static void clear_observations(ArcDiscRequest *request)
{
   memset(request->identities, 0, sizeof(request->identities));
   request->next_order = 0u;
   request->observations = 0u;
   request->b_triggered = false;
}

bool arc_disc_request_name(const char *name, const char *inserted_name,
                           char canonical[256])
{
   if (!name || !canonical || !inserted_name) return false;
   size_t length = 0u;
   while (length < 255u && (unsigned char)name[length] >= 0x20u) ++length;
   if (length < 9u || length == 255u || !equal(name, "adfs::", 6u))
      return false;
   char terminated[256];
   memcpy(terminated, name, length);
   terminated[length] = 0;
   name = terminated;
   const char *disc = name + 6u;
   const char *dot = strchr(disc, '.');
   if (!dot || dot == disc || dot - disc > 10 || dot[1] != '$') return false;
   size_t disc_length = (size_t)(dot - disc);
   size_t inserted_length = strlen(inserted_name);
   if (!((disc_length == 1u && disc[0] == '0') ||
         (disc_length == inserted_length && inserted_length &&
          equal(disc, inserted_name, disc_length)))) return false;
   const char *path = dot + 1u;
   unsigned component = 0u;
   for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
   {
      if (*p == '.')
      {
         if (!component || component > 10u) return false;
         component = 0u;
      }
      else
      {
         if (*p < 0x21u || *p > 0x7eu ||
             (p != (const unsigned char *)path && *p == '$') ||
             strchr("@^&%\\<>#*;:/|\"", (int)*p)) return false;
         ++component;
      }
   }
   if (path[1] && (path[1] != '.' || !component || component > 10u))
      return false;
   if (path[1] == '.' && !path[2]) return false;
   if (!path[1]) { canonical[0] = '$'; canonical[1] = 0; return true; }
   memcpy(canonical, path, strlen(path) + 1u);
   return true;
}

bool arc_disc_request_operation(uint32_t swi, uint32_t r0,
                                ArcDiscOperation *operation)
{
   swi &= ~UINT32_C(0x20000);
   if (swi == 8u)
   {
      switch (r0)
      {
         case 5u: case 13u: case 15u: case 17u:
         case 20u: case 21u: case 22u: case 23u:
            *operation = ARC_DISC_CATALOGUE; return true;
         case 12u: case 14u: case 16u: case 255u:
            *operation = ARC_DISC_LOAD; return true;
         default: return false;
      }
   }
   if (swi != 13u || (r0 & ~255u) ||
       ((r0 & 0xc0u) != 0x40u && (r0 & 0xc0u) != 0xc0u))
      return false;
   *operation = (r0 & 4u) ? ARC_DISC_OPEN_FILE : ARC_DISC_OPEN_DIR;
   return true;
}

void arc_disc_request_swi(ArcDiscRequest *request, const ArcDiscSwi *event,
                          const char *inserted_name)
{
   if (!request || !event) return;
   uint32_t swi = event->swi & ~UINT32_C(0x20000);
   if (swi == 0x400c7u || swi == 0x400e1u || swi == 0x400c0u ||
       swi == 0x400ddu || swi == 0x400deu || swi == 0x11u || swi == 0x40u)
   { arc_disc_request_clear(request); return; }
   if (swi == 0x33u && event->r0 == 4u)
   { arc_disc_request_clear(request); return; }
   if (swi == 0x33u && event->in_rom && event->mode == 3u &&
       (event->r0 == 1u || event->r0 == 2u) && event->r1 == 8u &&
       (event->r3 == 0u || event->r3 == UINT32_MAX) &&
       event->name_readable && event->name)
   {
      size_t length = 0u;
      while (length < 11u && event->name[length] &&
             (unsigned char)event->name[length] >= 0x20u &&
             (unsigned char)event->name[length] <= 0x7eu) ++length;
      if (length && length <= 10u &&
          (unsigned char)event->name[length] < 0x20u)
      {
         request->armed = false;
         clear_observations(request);
         request->b_triggered = false;
         request->a_active = true;
         if (strlen(request->a_name) != length ||
             !equal(request->a_name, event->name, length))
         { request->a_notified = false; memcpy(request->a_name, event->name, length); request->a_name[length] = 0; }
      }
      return;
   }
   ArcDiscOperation operation;
   if (arc_disc_request_operation(swi, event->r0, &operation))
   {
      /* A new observed-class SWI never revives the previous watch. */
      request->armed = false;
      if (request->a_active || event->mode != 0u ||
          !event->name_readable || !event->name) return;
      char canonical[256];
      if (!arc_disc_request_name(event->name, inserted_name, canonical)) return;
      strcpy(request->object, canonical);
      request->operation = operation;
      request->continuation = event->address + 4u;
      request->sp = event->user_sp;
      request->candidate = event->candidate;
      request->armed = true;
   }
}

ArcDiscResult arc_disc_request_outcome(ArcDiscRequest *request,
   uint32_t address, uint32_t mode, uint32_t sp, uint32_t r0,
   bool overflow, bool error_readable, uint32_t error_number)
{
   ArcDiscResult result = {0};
   if (!request || !request->armed || mode != 0u) return result;
   request->armed = false;
   if (address != request->continuation || sp != request->sp)
   { result.discarded = true; result.reason = "continuation"; return result; }
   bool absent = !overflow ? r0 == 0u && request->operation != ARC_DISC_LOAD :
      (request->operation != ARC_DISC_CATALOGUE && error_readable &&
       error_number == 0xd6u);
   if (!absent)
   {
      /* A found object ends the episode. Other errors do not count. */
      if (!overflow && (request->operation == ARC_DISC_LOAD || r0 != 0u))
         clear_observations(request);
      result.reason = "outcome";
      return result;
   }
   result.needs_resolution = true;
   return result;
}

ArcDiscResult arc_disc_request_resolved(ArcDiscRequest *request,
   unsigned candidate, uint64_t now_us)
{
   ArcDiscResult result = {0};
   if (!request || candidate == UINT_MAX) return result;
   result.observed = true;
   result.candidate = candidate;
   if (request->b_triggered)
   {
      ArcDiscIdentity *triggered = &request->identities[request->triggered_slot];
      if (!triggered->occupied || now_us < triggered->observed_us ||
          now_us - triggered->observed_us > UINT64_C(120000000))
         request->b_triggered = false;
   }
   ArcDiscIdentity *identity = NULL, *free_slot = NULL, *oldest = NULL;
   for (unsigned i = 0u; i < ARC_DISC_IDENTITY_CAPACITY; ++i)
   {
      ArcDiscIdentity *slot = &request->identities[i];
      if (!slot->occupied) { if (!free_slot) free_slot = slot; continue; }
      if (slot->continuation == request->continuation &&
          slot->sp == request->sp && !strcmp(slot->object, request->object))
      { identity = slot; break; }
      if (!oldest || slot->order < oldest->order) oldest = slot;
   }
   bool same = identity && now_us >= identity->observed_us &&
      now_us - identity->observed_us <= UINT64_C(120000000);
   if (identity && !same && request->b_triggered &&
       request->triggered_slot == (unsigned)(identity - request->identities))
      request->b_triggered = false;
   if (!identity)
   {
      identity = free_slot ? free_slot : oldest;
      if (request->b_triggered &&
          request->triggered_slot == (unsigned)(identity - request->identities))
         request->b_triggered = false;
      identity->occupied = true;
      strcpy(identity->object, request->object);
      identity->continuation = request->continuation;
      identity->sp = request->sp;
   }
   identity->observed_us = now_us;
   identity->order = ++request->next_order;
   request->observations = same ? 2u : 1u;
   result.observation = request->observations;
   if (request->observations == 2u && !request->b_triggered)
   {
      request->b_triggered = true;
      request->triggered_slot = (unsigned)(identity - request->identities);
      result.trigger = true;
   }
   return result;
}
ArcDiscResult arc_disc_request_continue(ArcDiscRequest *request,
   uint32_t address, uint32_t mode, uint32_t sp, uint32_t r0,
   bool overflow, bool error_readable, uint32_t error_number,
   uint64_t now_us)
{
   ArcDiscResult result = arc_disc_request_outcome(request, address, mode, sp,
      r0, overflow, error_readable, error_number);
   return result.needs_resolution ?
      arc_disc_request_resolved(request, request->candidate, now_us) : result;
}
void arc_disc_request_retry(ArcDiscRequest *request)
{
   if (!request || !request->b_triggered) return;
   request->identities[request->triggered_slot].occupied = false;
   request->b_triggered = false;
   request->observations = 0u;
}
