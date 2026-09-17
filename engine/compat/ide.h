#ifndef ARCDROID_IDE_STUB_H
#define ARCDROID_IDE_STUB_H
#include <stdint.h>
typedef struct { int unused; } ide_t;
extern ide_t ide_internal;
uint16_t readidew(ide_t *ide);
void writeidew(ide_t *ide, uint16_t value);
uint8_t readide(ide_t *ide, uint32_t address);
void writeide(ide_t *ide, uint32_t address, uint8_t value);
void resetide(ide_t *ide, char *primary, int primary_spt, int primary_hpc,
              int primary_cyl, char *secondary, int secondary_spt,
              int secondary_hpc, int secondary_cyl,
              void (*irq_raise)(ide_t *), void (*irq_clear)(ide_t *));
#endif
