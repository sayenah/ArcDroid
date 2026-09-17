#ifndef ARCDROID_82C711_FDC_STUB_H
#define ARCDROID_82C711_FDC_STUB_H
#include <stdint.h>
uint8_t c82c711_fdc_dmaread(int terminal_count, void *opaque);
void c82c711_fdc_dmawrite(uint8_t value, int terminal_count, void *opaque);
void c82c711_fdc_init(void);
void c82c711_fdc_write(uint16_t address, uint8_t value, void *opaque);
uint8_t c82c711_fdc_read(uint16_t address, void *opaque);
void *c82c711_fdc_init_override(void (*irq)(int, void *),
   void (*index_irq)(void *), void (*fiq)(int, void *), void *opaque);
void c82c711_fdc_media_change(unsigned drive);
void c82c711_fdc_shutdown(void);
#endif
