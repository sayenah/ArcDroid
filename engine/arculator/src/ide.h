/* Arculator 2.2 by Sarah Walker. GPL-2.0-only.
 * ArcDroid modification (2026-09-25): bounded block-device interface,
 * transaction buffer and lifecycle state replace the host FILE handles. */
#ifndef ARCDROID_IDE_H
#define ARCDROID_IDE_H
#include <stdbool.h>
#include <stdint.h>
#include "timer.h"
#define ARC_IDE_SECTOR_SIZE 512u
#define ARC_IDE_MAX_SECTORS 256u
typedef struct {
    void *opaque;
    bool (*read)(void *, uint32_t, uint32_t, uint8_t *);
    bool (*write)(void *, uint32_t, uint32_t, const uint8_t *);
    void (*failed)(void *, const char *);
    uint32_t sectors;
    bool writable;
} ArcIdeBackend;
typedef struct ide_t {
    uint8_t status, error, features, count, sector, cylinder_lo, cylinder_hi;
    uint8_t head_drive, device_control, command;
    unsigned position, transfer_index, transfer_count;
    uint32_t transfer_lba;
    uint16_t spt, heads, cylinders;
    bool active, write_transfer, read_only_latch, reset_pending, timer_registered;
    bool irq_active, irq_enabled;
    uint8_t sector_buffer[ARC_IDE_SECTOR_SIZE];
    uint8_t transaction[ARC_IDE_SECTOR_SIZE * ARC_IDE_MAX_SECTORS];
    ArcIdeBackend backend;
    emu_timer_t timer;
    void (*irq_raise)(struct ide_t *);
    void (*irq_clear)(struct ide_t *);
} ide_t;

extern ide_t ide_internal;
void ide_set_backend(const ArcIdeBackend *backend);

uint16_t readidew(ide_t *ide);
void writeidew(ide_t *ide, uint16_t val);
uint8_t readide(ide_t *ide, uint32_t addr);
void writeide(ide_t *ide, uint32_t addr, uint8_t val);
void callbackide(void *p);

void resetide(ide_t *ide,
		char *fn_pri, int pri_spt, int pri_hpc, int pri_cyl,
		char *fn_sec, int sec_spt, int sec_hpc, int sec_cyl,
		void (*irq_raise)(ide_t *ide), void (*irq_clear)(ide_t *ide));
void resetide_drive(ide_t *ide);
void closeide(ide_t *ide);
#if defined(ARCDROID_TEST_FAULTS)
unsigned arc_ide_test_accesses(void);
#endif
#endif
