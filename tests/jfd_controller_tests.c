/* ArcDroid WD1770/JFD controller golden tests. GPL-2.0-only. */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arc_io.h"
#include "arcdroid_core.h"
#include "config.h"
#include "disc.h"
#include "disc_adf.h"
#include "ioc.h"
#include "jfd.h"
#include "timer.h"
#include "wd1770.h"
#include "82c711_fdc.h"

IOC_t ioc;
int speed_mhz = 1;
int romset = ROM_RISCOS_311;
int ins;
static unsigned irq_assertions;
static unsigned drq_assertions;
static uint8_t transfer[2048];
static size_t transfer_count;
static size_t newio_transfer_limit;
static bool newio_service_dma;
static bool newio_dma_write;
static uint8_t newio_write_value;
static uint8_t finish_command(uint8_t command, unsigned limit_us);

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
void ioc_irqa(uint8_t value) { ioc.irqa |= value; }
void ioc_irqb(uint8_t value) { ioc.irqb |= value; }
void ioc_irqbc(uint8_t value) { ioc.irqb &= (uint8_t)~value; }

/* A single in-memory helper lets the real raw-drive and WD1772 path prove
 * drive-1 selection without adding a production test hook. */
struct ArcIoFile { size_t position; bool writable; };
static uint8_t raw_helper[ARC_LAUNCH_DISC_SIZE];
static struct ArcIoFile raw_file;
ArcIoFile *arc_io_open_file(const char *path, bool writable)
{
   if (strcmp(path, "memory-helper.adl")) return NULL;
   raw_file.position = 0u; raw_file.writable = writable;
   return &raw_file;
}
bool arc_io_close_file(ArcIoFile *file) { return file == &raw_file; }
int64_t arc_io_file_length(ArcIoFile *file)
{ return file == &raw_file ? (int64_t)sizeof(raw_helper) : -1; }
int64_t arc_io_seek_file(ArcIoFile *file, int64_t offset, int origin)
{
   if (file != &raw_file || origin != RETRO_VFS_SEEK_POSITION_START ||
       offset < 0 || (uint64_t)offset > sizeof(raw_helper)) return -1;
   file->position = (size_t)offset; return offset;
}
size_t arc_io_read_file(ArcIoFile *file, void *data, size_t size)
{
   if (file != &raw_file || size > sizeof(raw_helper) - file->position) return 0u;
   memcpy(data, raw_helper + file->position, size); file->position += size;
   return size;
}
size_t arc_io_write_file(ArcIoFile *file, const void *data, size_t size)
{
   if (file != &raw_file || !file->writable ||
       size > sizeof(raw_helper) - file->position) return 0u;
   memcpy(raw_helper + file->position, data, size); file->position += size;
   return size;
}
bool arc_io_flush_file(ArcIoFile *file)
{ return file == &raw_file && file->writable; }

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
   adf_init();
   fdctype = FDC_WD1770;
   wd1770_reset();
   disc_reset();
   if (image) assert(disc_load_jfd(0, image));
   wd1770_writelatch_a(0x9eu); /* drive 0, side 0, motor on */
   wd1770_writelatch_b(0x00u); /* double density */
}

static void test_drive_one_helper(const char *root)
{
   static const char boot_text[] =
      "| ArcDroid launch disc\r"
      "Desktop -File ADFS::1.$.ArcLaunch\r";
   static const char launch_text[] =
      "| ArcDroid desktop boot file\r"
      "Dir ADFS::Demo.$\r"
      "URD ADFS::Demo.$\r"
      "Filer_Run ADFS::Demo.$.!Demo\r";
   ArcJfdImage image = load_image(root, "size-1.jfd");
   controller_reset(&image);
   assert(arc_launch_disc_build("Demo", "!Demo", raw_helper));
   adf_set_writable(1, 0);
   assert(disc_load_raw(1, "memory-helper.adl", ARC_GEOMETRY_640K_L));
   assert(writeprot[1] && !disc_empty(0) && !disc_empty(1));
   wd1770_writelatch_a(0x9du); /* drive 1, side 0, motor on */
   wd1770_write(0x08u, 0u);
   assert(finish_command(0x80u, 1100000u) == 0x80u);
   assert(transfer_count == 256u && !memcmp(transfer, raw_helper, 256u));

   /* Logical sector 1279 is track 79, side 0, sector 15. */
   wd1770_write(0x0cu, 79u);
   assert(finish_command(0x10u, 1100000u) == 0x80u);
   wd1770_writelatch_a(0x9du);
   wd1770_write(0x08u, 15u);
   assert(finish_command(0x80u, 1100000u) == 0x80u);
   assert(transfer_count == 256u &&
          !memcmp(transfer, boot_text, sizeof(boot_text) - 1u));

   /* Logical sector 1280 wraps to track 0 on sequential side 1. */
   wd1770_write(0x0cu, 0u);
   assert(finish_command(0x10u, 1100000u) == 0x84u);
   wd1770_writelatch_a(0x8du);
   wd1770_write(0x08u, 0u);
   assert(finish_command(0x80u, 1100000u) == 0x80u);
   assert(transfer_count == 256u &&
          !memcmp(transfer, launch_text, sizeof(launch_text) - 1u));

   assert(finish_command(0xa0u, 1000u) == 0x40u);
   assert(writeprot[1]);
   assert(!disc_empty(0));
   disc_close(1);
   disc_close(0);
   arc_jfd_free(&image);
}

static void step_controller(void)
{
   assert(timer_target != UINT32_MAX);
   tsc = (uint64_t)timer_target << 32u;
   timer_process();
   if ((ioc.fiq & IOC_FIQ_DISC_DATA) && fdctype == FDC_82C711 && newio_service_dma)
   {
      assert(transfer_count < sizeof(transfer));
      bool terminal = newio_transfer_limit && transfer_count + 1u == newio_transfer_limit;
      if (newio_dma_write)
         c82c711_fdc_dmawrite(newio_write_value, terminal, NULL);
      else
         transfer[transfer_count] = c82c711_fdc_dmaread(terminal, NULL);
      transfer_count++;
   }
   else if ((ioc.fiq & IOC_FIQ_DISC_DATA) && fdctype != FDC_82C711)
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

static void test_82c711_hardening(void)
{
   memset(&ioc, 0, sizeof(ioc));
   tsc = 0u;
   timer_reset();
   disc_init();
   disc_reset();
   fdctype = FDC_82C711;
   c82c711_fdc_init();

   /* Guest-selected drives 2/3 must return an abnormal result without ever
    * indexing the two-drive track/eot arrays. */
   c82c711_fdc_write(5u, 0x06u, NULL);
   const uint8_t read_drive_2[8] = {2u, 0u, 0u, 0u, 1u, 0u, 0x1bu, 0xffu};
   for (unsigned i = 0; i < 8u; ++i) c82c711_fdc_write(5u, read_drive_2[i], NULL);
   assert((c82c711_fdc_read(4u, NULL) & 0xf0u) == 0xd0u);
   assert((c82c711_fdc_read(5u, NULL) & 0x43u) == 0x42u);
   for (unsigned i = 1; i < 7u; ++i) (void)c82c711_fdc_read(5u, NULL);

   assert(arc_launch_disc_build("Demo", "!Demo", raw_helper));
   adf_init();
   adf_set_writable(0, 1);
   assert(disc_load_raw(0, "memory-helper.adl", ARC_GEOMETRY_640K_L));
   c82c711_fdc_write(2u, 0x1cu, NULL);
   c82c711_fdc_write(7u, 1u, NULL);

   /* Zero-sector FORMAT is rejected before accepting DMA bytes. */
   c82c711_fdc_write(5u, 0x0du, NULL);
   const uint8_t bad_format[5] = {0u, 1u, 0u, 0x1bu, 0u};
   for (unsigned i = 0; i < 5u; ++i) c82c711_fdc_write(5u, bad_format[i], NULL);
   assert((c82c711_fdc_read(4u, NULL) & 0xf0u) == 0xd0u);
   for (unsigned i = 0; i < 7u; ++i) (void)c82c711_fdc_read(5u, NULL);

   /* A terminal count before the final CHRN byte is an abnormal completion,
    * and the data request is cleared before the result becomes visible. */
   c82c711_fdc_write(5u, 0x0du, NULL);
   const uint8_t short_format[5] = {0u, 1u, 2u, 0x1bu, 0u};
   for (unsigned i = 0; i < 5u; ++i) c82c711_fdc_write(5u, short_format[i], NULL);
   while (!(ioc.fiq & IOC_FIQ_DISC_DATA)) step_controller();
   c82c711_fdc_dmawrite(0u, 1, NULL);
   step_controller();
   assert(!(ioc.fiq & IOC_FIQ_DISC_DATA));
   assert((c82c711_fdc_read(4u, NULL) & 0xf0u) == 0xd0u);
   assert((c82c711_fdc_read(5u, NULL) & 0x40u) == 0x40u);
   assert(c82c711_fdc_read(5u, NULL) == 0x10u);
   for (unsigned i = 2u; i < 7u; ++i) (void)c82c711_fdc_read(5u, NULL);

   /* An incomplete parameter sequence remains in command phase until reset. */
   c82c711_fdc_write(5u, 0x0du, NULL);
   for (unsigned i = 0u; i < 4u; ++i) c82c711_fdc_write(5u, short_format[i], NULL);
   assert((c82c711_fdc_read(4u, NULL) & 0xf0u) == 0x90u);
   c82c711_fdc_shutdown();
   c82c711_fdc_init();

   /* Counts beyond the 256-byte parameter buffer are rejected as well. */
   c82c711_fdc_write(5u, 0x0du, NULL);
   const uint8_t long_format[5] = {0u, 1u, 65u, 0x1bu, 0u};
   for (unsigned i = 0; i < 5u; ++i) c82c711_fdc_write(5u, long_format[i], NULL);
   assert((c82c711_fdc_read(4u, NULL) & 0xf0u) == 0xd0u);
   for (unsigned i = 0; i < 7u; ++i) (void)c82c711_fdc_read(5u, NULL);

   /* Unknown commands use the one-byte invalid-command result in every build. */
   c82c711_fdc_write(5u, 0x19u, NULL);
   for (unsigned poll = 0u;
        poll < 1000u && (c82c711_fdc_read(4u, NULL) & 0xf0u) != 0xd0u;
        ++poll) step_controller();
   assert(c82c711_fdc_read(5u, NULL) == 0x80u);
   c82c711_fdc_shutdown();
   disc_close(0);
}

static void newio_reset(const ArcJfdImage *image, bool helper)
{
   memset(&ioc, 0, sizeof(ioc));
   tsc = 0u;
   timer_reset();
   disc_init();
   adf_init();
   disc_reset();
   fdctype = FDC_82C711;
   if (image) assert(disc_load_jfd(0, image));
   if (helper)
   {
      assert(arc_launch_disc_build("Demo", "!Demo", raw_helper));
      adf_set_writable(1, 0);
      assert(disc_load_raw(1, "memory-helper.adl", ARC_GEOMETRY_640K_L));
   }
   c82c711_fdc_init();
   c82c711_fdc_write(2u, 0x1cu, NULL); /* reset released, drive 0 motor */
   c82c711_fdc_write(7u, 1u, NULL);    /* double density */
}

static void newio_start_read(unsigned drive, uint8_t sector, uint8_t size)
{
   const uint8_t params[8] = {
      (uint8_t)drive, 0u, 0u, sector, size, sector, 0x1bu, 0xffu
   };
   c82c711_fdc_write(2u, 0x0cu, NULL);
   c82c711_fdc_write(2u, (uint8_t)(0x0cu | (0x10u << drive) | drive), NULL);
   c82c711_fdc_write(5u, 0x06u, NULL);
   for (unsigned i = 0u; i < sizeof(params); ++i)
      c82c711_fdc_write(5u, params[i], NULL);
}

static void newio_start_write(unsigned drive, uint8_t sector, uint8_t size)
{
   const uint8_t params[8] = {
      (uint8_t)drive, 0u, 0u, sector, size, sector, 0x1bu, 0xffu
   };
   c82c711_fdc_write(2u, 0x0cu, NULL);
   c82c711_fdc_write(2u, (uint8_t)(0x0cu | (0x10u << drive) | drive), NULL);
   c82c711_fdc_write(5u, 0x05u, NULL);
   for (unsigned i = 0u; i < sizeof(params); ++i)
      c82c711_fdc_write(5u, params[i], NULL);
}

static void newio_wait_irq(unsigned limit_us)
{
   static unsigned wait_number;
   ++wait_number;
   uint64_t end = tsc + (uint64_t)limit_us * TIMER_USEC;
   while (!(ioc.irqb & IOC_IRQB_DISC_IRQ) && tsc < end) step_controller();
   if (!(ioc.irqb & IOC_IRQB_DISC_IRQ))
      fprintf(stderr, "newio wait %u timed out after %zu DMA bytes\n",
              wait_number, transfer_count);
   assert(ioc.irqb & IOC_IRQB_DISC_IRQ);
   (void)c82c711_fdc_read(4u, NULL); /* acknowledge completion interrupt */
}

static void newio_result(uint8_t result[7])
{
   for (unsigned i = 0u; i < 7u; ++i)
      result[i] = c82c711_fdc_read(5u, NULL);
}

static void test_82c711_golden(const char *root)
{
   uint8_t result[7];
   ArcJfdImage image = load_image(root, "size-3.jfd");
   newio_reset(&image, true);

   transfer_count = 0u;
   newio_transfer_limit = 1024u;
   newio_service_dma = true;
   newio_dma_write = false;
   newio_start_read(0u, 3u, 3u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert(!(result[0] & 0xc0u) && result[1] == 0u && result[2] == 0u);
   assert(transfer_count == 1024u && transfer[0] == 3u && !(ioc.fiq & IOC_FIQ_DISC_DATA));

   /* The independently mounted raw helper exercises drive 1. */
   transfer_count = 0u;
   newio_transfer_limit = 256u;
   newio_start_read(1u, 0u, 1u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert(!(result[0] & 0xc0u) && transfer_count == 256u &&
          !memcmp(transfer, raw_helper, 256u));

   /* A writable raw backing receives the exact DMA payload. This is the
    * controller-level oracle used beneath the libretro shadow policy. */
   disc_close(1);
   adf_set_writable(1, 1);
   assert(disc_load_raw(1, "memory-helper.adl", ARC_GEOMETRY_640K_L));
   transfer_count = 0u;
   newio_transfer_limit = 256u;
   newio_dma_write = true;
   newio_write_value = 0xa6u;
   newio_start_write(1u, 0u, 1u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert(!(result[0] & 0xc0u) && transfer_count == 256u);
   for (unsigned i = 0u; i < 256u; ++i) assert(raw_helper[i] == 0xa6u);
   newio_dma_write = false;

   /* Media removal terminates the active drive exactly once with no stale FIQ. */
   c82c711_fdc_shutdown();
   disc_close(1);
   disc_close(0);
   newio_reset(&image, true);
   transfer_count = 0u;
   newio_transfer_limit = 1024u;
   newio_start_read(0u, 3u, 3u);
   c82c711_fdc_media_change(0u);
   assert(!(ioc.fiq & IOC_FIQ_DISC_DATA) && (ioc.irqb & IOC_IRQB_DISC_IRQ));
   c82c711_fdc_media_change(0u);
   (void)c82c711_fdc_read(4u, NULL);
   newio_result(result);
   assert((result[0] & 0x40u) && result[1] == 0x04u);

   /* Removing drive 0 must not disturb an active helper-drive transfer. */
   c82c711_fdc_shutdown();
   disc_close(1);
   disc_close(0);
   newio_reset(&image, true);
   transfer_count = 0u;
   newio_transfer_limit = 256u;
   newio_start_read(1u, 0u, 1u);
   c82c711_fdc_media_change(0u);
   assert(!(ioc.irqb & IOC_IRQB_DISC_IRQ));
   while (!(ioc.irqb & IOC_IRQB_DISC_IRQ)) step_controller();
   (void)c82c711_fdc_read(4u, NULL);
   newio_result(result);
   assert(!(result[0] & 0xc0u) && transfer_count == 256u);

   /* Deliberately miss a DMA request: overrun clears FIQ and a later read recovers. */
   transfer_count = 0u;
   newio_service_dma = false;
   newio_dma_write = false;
   newio_start_read(0u, 3u, 3u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert((result[0] & 0x40u) && result[1] == 0x10u &&
          !(ioc.fiq & IOC_FIQ_DISC_DATA));
   newio_service_dma = true;
   newio_transfer_limit = 1024u;
   newio_start_read(0u, 3u, 3u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert(!(result[0] & 0xc0u) && transfer_count == 1024u);

   /* JFD is read-only: write data completes with the controller WP result. */
   c82c711_fdc_write(5u, 0x05u, NULL);
   const uint8_t write_params[8] = {0u, 0u, 0u, 3u, 3u, 3u, 0x1bu, 0xffu};
   for (unsigned i = 0u; i < sizeof(write_params); ++i)
      c82c711_fdc_write(5u, write_params[i], NULL);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert((result[0] & 0x40u) && result[1] == 0x02u &&
          !(ioc.fiq & IOC_FIQ_DISC_DATA));

   c82c711_fdc_shutdown();
   disc_close(1);
   disc_close(0);
   arc_jfd_free(&image);

   ArcJfdImage crc = load_image(root, "data-crc.jfd");
   newio_reset(&crc, false);
   transfer_count = 0u;
   newio_transfer_limit = 128u;
   newio_service_dma = true;
   newio_start_read(0u, 4u, 0u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert((result[0] & 0x40u) && result[1] == 0x20u && result[2] == 0x20u &&
          !(ioc.fiq & IOC_FIQ_DISC_DATA));
   c82c711_fdc_shutdown();
   disc_close(0);
   arc_jfd_free(&crc);
}

int main(int argc, char **argv)
{
   assert(argc == 2);
   test_family(argv[1]);
   test_rejections(argv[1]);
   test_empty_drive_cancellation();
   test_busy_jfd_read_is_not_replaced(argv[1]);
   test_drive_one_helper(argv[1]);
   test_82c711_hardening();
   test_82c711_golden(argv[1]);
   puts("jfd_controller_tests: all checks passed");
   return 0;
}
