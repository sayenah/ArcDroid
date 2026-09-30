/* Read-only inspection of ArcDroid's owned FileCore directory. GPL-2.0-only. */
#ifndef ARC_HARD_DISC_SCOPE_H
#define ARC_HARD_DISC_SCOPE_H
#include "arcdroid_core.h"
#include "install_disc.h"
typedef bool (*ArcHdRead)(void *, uint32_t, uint32_t, uint8_t *);
typedef enum {
   ARC_HD_INDETERMINATE, ARC_HD_ABSENT, ARC_HD_UNOWNED, ARC_HD_FOREIGN,
   ARC_HD_PENDING, ARC_HD_STARTED, ARC_HD_COMPLETED, ARC_HD_FAILED
} ArcHdScopeState;
typedef struct {
   const char *source;
   const char *media;
   const char *fingerprint;
   const char *content;
   const char *os;
   const ArcGuestName *copy;
} ArcHdExpected;
typedef struct {
   ArcHdScopeState state;
   unsigned attempt;
   unsigned application_count;
   unsigned other_count;
   bool copy_present;
   bool product_present;
   ArcGuestName product;
} ArcHdScope;
ArcHdScope arc_hd_scope_read(ArcHdRead read, void *opaque,
                             const ArcHdExpected *expected);
const char *arc_hd_scope_name(ArcHdScopeState state);
#endif
