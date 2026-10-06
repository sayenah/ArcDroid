/* ArcDroid block storage contract. GPL-2.0-only. */
#ifndef ARC_STORAGE_H
#define ARC_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#define ARC_IDE_SECTOR_SIZE 512u
#define ARC_IDE_MAX_SECTORS 256u
typedef struct {
    void *opaque;
    bool (*read)(void *, uint32_t, uint32_t, uint8_t *);
    bool (*write)(void *, uint32_t, uint32_t, const uint8_t *);
    void (*failed)(void *, const char *);
    uint32_t sectors;
    bool writable;
    void (*unsupported)(void *, uint8_t);
} ArcIdeBackend;

#endif
