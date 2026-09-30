/* Project-authored BASIC installation programs. GPL-2.0-only. */
#ifndef ARC_HD_PROGRAM_H
#define ARC_HD_PROGRAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum { ARC_HD_HELPER_FLOPPY, ARC_HD_HELPER_PRODUCT,
               ARC_HD_HELPER_INSTALL, ARC_HD_HELPER_MARK } ArcHdHelperMode;
typedef struct {
   const char *source; /* ADFS::0.$.[parent.]<target> */
   const char *target; /* Application leaf, including ! */
   const char *product; /* Product leaf for mark/product helpers. */
   const char *media;
   const char *fingerprint;
   const char *content;
   const char *os;
   unsigned attempt;
} ArcHdProgramSpec;
bool arc_hd_program_build(ArcHdHelperMode mode,
                           const ArcHdProgramSpec *spec,
                           uint8_t *out, size_t capacity, size_t *size);
#endif
