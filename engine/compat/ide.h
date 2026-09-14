#ifndef ARCDROID_IDE_STUB_H
#define ARCDROID_IDE_STUB_H
#include <stdint.h>
typedef struct { int unused; } ide_t;
extern ide_t ide_internal;
uint16_t readidew(ide_t *ide);
void writeidew(ide_t *ide, uint16_t value);
#endif
