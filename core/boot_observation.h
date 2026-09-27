#ifndef ARCDROID_BOOT_OBSERVATION_H
#define ARCDROID_BOOT_OBSERVATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Change this when boot observation or launch preparation semantics change. */
#define ARC_BOOT_OBSERVATION_REVISION 1u
#define ARC_BOOT_OBSERVATION_SIZE 160u

typedef struct {
   char firmware[41];
   char media[41];
   char cmos[41];
   uint32_t machine;
   uint32_t cpu;
   uint32_t memory;
   uint32_t jfd_boot;
   uint32_t disk_writes;
   uint32_t install_saves;
} ArcBootObservation;

bool arc_boot_observation_format(const ArcBootObservation *stamp,
                                 uint8_t *out, size_t capacity);
bool arc_boot_observation_parse(const uint8_t *data, size_t size,
                                ArcBootObservation *stamp);
bool arc_boot_observation_equal(const ArcBootObservation *a,
                                const ArcBootObservation *b);

#endif
