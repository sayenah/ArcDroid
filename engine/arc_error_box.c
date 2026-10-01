/* RISC OS 3.1 Wimp error-box invocation observer. GPL-2.0-only. */
#include "arc_error_box.h"

void arc_error_end(ArcErrorBox *box)
{
   box->stage = ARC_ERROR_OFF;
   box->site = box->sp = box->lr = 0u;
   box->pending = box->consumed = ARC_ERROR_NONE;
   box->key = 0u;
}

void arc_error_discard(ArcErrorBox *box)
{ box->pending = ARC_ERROR_NONE; box->key = 0u; }

const char *arc_error_swi(ArcErrorBox *box, uint32_t swi, uint32_t r0,
                          uint32_t r1, uint32_t r2, uint32_t address,
                          uint32_t mode, uint32_t sp, uint32_t lr,
                          bool in_rom, bool block_valid, uint8_t block_first)
{
   /* Compare complete normalised SWI numbers. Only the two specified X/non-X
    * forms of ReportError and the two forms of each kernel SWI are accepted. */
   if (swi == 0x400dfu || swi == 0x600dfu)
   {
      arc_error_end(box);
      if (box->os != ARC_OS_RISCOS_310 && box->os != ARC_OS_RISCOS_311 &&
          box->os != ARC_OS_RISCOS_319) return "os";
      if (mode != 0u) return "caller-mode";
      if (r1 & ~31u) return "flags";
      ++box->generation;
      box->ok = (r1 & 1u) != 0u || (r1 & 3u) == 0u;
      box->cancel = (r1 & 2u) != 0u;
      box->default_cancel = box->cancel && (!box->ok || (r1 & 4u) != 0u);
      box->stage = ARC_ERROR_WORD;
      return NULL;
   }
   if (box->stage == ARC_ERROR_OFF || box->stage == ARC_ERROR_READY)
      return NULL;
   if (mode == 1u || mode == 2u) return NULL; /* interrupt activity */
   if (mode != 3u || !in_rom) { arc_error_end(box); return NULL; }
   if (swi == 7u || swi == 0x20007u)
   {
      if (r0 == 21u)
      {
         if (box->stage != ARC_ERROR_WORD || !block_valid || block_first != 1u)
            arc_error_end(box);
         else box->stage = ARC_ERROR_BYTE15;
      }
   }
   else if (swi == 6u || swi == 0x20006u)
   {
      if (r0 == 15u)
      {
         if (box->stage != ARC_ERROR_BYTE15 || r1 != 1u)
            arc_error_end(box);
         else box->stage = ARC_ERROR_BYTE21;
      }
      else if (r0 == 21u)
      {
         if (box->stage != ARC_ERROR_BYTE21 || r1 != 9u)
            arc_error_end(box);
         else box->stage = ARC_ERROR_POLL;
      }
      else if (r0 == 129u)
      {
         if (box->stage != ARC_ERROR_POLL || r1 || r2)
            arc_error_end(box);
         else
         {
            box->site = address;
            box->sp = sp;
            /* LR is retained for diagnostics only: each nested SWI changes
             * it before the next poll. SP identifies this SVC stack. */
            box->lr = lr;
            box->stage = ARC_ERROR_READY;
         }
      }
   }
   return NULL;
}

bool arc_error_request(ArcErrorBox *box, ArcErrorAnswer answer)
{
   if (box->stage != ARC_ERROR_READY || box->pending != ARC_ERROR_NONE ||
       box->consumed != ARC_ERROR_NONE ||
       (answer != ARC_ERROR_OK && answer != ARC_ERROR_CANCEL) ||
       (answer == ARC_ERROR_OK && !box->ok) ||
       (answer == ARC_ERROR_CANCEL && !box->cancel)) return false;
   box->pending = answer;
   box->key = (answer == ARC_ERROR_CANCEL) == box->default_cancel ? 13u : 27u;
   return true;
}

bool arc_error_consume(ArcErrorBox *box, uint32_t address, uint32_t mode,
                       uint32_t sp, uint32_t lr, uint8_t *key)
{
   if (box->stage != ARC_ERROR_READY || box->pending == ARC_ERROR_NONE ||
       mode != 3u || address != box->site || sp != box->sp)
      return false;
   (void)lr;
   *key = box->key;
   box->consumed = box->pending;
   arc_error_discard(box);
   return true;
}
