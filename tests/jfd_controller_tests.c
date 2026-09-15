/* ArcDroid WD1770/JFD controller golden tests. GPL-2.0-only. */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arc_io.h"
#include "config.h"
#include "disc.h"
#include "ioc.h"
#include "jfd.h"
#include "timer.h"
#include "wd1770.h"

IOC_t ioc;
int speed_mhz = 1;
int romset = ROM_RISCOS_311;
int ins;
static unsigned irq_assertions;
static unsigned drq_assertions;
static uint8_t transfer[2048];
static size_t transfer_count;

void rpclog(const char *format, ...) { (void)format; }
void error(const char *format, ...) { (void)format; abort(); }
void fatal(const char *format, ...) { (void)format; abort(); }
void arc_print_error(const char *format, ...) { (void)format; abort(); }
void ioc_updateirqs(void) {}
void ioc_discchange(int drive) { (void)drive; }
void ioc_discchange_clear(int drive) { (void)drive; }
void ioc_fiq(uint8_t value)
{
   ioc.fiq |= value;
   if (value & IOC_FIQ_DISC_IRQ) ++irq_assertions;
   if (value & IOC_FIQ_DISC_DATA) ++drq_assertions;
}
void ioc_fiqc(uint8_t value) { ioc.fiq &= (uint8_t)~value; }

/* Raw ADF is linked into this test to keep the real drive/controller boundary
 * intact. It is not opened by these JFD cases, so a deliberately inert VFS
 * stub is sufficient. */
struct ArcIoFile { int unused; };
ArcIoFile *arc_io_open_file(const char *path, bool writable)
{ (void)path; (void)writable; return NULL; }
bool arc_io_close_file(ArcIoFile *file) { (void)file; return false; }
int64_t arc_io_file_length(ArcIoFile *file) { (void)file; return -1; }
int64_t arc_io_seek_file(ArcIoFile *file, int64_t offset, int origin)
{ (void)file; (void)offset; (void)origin; return -1; }
size_t arc_io_read_file(ArcIoFile *file, void *data, size_t size)
{ (void)file; (void)data; (void)size; return 0u; }
size_t arc_io_write_file(ArcIoFile *file, const void *data, size_t size)
{ (void)file; (void)data; (void)size; return 0u; }
bool arc_io_flush_file(ArcIoFile *file) { (void)file; return false; }

static uint8_t *read_all(const char *path, size_t *size)
{
   FILE *file = fopen(path, "rb");
   assert(file && fseek(file, 0, SEEK_END) == 0);
   long length = ftell(file);
   assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
   uint8_t *data = malloc((size_t)length ? (size_t)length : 1u);
   assert(data && fread(data, 1u, (size_t)length, file) == (size_t)length);
   assert(fclose(file) == 0);
   *size = (size_t)length;
   return data;
}

static void fixture_path(char path[4096], const char *root, const char *name)
{
   int written = snprintf(path, 4096u, "%s/jfd-family/%s", root, name);
   assert(written > 0 && written < 4096);
}

static ArcJfdImage load_image(const char *root, const char *name)
{
   char path[4096], reason[256];
   size_t size;
   fixture_path(path, root, name);
   uint8_t *data = read_all(path, &size);
   ArcJfdImage image;
   bool accepted = arc_jfd_parse_owned(data, size, &image, reason, sizeof(reason));
   if (!accepted) fprintf(stderr, "%s: %s\n", name, reason);
   assert(accepted);
   return image;
}

static void controller_reset(const ArcJfdImage *image)
{
   memset(&ioc, 0, sizeof(ioc));
   irq_assertions = drq_assertions = 0u;
   transfer_count = 0u;
   tsc = 0u;
   timer_reset();
   disc_init();
   fdctype = FDC_WD1770;
   wd1770_reset();
   disc_reset();
   if (image) assert(disc_load_jfd(0, image));
   wd1770_writelatch_a(0x9eu); /* drive 0, side 0, motor on */
   wd1770_writelatch_b(0x00u); /* double density */
}

static void step_controller(void)
{
   assert(timer_target != UINT32_MAX);
   tsc = (uint64_t)timer_target << 32u;
   timer_process();
   if (ioc.fiq & IOC_FIQ_DISC_DATA)
   {
      assert(transfer_count < sizeof(transfer));
      transfer[transfer_count++] = wd1770_read(0x0cu);
   }
}

static void run_us(unsigned microseconds)
{
   uint64_t end = tsc + (uint64_t)microseconds * TIMER_USEC;
   while (tsc < end) step_controller();
}

static uint8_t finish_command(uint8_t command, unsigned limit_us)
{
   transfer_count = 0u;
   memset(transfer, 0, sizeof(transfer));
   ioc.fiq = 0u;
   wd1770_write(0x00u, command);
   uint64_t end = tsc + (uint64_t)limit_us * TIMER_USEC;
   while (!(ioc.fiq & IOC_FIQ_DISC_IRQ) && tsc < end) step_controller();
   assert(ioc.fiq & IOC_FIQ_DISC_IRQ);
   uint8_t status = wd1770_read(0x00u);
   unsigned completions = irq_assertions;
   run_us(210000u);
   assert(!(ioc.fiq & IOC_FIQ_DISC_IRQ) && irq_assertions == completions);
   return status;
}

static void expect_read(const char *root, const char *name, uint8_t sector,
                        uint8_t status, size_t bytes, uint8_t first)
{
   ArcJfdImage image = load_image(root, name);
   controller_reset(&image);
   wd1770_write(0x08u, sector);
   assert(finish_command(0x80u, 1100000u) == status);
   if (transfer_count != bytes)
      fprintf(stderr, "%s: received %zu bytes, expected %zu\n",
              name, transfer_count, bytes);
   assert(transfer_count == bytes);
   if (bytes) assert(transfer[0] == first);
   disc_close(0);
   arc_jfd_free(&image);
}

static void select_density(bool single_density)
{
   wd1770_writelatch_b(single_density ? 0x02u : 0x00u);
}

static void expect_address(const char *root, const char *name,
                           bool single_density, uint8_t status, size_t bytes)
{
   ArcJfdImage image = load_image(root, name);
   controller_reset(&image);
   select_density(single_density);
   assert(finish_command(0xc0u, 1100000u) == status);
   assert(transfer_count == bytes);
   if (bytes) assert(transfer[0] == 0u);
   disc_close(0);
   arc_jfd_free(&image);
}

static void expect_write_protected(const char *root, const char *name,
                                   bool single_density)
{
   ArcJfdImage image = load_image(root, name);
   controller_reset(&image);
   select_density(single_density);
   wd1770_write(0x08u, 0u);
   unsigned before = drq_assertions;
   assert(finish_command(0xa0u, 1000u) == 0x40u);
   assert(transfer_count == 0u && drq_assertions == before);
   before = drq_assertions;
   assert(finish_command(0xf0u, 1000u) == 0x40u);
   assert(transfer_count == 0u && drq_assertions == before);
   disc_close(0);
   arc_jfd_free(&image);
}

static void expect_missing_id(const char *root, const char *name,
                              bool single_density)
{
   ArcJfdImage image = load_image(root, name);
   controller_reset(&image);
   select_density(single_density);
   wd1770_write(0x08u, 0xfeu);
   assert(finish_command(0x80u, 1100000u) == 0x10u);
   assert(transfer_count == 0u);
   disc_close(0);
   arc_jfd_free(&image);
}

static void expect_common_paths(const char *root, const char *name,
                                bool single_density, uint8_t address_status,
                                size_t address_bytes)
{
   expect_address(root, name, single_density, address_status, address_bytes);
   expect_write_protected(root, name, single_density);
   expect_missing_id(root, name, single_density);
}

static void test_family(const char *root)
{
   for (unsigned code = 0u; code < 4u; ++code)
   {
      char name[32];
      snprintf(name, sizeof(name), "size-%u.jfd", code);
      expect_read(root, name, (uint8_t)code, 0x80u,
                  (size_t)128u << code, (uint8_t)code);
      expect_common_paths(root, name, false, 0x80u, 6u);
   }
   expect_read(root, "data-crc.jfd", 4u, 0x08u, 128u, 0x99u);
   expect_common_paths(root, "data-crc.jfd", false, 0x80u, 6u);
   expect_read(root, "id-crc.jfd", 4u, 0x18u, 0u, 0u);
   expect_common_paths(root, "id-crc.jfd", false, 0x18u, 0u);
   expect_read(root, "both-crc.jfd", 4u, 0x18u, 0u, 0u);
   expect_common_paths(root, "both-crc.jfd", false, 0x18u, 0u);
   expect_read(root, "id-only.jfd", 4u, 0x10u, 0u, 0u);
   expect_common_paths(root, "id-only.jfd", false, 0x80u, 6u);
   expect_read(root, "data-crc-id-only.jfd", 4u, 0x10u, 0u, 0u);
   expect_common_paths(root, "data-crc-id-only.jfd", false, 0x80u, 6u);
   expect_read(root, "blank-track.jfd", 4u, 0x10u, 0u, 0u);
   expect_common_paths(root, "blank-track.jfd", false, 0x10u, 0u);
   expect_read(root, "timed-duplicates.jfd", 7u, 0x80u, 256u, 0x11u);
   expect_common_paths(root, "timed-duplicates.jfd", false, 0x80u, 6u);
   expect_read(root, "wrap-equal-times.jfd", 1u, 0x80u, 128u, 0x33u);
   expect_common_paths(root, "wrap-equal-times.jfd", false, 0x80u, 6u);
   expect_read(root, "untimed.jfd", 1u, 0x80u, 128u, 0x66u);
   expect_common_paths(root, "untimed.jfd", false, 0x80u, 6u);

   ArcJfdImage single = load_image(root, "single-density.jfd");
   controller_reset(&single);
   wd1770_write(0x08u, 0u);
   assert(finish_command(0x80u, 1100000u) == 0x10u);
   assert(transfer_count == 0u);
   disc_close(0);
   arc_jfd_free(&single);
   expect_common_paths(root, "single-density.jfd", true, 0x80u, 6u);
}

static void test_rejections(const char *root)
{
   static const struct { const char *name; const char *reason; } cases[] = {
      {"reject-discop3.jfd", "DiscOp 3"},
      {"reject-density4.jfd", "quad/octal"},
      {"reject-mixed-time.jfd", "mixes timed"},
      {"reject-untimed-duplicate.jfd", "duplicate"},
      {"reject-long-schedule.jfd", "one revolution"},
      {"reject-delta.jfd", "delta"},
   };
   for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i)
   {
      char path[4096], reason[256];
      size_t size;
      fixture_path(path, root, cases[i].name);
      uint8_t *data = read_all(path, &size);
      ArcJfdImage image;
      assert(!arc_jfd_parse_owned(data, size, &image, reason, sizeof(reason)));
      assert(strstr(reason, cases[i].reason));
   }
}

static void test_empty_drive_cancellation(void)
{
   controller_reset(NULL);
   wd1770_write(0x08u, 0u);
   wd1770_write(0x00u, 0x80u);
   assert(wd1770_read(0x00u) == 0x81u);
   unsigned completions = irq_assertions;
   wd1770_write(0x00u, 0x00u);
   assert(wd1770_read(0x00u) == 0x81u);
   assert(!(ioc.fiq & IOC_FIQ_DISC_IRQ));
   run_us(1000000u);
   assert((ioc.fiq & IOC_FIQ_DISC_IRQ) && irq_assertions == completions + 1u);
   assert(wd1770_read(0x00u) == 0x10u);
   completions = irq_assertions;
   run_us(210000u);
   assert(!(ioc.fiq & IOC_FIQ_DISC_IRQ) && irq_assertions == completions);

   wd1770_write(0x00u, 0x80u);
   assert(wd1770_read(0x00u) == 0x81u);
   wd1770_write(0x00u, 0xd0u);
   assert(wd1770_read(0x00u) == 0x84u);
   wd1770_write(0x00u, 0x00u);
   run_us(1000u);
   assert(ioc.fiq & IOC_FIQ_DISC_IRQ);
   assert(wd1770_read(0x00u) == 0x84u);
   completions = irq_assertions;
   run_us(1000000u);
   assert(!(ioc.fiq & IOC_FIQ_DISC_IRQ) && irq_assertions == completions);

   unsigned before = drq_assertions;
   assert(finish_command(0xa0u, 1000u) == 0x10u);
   assert(transfer_count == 0u && drq_assertions == before);
   before = drq_assertions;
   assert(finish_command(0xf0u, 1000u) == 0x10u);
   assert(transfer_count == 0u && drq_assertions == before);
}

static void test_busy_jfd_read_is_not_replaced(const char *root)
{
   ArcJfdImage image = load_image(root, "size-3.jfd");
   controller_reset(&image);
   transfer_count = 0u;
   memset(transfer, 0, sizeof(transfer));
   ioc.fiq = 0u;
   wd1770_write(0x08u, 3u);
   wd1770_write(0x00u, 0x80u);
   assert(wd1770_read(0x00u) == 0x81u);

   wd1770_write(0x08u, 0xfeu);
   wd1770_write(0x00u, 0x80u);
   assert(wd1770_read(0x00u) == 0x81u);

   uint64_t end = tsc + (uint64_t)1100000u * TIMER_USEC;
   while (!(ioc.fiq & IOC_FIQ_DISC_IRQ) && tsc < end) step_controller();
   assert(ioc.fiq & IOC_FIQ_DISC_IRQ);
   assert(wd1770_read(0x00u) == 0x80u);
   assert(transfer_count == 1024u && transfer[0] == 3u);
   unsigned completions = irq_assertions;
   run_us(210000u);
   assert(!(ioc.fiq & IOC_FIQ_DISC_IRQ) && irq_assertions == completions);
   disc_close(0);
   arc_jfd_free(&image);
}

int main(int argc, char **argv)
{
   assert(argc == 2);
   test_family(argv[1]);
   test_rejections(argv[1]);
   test_empty_drive_cancellation();
   test_busy_jfd_read_is_not_replaced(argv[1]);
   puts("jfd_controller_tests: all checks passed");
   return 0;
}
