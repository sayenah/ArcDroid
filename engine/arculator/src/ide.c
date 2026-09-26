/* Arculator 2.2 by Sarah Walker. GPL-2.0-only.
 * ArcDroid modification (2026-09-25): replaces FILE I/O with a bounded
 * sector backend; latches complete WRITE SECTOR(S) commands before committing;
 * hardens address, data width, IRQ, reset and timer lifetime; and preserves
 * the absent bus exactly when no disc is attached; reports each unsupported
 * ABRT command once per machine session. */
#include "ide.h"
#include <string.h>

#define IDE_ERR 0x01u
#define IDE_DRQ 0x08u
#define IDE_DSC 0x10u
#define IDE_READY 0x40u
#define IDE_BUSY 0x80u
#define IDE_ABRT 0x04u
#define IDE_IDNF 0x10u

ide_t ide_internal;
static ArcIdeBackend selected_backend;
#if defined(ARCDROID_TEST_FAULTS)
static unsigned access_count;
unsigned arc_ide_test_accesses(void) { return access_count; }
#define ACCESS() (++access_count)
#else
#define ACCESS() ((void)0)
#endif

void ide_set_backend(const ArcIdeBackend *backend)
{
    if (backend) selected_backend = *backend;
    else memset(&selected_backend, 0, sizeof(selected_backend));
}

static bool attached(const ide_t *ide)
{
    return ide && ide->backend.sectors && ide->backend.read &&
           !(ide->head_drive & 0x10u);
}

static void update_irq(ide_t *ide)
{
    if (ide->irq_active && ide->irq_enabled) {
        if (ide->irq_raise) ide->irq_raise(ide);
    } else if (ide->irq_clear) ide->irq_clear(ide);
}
static void irq(ide_t *ide, bool active)
{
    ide->irq_active = active;
    update_irq(ide);
}
static void discard(ide_t *ide)
{
    if (ide->timer_registered) timer_disable(&ide->timer);
    ide->active = false;
    ide->write_transfer = false;
    ide->position = ide->transfer_count = ide->transfer_index = 0u;
}
static void fail(ide_t *ide, uint8_t reason)
{
    discard(ide);
    ide->status = IDE_READY | IDE_DSC | IDE_ERR;
    ide->error = reason;
    irq(ide, true);
}
static void unsupported(ide_t *ide)
{
    uint8_t code = ide->command;
    uint8_t mask = (uint8_t)(1u << (code & 7u));
    if (!(ide->unsupported_seen[code >> 3u] & mask)) {
        ide->unsupported_seen[code >> 3u] |= mask;
        if (ide->backend.unsupported)
            ide->backend.unsupported(ide->backend.opaque, code);
    }
}
static void delay(ide_t *ide)
{
    ide->status = IDE_BUSY | IDE_READY | IDE_DSC;
    timer_set_delay_u64(&ide->timer, 1000u * TIMER_USEC);
}
static void advance_address(ide_t *ide)
{
    if (ide->head_drive & 0x40u) {
        uint32_t next = ide->transfer_lba + ide->transfer_index;
        ide->sector = (uint8_t)next;
        ide->cylinder_lo = (uint8_t)(next >> 8u);
        ide->cylinder_hi = (uint8_t)(next >> 16u);
        ide->head_drive = (uint8_t)((ide->head_drive & 0xf0u) |
                                     ((next >> 24u) & 0x0fu));
    } else {
        unsigned sector = (unsigned)ide->sector + 1u;
        unsigned head = ide->head_drive & 15u;
        unsigned cyl = (unsigned)ide->cylinder_lo |
                       ((unsigned)ide->cylinder_hi << 8u);
        if (sector > ide->spt) {
            sector = 1u;
            if (++head >= ide->heads) { head = 0u; ++cyl; }
        }
        ide->sector = (uint8_t)sector;
        ide->head_drive = (uint8_t)((ide->head_drive & 0xf0u) | head);
        ide->cylinder_lo = (uint8_t)cyl;
        ide->cylinder_hi = (uint8_t)(cyl >> 8u);
    }
    ide->count = (uint8_t)(ide->transfer_count - ide->transfer_index);
}
static bool command_address(const ide_t *ide, unsigned count, uint32_t *lba)
{
    if (!attached(ide) || !count || count > ARC_IDE_MAX_SECTORS) return false;
    uint32_t first;
    if (ide->head_drive & 0x40u) {
        first = ((uint32_t)(ide->head_drive & 15u) << 24u) |
                ((uint32_t)ide->cylinder_hi << 16u) |
                ((uint32_t)ide->cylinder_lo << 8u) | ide->sector;
    } else {
        unsigned cyl = (unsigned)ide->cylinder_lo |
                       ((unsigned)ide->cylinder_hi << 8u);
        unsigned head = ide->head_drive & 15u;
        if (!ide->sector || ide->sector > ide->spt ||
            head >= ide->heads || cyl >= ide->cylinders) return false;
        first = ((uint32_t)cyl * ide->heads + head) * ide->spt +
                (ide->sector - 1u);
    }
    if (first >= ide->backend.sectors || count > ide->backend.sectors - first)
        return false;
    *lba = first;
    return true;
}

void closeide(ide_t *ide)
{
    if (!ide) return;
    discard(ide);
    irq(ide, false);
    ide->timer_registered = false;
    memset(&ide->backend, 0, sizeof(ide->backend));
}

void resetide(ide_t *ide, char *primary, int primary_spt, int primary_hpc,
              int primary_cyl, char *secondary, int secondary_spt,
              int secondary_hpc, int secondary_cyl,
              void (*irq_raise)(ide_t *), void (*irq_clear)(ide_t *))
{
    (void)primary; (void)primary_spt; (void)primary_hpc; (void)primary_cyl;
    (void)secondary; (void)secondary_spt; (void)secondary_hpc;
    (void)secondary_cyl;
    closeide(ide);
    memset(ide, 0, sizeof(*ide));
#if defined(ARCDROID_TEST_FAULTS)
    access_count = 0u;
#endif
    ide->backend = selected_backend;
    if (!ide->backend.read || !ide->backend.sectors)
        memset(&ide->backend, 0, sizeof(ide->backend));
    ide->spt = 63u; ide->heads = 16u; ide->cylinders = 32u;
    ide->head_drive = 0xa0u;
    ide->sector = 1u; ide->count = 1u;
    ide->status = IDE_READY | IDE_DSC;
    ide->irq_enabled = true;
    ide->irq_raise = irq_raise;
    ide->irq_clear = irq_clear;
    if (ide->backend.sectors) {
        timer_add(&ide->timer, callbackide, ide, 0);
        ide->timer_registered = true;
    }
}

void resetide_drive(ide_t *ide)
{
    if (!attached(ide)) return;
    discard(ide);
    irq(ide, false);
    ide->reset_pending = true;
    delay(ide);
}

static void sector_read_ready(ide_t *ide)
{
    if (!ide->backend.read(ide->backend.opaque,
                           ide->transfer_lba + ide->transfer_index, 1u,
                           ide->sector_buffer)) {
        fail(ide, IDE_IDNF); return;
    }
    ide->position = 0u;
    ide->status = IDE_READY | IDE_DSC | IDE_DRQ;
    irq(ide, true);
}

void callbackide(void *opaque)
{
    ide_t *ide = opaque;
    if (!attached(ide)) return;
    if (ide->reset_pending) {
        ide->reset_pending = false;
        ide->status = IDE_READY | IDE_DSC;
        ide->error = 0u;
        ide->sector = ide->count = 1u;
        ide->cylinder_lo = ide->cylinder_hi = 0u;
        ide->head_drive &= 0xf0u;
        return;
    }
    if (ide->active) {
        if (ide->write_transfer) {
            if (ide->transfer_index == ide->transfer_count) {
                if (!ide->backend.write || !ide->backend.writable ||
                    ide->read_only_latch ||
                    !ide->backend.write(ide->backend.opaque,
                                        ide->transfer_lba,
                                        ide->transfer_count,
                                        ide->transaction)) {
                    ide->read_only_latch = true;
                    if (ide->backend.failed)
                        ide->backend.failed(ide->backend.opaque,
                                            "write-commit-failed");
                    fail(ide, IDE_ABRT);
                    return;
                }
                ide->active = false;
                ide->status = IDE_READY | IDE_DSC;
            } else {
                ide->position = 0u;
                ide->status = IDE_READY | IDE_DSC | IDE_DRQ;
            }
            irq(ide, true);
        } else sector_read_ready(ide);
        return;
    }
    switch (ide->command) {
    case 0x10: case 0x70: case 0x40: case 0x41:
        ide->status = IDE_READY | IDE_DSC;
        irq(ide, true);
        return;
    case 0x91:
        if (!ide->count || (unsigned)(ide->head_drive & 15u) + 1u > 16u) {
            fail(ide, IDE_ABRT); return;
        }
        ide->spt = ide->count;
        ide->heads = (uint16_t)((ide->head_drive & 15u) + 1u);
        ide->cylinders = (uint16_t)(ide->backend.sectors /
                                    ((uint32_t)ide->spt * ide->heads));
        ide->status = IDE_READY | IDE_DSC;
        irq(ide, true);
        return;
    case 0xec: {
        uint16_t words[256] = {0};
        words[1] = 32u; words[3] = 16u; words[6] = 63u;
        words[10] = 0x3030u;
        for (unsigned i = 11u; i < 20u; ++i) words[i] = 0x2020u;
        for (unsigned i = 23u; i < 47u; ++i) words[i] = 0x2020u;
        words[23] = 0x7632u; words[24] = 0x2e30u;
        words[27] = 0x4172u; words[28] = 0x6375u;
        words[29] = 0x6c61u; words[30] = 0x746fu;
        words[31] = 0x7248u; words[32] = 0x4420u;
        words[50] = 0x4000u; words[53] = 1u;
        words[54] = ide->cylinders; words[55] = ide->heads;
        words[56] = ide->spt;
        words[57] = (uint16_t)ide->backend.sectors;
        words[58] = (uint16_t)(ide->backend.sectors >> 16u);
        for (unsigned i = 0u; i < 256u; ++i) {
            ide->sector_buffer[2u*i] = (uint8_t)words[i];
            ide->sector_buffer[2u*i+1u] = (uint8_t)(words[i] >> 8u);
        }
        ide->position = 0u;
        ide->status = IDE_READY | IDE_DSC | IDE_DRQ;
        irq(ide, true);
        return;
    }
    default: unsupported(ide); fail(ide, IDE_ABRT); return;
    }
}

static uint8_t read_data(ide_t *ide)
{
    if (!(ide->status & IDE_DRQ) || ide->write_transfer ||
        ide->position >= ARC_IDE_SECTOR_SIZE) {
        fail(ide, IDE_ABRT); return 0xffu;
    }
    uint8_t value = ide->sector_buffer[ide->position++];
    if (ide->position == ARC_IDE_SECTOR_SIZE) {
        ide->position = 0u;
        if (ide->active) {
            ++ide->transfer_index;
            advance_address(ide);
            if (ide->transfer_index < ide->transfer_count) delay(ide);
            else { ide->active = false; ide->status = IDE_READY | IDE_DSC; }
        } else ide->status = IDE_READY | IDE_DSC;
    }
    return value;
}
static void write_data(ide_t *ide, uint8_t value)
{
    if (!(ide->status & IDE_DRQ) || !ide->active ||
        !ide->write_transfer || ide->position >= ARC_IDE_SECTOR_SIZE) {
        fail(ide, IDE_ABRT); return;
    }
    ide->transaction[ide->transfer_index * ARC_IDE_SECTOR_SIZE +
                     ide->position++] = value;
    if (ide->position == ARC_IDE_SECTOR_SIZE) {
        ide->position = 0u;
        ++ide->transfer_index;
        advance_address(ide);
        delay(ide);
    }
}

uint16_t readidew(ide_t *ide)
{
    ACCESS();
    if (!attached(ide)) return 0xffffu;
    uint16_t low = read_data(ide);
    if (!(ide->status & IDE_ERR))
        return (uint16_t)(low | ((uint16_t)read_data(ide) << 8u));
    return (uint16_t)(low | 0xff00u);
}
void writeidew(ide_t *ide, uint16_t value)
{
    ACCESS();
    if (!attached(ide)) return;
    write_data(ide, (uint8_t)value);
    if (!(ide->status & IDE_ERR)) write_data(ide, (uint8_t)(value >> 8u));
}
uint8_t readide(ide_t *ide, uint32_t address)
{
    ACCESS();
    if (!attached(ide)) return 0xffu;
    switch (address) {
    case 0x1f0: return read_data(ide);
    case 0x1f1: return ide->error;
    case 0x1f2: return ide->count;
    case 0x1f3: return ide->sector;
    case 0x1f4: return ide->cylinder_lo;
    case 0x1f5: return ide->cylinder_hi;
    case 0x1f6: return ide->head_drive;
    case 0x1f7: irq(ide, false); return ide->status;
    case 0x3f6: return ide->status;
    default: return 0xffu;
    }
}
void writeide(ide_t *ide, uint32_t address, uint8_t value)
{
    ACCESS();
    if (!ide || !ide->backend.sectors || !ide->backend.read) return;
    switch (address) {
    case 0x1f0: if (attached(ide)) write_data(ide, value); return;
    case 0x1f1: ide->features = value; return;
    case 0x1f2: ide->count = value; return;
    case 0x1f3: ide->sector = value; return;
    case 0x1f4: ide->cylinder_lo = value; return;
    case 0x1f5: ide->cylinder_hi = value; return;
    case 0x1f6:
        ide->head_drive = value;
        if (value & 0x10u) irq(ide, false);
        return;
    case 0x3f6:
        if (value & 4u) {
            discard(ide);
            ide->reset_pending = false;
            ide->status = IDE_BUSY | IDE_DSC;
            irq(ide, false);
        } else if (ide->device_control & 4u) resetide_drive(ide);
        ide->device_control = value;
        ide->irq_enabled = !(value & 2u);
        update_irq(ide);
        return;
    case 0x1f7: {
        discard(ide);
        ide->command = value;
        ide->error = 0u;
        irq(ide, false);
        if (!attached(ide)) { fail(ide, IDE_ABRT); return; }
        unsigned count = ide->count ? ide->count : 256u;
        uint32_t lba;
        switch (value) {
        case 0x20: case 0x21: case 0x30: case 0x31:
            if (!command_address(ide, count, &lba)) {
                fail(ide, IDE_IDNF); return;
            }
            if (value == 0x30 || value == 0x31) {
                if (!ide->backend.writable || ide->read_only_latch) {
                    fail(ide, IDE_ABRT); return;
                }
            }
            ide->transfer_lba = lba;
            ide->transfer_count = count;
            ide->transfer_index = ide->position = 0u;
            ide->active = true;
            ide->write_transfer = value == 0x30 || value == 0x31;
            if (ide->write_transfer)
                ide->status = IDE_READY | IDE_DSC | IDE_DRQ;
            else delay(ide);
            return;
        case 0x10: case 0x70: case 0x40: case 0x41:
        case 0x91: case 0xec: delay(ide); return;
        default: unsupported(ide); fail(ide, IDE_ABRT); return;
        }
    }
    default: return;
    }
}
