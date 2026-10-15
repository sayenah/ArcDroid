/* RISC OS 3.1 Wimp error-box invocation observer. GPL-2.0-only. */
#ifndef ARC_ERROR_BOX_H
#define ARC_ERROR_BOX_H
#include "arcdroid_core.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
   ARC_ERROR_OFF, ARC_ERROR_WORD, ARC_ERROR_BYTE15,
   ARC_ERROR_BYTE21, ARC_ERROR_POLL, ARC_ERROR_READY
} ArcErrorStage;
typedef enum { ARC_ERROR_NONE, ARC_ERROR_OK, ARC_ERROR_CANCEL } ArcErrorAnswer;
typedef enum { ARC_ERROR_NUMBER_ZERO, ARC_ERROR_NUMBER_ORDINARY } ArcErrorNumberClass;
typedef struct {
   bool contextual;
   uint32_t poll_r1, poll_r2, poll_modes;
   uint32_t answer_forms; /* one bit for each enumerated old-style form */
   uint32_t ok_forms, cancel_forms, default_cancel_forms;
   ArcErrorNumberClass numbers;
} ArcErrorProfile;
typedef struct {
   uint64_t generation;
   ArcOsClass os;
   ArcErrorStage stage;
   bool ok, cancel, default_cancel;
   uint32_t site, sp, lr;
   ArcErrorAnswer pending, consumed;
   uint8_t key;
   const ArcErrorProfile *profile;
   bool recognised_open, answer_available, modal_unknown;
   uint32_t flags, poll_mode;
} ArcErrorBox;

/* The caller reports a bounded first byte for OS_Word 21. No guest pointer is
 * dereferenced here. Other ROM SVC SWIs and IRQ/FIQ activity may intervene
 * between ordered steps. Readable non-bounding OS_Word 21 calls from ROM SVC
 * are neutral at every pre-ready stage; byte 1 advances only from WORD, and
 * an unreadable block or later byte 1 ends the generation. An invalid ordered
 * step, non-ROM SVC SWI, nested report, first return to user mode, reset,
 * teardown, or fault ends this
 * generation. The first eligible read only records the site; a later read
 * from the same site and SVC stack may consume one pending request. */
const char *arc_error_swi(ArcErrorBox *box, uint32_t swi, uint32_t r0,
                          uint32_t r1, uint32_t r2, uint32_t address,
                          uint32_t mode, uint32_t sp, uint32_t lr,
                          bool in_rom, bool block_valid, uint8_t block_first);
void arc_error_end(ArcErrorBox *box);
bool arc_error_poll(ArcErrorBox *, uint32_t r1, uint32_t r2,
   uint32_t address, uint32_t mode, uint32_t sp, bool in_rom, uint8_t *key);
bool arc_error_poll_matches(const ArcErrorBox *, uint32_t r1, uint32_t r2,
   uint32_t address, uint32_t mode, uint32_t sp, bool in_rom);
bool arc_error_number_answerable(const ArcErrorProfile *, uint32_t number);
bool arc_error_request(ArcErrorBox *box, ArcErrorAnswer answer);
bool arc_error_consume(ArcErrorBox *box, uint32_t address, uint32_t mode,
                       uint32_t sp, uint32_t lr, uint8_t *key);
void arc_error_discard(ArcErrorBox *box);
#endif
