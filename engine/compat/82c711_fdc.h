#ifndef ARCDROID_82C711_FDC_STUB_H
#define ARCDROID_82C711_FDC_STUB_H
#include <stdint.h>
uint8_t c82c711_fdc_dmaread(int terminal_count, void *opaque);
void c82c711_fdc_dmawrite(uint8_t value, int terminal_count, void *opaque);
#endif
