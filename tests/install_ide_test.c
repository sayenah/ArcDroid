/* Register-level tests for ArcDroid's bounded IDE bus. GPL-2.0-only. */
#include "ide.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t tsc, TIMER_USEC = 1u;
uint32_t timer_target;
void timer_enable(emu_timer_t *t) { t->enabled = 1; }
void timer_disable(emu_timer_t *t) { t->enabled = 0; }
void timer_add(emu_timer_t *t, void (*callback)(void *), void *p, int start)
{ t->callback = callback; t->p = p; t->enabled = start; }

static uint8_t *disk;
static unsigned writes, raises, clears, failed;
static unsigned unsupported_count[256];
static uint32_t written_lba, written_count;
static bool reject_write;
static bool read_block(void *p, uint32_t lba, uint32_t count, uint8_t *out)
{ (void)p; memcpy(out, disk + (size_t)lba * 512u, (size_t)count * 512u); return true; }
static bool write_block(void *p, uint32_t lba, uint32_t count, const uint8_t *in)
{
  (void)p;
  if (reject_write) return false;
  ++writes; written_lba = lba; written_count = count;
  memcpy(disk + (size_t)lba * 512u, in, (size_t)count * 512u);
  return true;
}
static void write_failed(void *p, const char *reason)
{ (void)p; assert(reason && *reason); ++failed; }
static void unsupported_command(void *p, uint8_t command)
{ (void)p; ++unsupported_count[command]; }
static void raise_irq(ide_t *ide) { (void)ide; ++raises; }
static void clear_irq(ide_t *ide) { (void)ide; ++clears; }
static void tick(ide_t *ide)
{ assert(ide->timer.enabled); timer_disable(&ide->timer); callbackide(ide); }
static void at_lba(ide_t *ide, uint32_t lba, unsigned count)
{
  writeide(ide, 0x1f2u, (uint8_t)count);
  writeide(ide, 0x1f3u, (uint8_t)lba);
  writeide(ide, 0x1f4u, (uint8_t)(lba >> 8u));
  writeide(ide, 0x1f5u, (uint8_t)(lba >> 16u));
  writeide(ide, 0x1f6u, (uint8_t)(0xe0u | (lba >> 24u)));
}
static void feed_sector(ide_t *ide, uint8_t value)
{
  assert(readide(ide, 0x3f6u) & 8u);
  for (unsigned i = 0; i < 256u; ++i) writeidew(ide, value | (uint16_t)value << 8u);
}
static void start(ide_t *ide, uint32_t lba, unsigned count)
{ at_lba(ide, lba, count); writeide(ide, 0x1f7u, 0x30u); assert(readide(ide, 0x3f6u) & 8u); }
int main(void)
{
  disk = calloc(32256u, 512u);
  assert(disk);
  ide_t ide = {0};
  assert(readide(&ide, 0x1f7u) == 0xffu && readidew(&ide) == 0xffffu);
  writeide(&ide, 0x1f7u, 0x30u);
  assert(!ide.timer_registered);
  ArcIdeBackend b = {NULL, read_block, write_block, write_failed, 32256u, true,
                     unsupported_command};
  ide_set_backend(&b);
  resetide(&ide, NULL, 0, 0, 0, NULL, 0, 0, 0, raise_irq, clear_irq);
  assert(ide.timer_registered);
  writeide(&ide, 0x1f6u, 0xb0u);
  writeide(&ide, 0x3f6u, 4u);
  writeide(&ide, 0x3f6u, 0u);
  tick(&ide);
  writeide(&ide, 0x1f6u, 0xa0u);
  assert(readide(&ide, 0x3f6u) == 0x50u && !ide.timer.enabled);
  writeide(&ide, 0x1f6u, 0xf0u);
  assert(readide(&ide, 0x1f7u) == 0xffu && readidew(&ide) == 0xffffu);
  writeide(&ide, 0x1f7u, 0x30u);
  assert(writes == 0u);
  start(&ide, 7u, 2u);
  feed_sector(&ide, 0x11u);
  assert(writes == 0u);
  /* The command's LBA/count stay latched while the guest changes registers. */
  at_lba(&ide, 100u, 1u);
  tick(&ide);
  feed_sector(&ide, 0x22u);
  assert(writes == 0u);
  tick(&ide);
  assert(writes == 1u && written_lba == 7u && written_count == 2u);
  assert(disk[7u * 512u] == 0x11u && disk[8u * 512u] == 0x22u);
  assert(!(readide(&ide, 0x1f7u) & 1u) && raises && clears);
  start(&ide, 9u, 1u);
  writeide(&ide, 0x1f0u, 0xabu);
  for (unsigned i = 0u; i < 255u; ++i) writeidew(&ide, 0xcddcu);
  writeide(&ide, 0x1f0u, 0xefu);
  tick(&ide);
  assert(writes == 2u && disk[9u * 512u] == 0xabu &&
         disk[9u * 512u + 1u] == 0xdcu &&
         disk[9u * 512u + 511u] == 0xefu);
  at_lba(&ide, 9u, 1u);
  writeide(&ide, 0x1f7u, 0x20u); tick(&ide);
  assert(readide(&ide, 0x1f0u) == 0xabu);
  assert(readidew(&ide) == 0xcddcu);
  for (unsigned i = 0u; i < 254u; ++i) (void)readidew(&ide);
  assert(readide(&ide, 0x1f0u) == 0xefu);
  for (unsigned completed = 0u; completed <= 2u; ++completed) {
    start(&ide, 12u, 3u);
    for (unsigned i = 0u; i < completed; ++i) {
      feed_sector(&ide, 0x55u); tick(&ide);
    }
    writeide(&ide, 0x3f6u, 4u);
    writeide(&ide, 0x3f6u, 0u);
    tick(&ide);
    assert(writes == 2u && disk[12u * 512u] == 0u);
  }
  start(&ide, 20u, 2u);
  feed_sector(&ide, 0x44u);
  writeide(&ide, 0x1f7u, 0x50u);
  assert(readide(&ide, 0x1f7u) & 1u);
  assert(readide(&ide, 0x1f1u) == 4u && writes == 2u);
  writeide(&ide, 0x1f7u, 0x50u);
  assert(readide(&ide, 0x1f1u) == 4u && unsupported_count[0x50] == 1u);
  at_lba(&ide, 21u, 1u);
  writeide(&ide, 0x1f7u, 0x99u);
  assert(readide(&ide, 0x1f1u) == 4u);
  assert(unsupported_count[0x99] == 1u);
  const uint8_t probe_commands[] = {0xa1u, 0xe3u, 0xe5u};
  for (unsigned i=0u; i<sizeof(probe_commands); ++i) {
    unsigned code=probe_commands[i];
    writeide(&ide, 0x1f7u, (uint8_t)code);
    assert(readide(&ide, 0x1f1u) == 4u);
    writeide(&ide, 0x1f7u, (uint8_t)code);
    assert(unsupported_count[code] == 1u);
  }
  /* The zero count encodes 256 sectors. Abort before commit on reset. */
  start(&ide, 100u, 0u);
  assert(ide.transfer_count == 256u);
  feed_sector(&ide, 0x33u);
  writeide(&ide, 0x3f6u, 4u);
  writeide(&ide, 0x3f6u, 0u); tick(&ide);
  assert(writes == 2u);
  start(&ide, 30u, 1u);
  reject_write = true;
  feed_sector(&ide, 0x77u); tick(&ide);
  assert(writes == 2u && failed == 1u && ide.read_only_latch);
  assert((readide(&ide, 0x1f7u) & 1u) && readide(&ide, 0x1f1u) == 4u);
  at_lba(&ide, 31u, 1u);
  writeide(&ide, 0x1f7u, 0x30u);
  assert(readide(&ide, 0x1f1u) == 4u);
  closeide(&ide);
  ide_set_backend(NULL);
  resetide(&ide, NULL, 0, 0, 0, NULL, 0, 0, 0, raise_irq, clear_irq);
  assert(!ide.timer_registered && readide(&ide, 0x1f7u) == 0xffu);
  assert(unsupported_count[0x50] == 1u);
  closeide(&ide);
  free(disk);
  puts("install IDE: pass");
  return 0;
}
