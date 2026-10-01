/* Focused error-box invocation and one-shot request tests. GPL-2.0-only. */
#include "arc_error_box.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint32_t site = 0x03801234u;
static void call(ArcErrorBox *box, uint32_t swi, uint32_t r0,
                 uint32_t r1, uint32_t r2, uint32_t mode,
                 bool in_rom, bool valid, unsigned first)
{
   (void)arc_error_swi(box, swi, r0, r1, r2, site, mode, 0x02010000u,
                       0x03805678u, in_rom, valid, (uint8_t)first);
}
static void enter(ArcErrorBox *box, uint32_t swi, unsigned flags)
{ call(box, swi, 0u, flags, 0u, 0u, false, false, 0u); }
static void advance_to(ArcErrorBox *box, ArcErrorStage target)
{
   if (target >= ARC_ERROR_BYTE15)
      call(box, 7u, 21u, 0x02002000u, 0u, 3u, true, true, 1u);
   if (target >= ARC_ERROR_BYTE21)
      call(box, 6u, 15u, 1u, 0u, 3u, true, false, 0u);
   if (target >= ARC_ERROR_POLL)
      call(box, 6u, 21u, 9u, 0u, 3u, true, false, 0u);
   assert(box->stage == target);
}
static void sequence(ArcErrorBox *box)
{
   call(box, 7u, 21u, 0x02002000u, 0u, 3u, true, true, 1u);
   assert(box->stage == ARC_ERROR_BYTE15);
   call(box, 6u, 15u, 1u, 0u, 3u, true, false, 0u);
   assert(box->stage == ARC_ERROR_BYTE21);
   call(box, 0x20006u, 21u, 9u, 0u, 3u, true, false, 0u);
   assert(box->stage == ARC_ERROR_POLL);
   call(box, 6u, 129u, 0u, 0u, 3u, true, false, 0u);
   assert(box->stage == ARC_ERROR_READY && box->site == site);
}
int main(void)
{
   ArcErrorBox box = {.os = ARC_OS_RISCOS_311};
   uint8_t key = 0u;
   for (unsigned os = ARC_OS_RISCOS_311; os <= ARC_OS_RISCOS_319; ++os)
   {
      box.os = (ArcOsClass)os;
      for (unsigned flags = 0u; flags < 32u; ++flags)
      {
         enter(&box, 0x400dfu, flags);
         assert(box.stage == ARC_ERROR_WORD);
         sequence(&box);
         bool ok = (flags & 1u) || !(flags & 3u);
         bool cancel = (flags & 2u) != 0u;
         bool default_cancel = cancel && (!ok || (flags & 4u));
         assert(box.ok == ok && box.cancel == cancel &&
                box.default_cancel == default_cancel);
         assert(!arc_error_consume(&box, site + 4u, 3u, box.sp, box.lr, &key));
         assert(!arc_error_consume(&box, site, 1u, box.sp, box.lr, &key));
         assert(!arc_error_consume(&box, site, 2u, box.sp, box.lr, &key));
         assert(!arc_error_consume(&box, site, 3u, box.sp + 4u, box.lr, &key));
         assert(!arc_error_consume(&box, site, 3u, box.sp, box.lr, &key));
         (void)arc_error_swi(&box,6u,129u,0u,0u,site + 4u,3u,
                             box.sp,box.lr,true,false,0u);
         assert(box.site == site);
         ArcErrorAnswer answer = ok ? ARC_ERROR_OK : ARC_ERROR_CANCEL;
         assert(arc_error_request(&box, answer));
         assert(!arc_error_request(&box, cancel ? ARC_ERROR_CANCEL : ARC_ERROR_OK));
         assert(arc_error_consume(&box, site, 3u, box.sp, box.lr, &key));
         assert(key == (((answer == ARC_ERROR_CANCEL) == default_cancel) ? 13u : 27u));
         assert(box.pending == ARC_ERROR_NONE && box.consumed == answer);
         assert(!arc_error_consume(&box, site, 3u, box.sp, box.lr, &key));
         assert(!arc_error_request(&box, answer));
         arc_error_end(&box);
      }
   }
   box.os = ARC_OS_RISCOS_300;
   assert(!strcmp(arc_error_swi(&box,0x400dfu,0u,3u,0u,site,0u,0u,0u,
                                false,false,0u),"os"));
   box.os = ARC_OS_RISCOS_311;
   assert(!strcmp(arc_error_swi(&box,0x400dfu,0u,3u,0u,site,3u,0u,0u,
                                false,false,0u),"caller-mode"));
   assert(!strcmp(arc_error_swi(&box,0x400dfu,0u,32u,0u,site,0u,0u,0u,
                                false,false,0u),"flags"));
   enter(&box, 0x600dfu, 3u);
   call(&box, 7u, 21u, 0u, 0u, 3u, true, true, 2u);
   assert(box.stage == ARC_ERROR_WORD);
   for (unsigned stage = ARC_ERROR_WORD; stage <= ARC_ERROR_POLL; ++stage)
   {
      enter(&box, 0x400dfu, 3u);
      advance_to(&box, (ArcErrorStage)stage);
      uint64_t generation = box.generation;
      for (unsigned first = 3u; first <= 6u; ++first)
      {
         call(&box, first & 1u ? 0x20007u : 7u, 21u, 0x02002000u,
              0u, 3u, true, true, first);
         assert(box.stage == (ArcErrorStage)stage &&
                box.generation == generation && box.site == 0u);
      }
      if (stage == ARC_ERROR_WORD) sequence(&box);
      else
      {
         if (stage == ARC_ERROR_BYTE15)
            call(&box, 6u, 15u, 1u, 0u, 3u, true, false, 0u);
         if (stage <= ARC_ERROR_BYTE21)
            call(&box, 6u, 21u, 9u, 0u, 3u, true, false, 0u);
         call(&box, 6u, 129u, 0u, 0u, 3u, true, false, 0u);
         assert(box.stage == ARC_ERROR_READY && box.site == site);
      }
      assert(arc_error_request(&box, ARC_ERROR_OK));
      assert(arc_error_consume(&box, site, 3u, box.sp, box.lr, &key));
   }
   for (unsigned stage = ARC_ERROR_BYTE15; stage <= ARC_ERROR_POLL; ++stage)
   {
      enter(&box, 0x400dfu, 3u);
      advance_to(&box, (ArcErrorStage)stage);
      call(&box, 7u, 21u, 0x02002000u, 0u, 3u, true, true, 1u);
      assert(box.stage == ARC_ERROR_OFF); /* unexpected re-bounding */
   }
   for (unsigned stage = ARC_ERROR_WORD; stage <= ARC_ERROR_POLL; ++stage)
   {
      enter(&box, 0x400dfu, 3u);
      advance_to(&box, (ArcErrorStage)stage);
      call(&box, 7u, 21u, 0x02002000u, 0u, 3u, true, false, 4u);
      assert(box.stage == ARC_ERROR_OFF); /* unreadable block */
   }
   enter(&box, 0x400dfu, 3u);
   call(&box, 6u, 15u, 1u, 0u, 3u, true, false, 0u);
   assert(box.stage == ARC_ERROR_OFF);
   enter(&box, 0x400dfu, 3u);
   call(&box, 7u, 21u, 0u, 0u, 3u, true, false, 0u);
   assert(box.stage == ARC_ERROR_OFF);
   enter(&box, 0x400dfu, 3u);
   call(&box, 7u, 21u, 0u, 0u, 3u, true, true, 1u);
   call(&box, 6u, 21u, 9u, 0u, 3u, true, false, 0u);
   assert(box.stage == ARC_ERROR_OFF);
   enter(&box, 0x400dfu, 3u);
   call(&box, 7u, 21u, 0u, 0u, 3u, true, true, 1u);
   call(&box, 6u, 15u, 1u, 0u, 3u, true, false, 0u);
   call(&box, 6u, 21u, 9u, 0u, 3u, true, false, 0u);
   call(&box, 6u, 129u, 0u, 1u, 3u, true, false, 0u);
   assert(box.stage == ARC_ERROR_OFF);
   for (unsigned step = 0u; step < 4u; ++step)
   {
      enter(&box, 0x400dfu, 3u);
      if (step > 0u) call(&box, 7u, 21u, 0u, 0u, 3u, true, true, 1u);
      if (step > 1u) call(&box, 6u, 15u, 1u, 0u, 3u, true, false, 0u);
      if (step > 2u) call(&box, 6u, 21u, 9u, 0u, 3u, true, false, 0u);
      arc_error_end(&box);
      assert(box.stage == ARC_ERROR_OFF);
   }
   enter(&box, 0x400dfu, 3u);
   sequence(&box);
   assert(arc_error_request(&box, ARC_ERROR_OK));
   arc_error_discard(&box);
   assert(box.stage == ARC_ERROR_READY && !box.pending);
   assert(arc_error_request(&box, ARC_ERROR_CANCEL));
   enter(&box, 0x600dfu, 3u);
   assert(box.stage == ARC_ERROR_WORD && !box.pending);
   puts("error_box_test: passed");
   return 0;
}
