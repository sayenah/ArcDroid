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
#include "disc_jfd.h"
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
static bool wd_write_mode;
static bool wd_service_data = true;
static uint8_t wd_write_value;
static unsigned jfd_commits;
static bool jfd_fail_commit;
static bool test_jfd_commit(ArcJfdImage *image, size_t index, const uint8_t *data)
{
   assert(image->overlay && index < image->sector_count);
   if (jfd_fail_commit)
   {
      image->overlay->writable = false;
      image->overlay->failed = true;
      return false;
   }
   uint8_t *copy = malloc(image->sectors[index].size);
   assert(copy);
   memcpy(copy, data, image->sectors[index].size);
   free(image->overlay->data[index]);
   image->overlay->data[index] = copy;
   ++jfd_commits;
   return true;
}
static bool test_jfd_commit_created(ArcJfdImage *image, unsigned track_side,
                                    unsigned id, const uint8_t *data)
{
   assert(image->overlay && track_side < ARC_JFD_PDD5_TRACKS &&
          id < ARC_JFD_PDD5_IDS);
   if (jfd_fail_commit) return false;
   uint8_t *copy = malloc(1024u); assert(copy);
   memcpy(copy, data, 1024u);
   free(image->overlay->created[track_side][id]);
   image->overlay->created[track_side][id] = copy;
   ++jfd_commits;
   return true;
}
static void make_jfd_writable(ArcJfdImage *image)
{
   image->overlay = calloc(1u, sizeof(*image->overlay));
   assert(image->overlay);
   image->overlay->data = calloc(image->sector_count, sizeof(uint8_t *));
   assert(image->overlay->data);
   image->overlay->writable = image->overlay->visible = true;
   image->overlay->commit = test_jfd_commit;
}
static uint8_t finish_command(uint8_t command, unsigned limit_us);
static void newio_wait_irq(unsigned limit_us);
static void newio_result(uint8_t result[7]);
static void assert_newio_refusal_quiet(void);

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
static uint8_t raw_helper[1638400u];
static uint8_t raw_hd[1638400u];
static size_t raw_helper_length = ARC_LAUNCH_DISC_SIZE;
static size_t raw_hd_length = sizeof(raw_hd);
static struct ArcIoFile raw_file;
static struct ArcIoFile raw_hd_file;
static bool raw_backing(ArcIoFile *file, uint8_t **bytes, size_t *length)
{
   if (file == &raw_file)
   { *bytes = raw_helper; *length = raw_helper_length; return true; }
   if (file == &raw_hd_file)
   { *bytes = raw_hd; *length = raw_hd_length; return true; }
   return false;
}
ArcIoFile *arc_io_open_file(const char *path, bool writable)
{
   ArcIoFile *file = !strcmp(path, "memory-helper.adl") ? &raw_file :
                     !strcmp(path, "memory-hd.adf") ? &raw_hd_file : NULL;
   if (!file) return NULL;
   file->position = 0u; file->writable = writable;
   return file;
}
bool arc_io_close_file(ArcIoFile *file)
{ return file == &raw_file || file == &raw_hd_file; }
int64_t arc_io_file_length(ArcIoFile *file)
{
   uint8_t *bytes; size_t length;
   return raw_backing(file, &bytes, &length) ? (int64_t)length : -1;
}
int64_t arc_io_seek_file(ArcIoFile *file, int64_t offset, int origin)
{
   uint8_t *bytes; size_t length;
   if (!raw_backing(file, &bytes, &length) ||
       origin != RETRO_VFS_SEEK_POSITION_START || offset < 0 ||
       (uint64_t)offset > length) return -1;
   file->position = (size_t)offset; return offset;
}
size_t arc_io_read_file(ArcIoFile *file, void *data, size_t size)
{
   uint8_t *bytes; size_t length;
   if (!raw_backing(file, &bytes, &length) ||
       size > length - file->position) return 0u;
   memcpy(data, bytes + file->position, size); file->position += size;
   return size;
}
size_t arc_io_write_file(ArcIoFile *file, const void *data, size_t size)
{
   uint8_t *bytes; size_t length;
   if (!raw_backing(file, &bytes, &length) || !file->writable ||
       size > length - file->position) return 0u;
   memcpy(bytes + file->position, data, size); file->position += size;
   return size;
}
bool arc_io_flush_file(ArcIoFile *file)
{ return (file == &raw_file || file == &raw_hd_file) && file->writable; }

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

static ArcJfdImage load_creation_image(const char *root, const char *name)
{
   char path[4096], error[256];
   int n = snprintf(path, sizeof(path), "%s/jfd-creation/%s", root, name);
   assert(n > 0 && (size_t)n < sizeof(path));
   size_t size;
   uint8_t *bytes = read_all(path, &size);
   ArcJfdImage image;
   bool okay = arc_jfd_parse_owned(bytes, size, &image, error, sizeof(error));
   if (!okay) fprintf(stderr, "%s: %s\n", name, error);
   assert(okay);
   return image;
}

static void make_jfd_creation_writable(ArcJfdImage *image)
{
   make_jfd_writable(image);
   image->overlay->eligible = calloc(image->sector_count, 1u);
   assert(image->overlay->eligible);
   for (size_t i = 0u; i < image->sector_count; ++i)
      image->overlay->eligible[i] = arc_jfd_sector_write_eligible(image, i);
   assert(arc_jfd_plan_creation(image, image->overlay));
   image->overlay->commit_created = test_jfd_commit_created;
}

static bool build_launch_helper(void)
{
   ArcLaunchDiscovery discovery;
   char error_text[128];
   memset(&discovery, 0, sizeof(discovery));
   discovery.candidate_count = 1u;
   discovery.application_count = 1u;
   discovery.candidates[0].name.length = 5u;
   memcpy(discovery.candidates[0].name.bytes, "!Demo", 5u);
   discovery.candidates[0].state = ARC_LAUNCH_CANDIDATE_APPLICATION;
   discovery.candidates[0].launchable = true;
   return arc_launch_disc_build(&discovery, 0u, raw_helper,
                                error_text, sizeof(error_text));
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
      "%Desktop -File \"ADFS::1.$.ArcLaunch\"\r";
   static const char launch_text[] =
      "| ArcDroid desktop boot file\r"
      "%Filer_Boot ADFS::0.$.!Demo\r"
      "%Dir ADFS::0.$\r"
      "%URD ADFS::0.$\r"
      "%Filer_Run ADFS::0.$.!Demo\r";
   ArcJfdImage image = load_image(root, "size-1.jfd");
   controller_reset(&image);
   assert(build_launch_helper());
   raw_helper_length = ARC_LAUNCH_DISC_SIZE;
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
   else if ((ioc.fiq & IOC_FIQ_DISC_DATA) && fdctype != FDC_82C711 &&
            wd_service_data)
   {
      assert(transfer_count < sizeof(transfer));
      if (wd_write_mode)
      {
         wd1770_write(0x0cu, wd_write_value);
         ++transfer_count;
      }
      else transfer[transfer_count++] = wd1770_read(0x0cu);
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

static void assert_wd_refusal_quiet(unsigned irq_before)
{
   assert(irq_assertions == irq_before + 1u &&
          !(ioc.fiq & (IOC_FIQ_DISC_IRQ | IOC_FIQ_DISC_DATA)) &&
          !timer_is_enabled(fdc_timer));
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

static void test_wd_jfd_write(const char *root)
{
   for (unsigned code = 0u; code < 3u; ++code)
   {
      char name[32];
      snprintf(name, sizeof(name), "size-%u.jfd", code);
      ArcJfdImage sized = load_image(root, name);
      make_jfd_writable(&sized);
      controller_reset(&sized);
      wd1770_write(0x08u, (uint8_t)code);
      wd_write_mode = true; wd_write_value = (uint8_t)(0x40u + code);
      jfd_commits = 0u;
      assert(finish_command(0xa0u, 1100000u) == 0x80u &&
             transfer_count == ((size_t)128u << code) &&
             jfd_commits == 1u &&
             sized.overlay->data[0][0] == wd_write_value);
      wd_write_mode = false;
      disc_close(0); arc_jfd_free(&sized);
   }
   static const struct { const char *name; uint8_t status; } refusals[] = {
      {"write-slow.jfd", 0x08u}, {"write-duplicate.jfd", 0x08u},
      {"write-shared.jfd", 0x08u}, {"data-crc.jfd", 0x08u},
      {"id-crc.jfd", 0x18u}, {"id-only.jfd", 0x10u},
      {"blank-track.jfd", 0x10u}
   };
   for (size_t i = 0u; i < sizeof(refusals) / sizeof(refusals[0]); ++i)
   {
      ArcJfdImage refusal = load_image(root, refusals[i].name);
      make_jfd_writable(&refusal);
      controller_reset(&refusal);
      wd1770_write(0x08u, i < 3u ? 3u : 4u);
      wd_write_mode = false; jfd_commits = 0u;
      unsigned irq_before = irq_assertions;
      uint8_t refusal_status = finish_command(0xa0u, 1100000u);
      if (refusal_status != refusals[i].status || transfer_count > 1u || jfd_commits)
         fprintf(stderr, "WD %s status=%02x transfer=%zu commits=%u\n",
                 refusals[i].name, refusal_status, transfer_count, jfd_commits);
      assert(refusal_status == refusals[i].status &&
             jfd_commits == 0u && transfer_count <= 1u);
      assert_wd_refusal_quiet(irq_before);
      disc_close(0);
      arc_jfd_free(&refusal);
   }
   ArcJfdImage image = load_image(root, "size-3.jfd");
   make_jfd_writable(&image);
   controller_reset(&image);
   assert(!writeprot[0]);
   wd1770_write(0x08u, 3u);
   wd_write_mode = true; wd_write_value = 0x5au; jfd_commits = 0u;
   assert(finish_command(0xa0u, 1100000u) == 0x80u);
   wd_write_mode = false;
   assert(transfer_count == 1024u && jfd_commits == 1u &&
          image.overlay->data[0][0] == 0x5au);
   wd1770_write(0x08u, 3u);
   assert(finish_command(0x80u, 1100000u) == 0x80u);
   assert(transfer_count == 1024u && transfer[0] == 0x5au);
   unsigned deleted_irq_before = irq_assertions;
   assert(finish_command(0xa1u, 1000u) == 0x40u);
   assert(jfd_commits == 1u);
   assert_wd_refusal_quiet(deleted_irq_before);
   disc_close(0);
   arc_jfd_free(&image);

   image = load_image(root, "size-3.jfd");
   make_jfd_writable(&image);
   controller_reset(&image);
   wd1770_write(0x08u, 3u);
   wd_write_mode = true; wd_write_value = 0x69u; jfd_commits = 0u;
   transfer_count = 0u; ioc.fiq = 0u;
   wd1770_write(0x00u, 0xa0u);
   uint64_t partial_deadline = tsc + (uint64_t)1100000u * TIMER_USEC;
   while (transfer_count < 32u && tsc < partial_deadline) step_controller();
   assert(transfer_count == 32u && jfd_commits == 0u);
   wd1770_write(0x00u, 0xd0u); /* Force Interrupt discards the partial buffer. */
   wd_write_mode = false;
   run_us(40000u);
   assert(jfd_commits == 0u && !image.overlay->data[0] &&
          !(ioc.fiq & IOC_FIQ_DISC_DATA));
   disc_close(0);
   arc_jfd_free(&image);

   image = load_image(root, "size-3.jfd");
   make_jfd_writable(&image);
   controller_reset(&image);
   wd1770_write(0x08u, 3u);
   wd_write_mode = jfd_fail_commit = true;
   wd_write_value = 0x91u; jfd_commits = 0u;
   unsigned failed_irq_before = irq_assertions;
   assert(finish_command(0xa0u, 1100000u) == 0x40u);
   assert_wd_refusal_quiet(failed_irq_before);
   assert(image.overlay->failed && !image.overlay->data[0] &&
          jfd_commits == 0u);
   failed_irq_before = irq_assertions;
   assert(finish_command(0xa0u, 1100000u) == 0x40u && jfd_commits == 0u);
   assert_wd_refusal_quiet(failed_irq_before);
   wd_write_mode = jfd_fail_commit = false;
   disc_close(0);
   arc_jfd_free(&image);

   image = load_image(root, "size-3.jfd");
   make_jfd_writable(&image);
   controller_reset(&image);
   wd1770_write(0x08u, 3u);
   wd_service_data = false;
   unsigned underrun_irq_before = irq_assertions;
   assert(finish_command(0xa0u, 1100000u) == 0x84u);
   assert(jfd_commits == 0u && !image.overlay->data[0]);
   assert_wd_refusal_quiet(underrun_irq_before);
   wd_service_data = true;
   disc_close(0); arc_jfd_free(&image);

   image = load_image(root, "write-two.jfd");
   make_jfd_writable(&image);
   controller_reset(&image);
   wd1770_write(0x08u, 3u);
   wd_write_mode = true; wd_write_value = 0x7cu; jfd_commits = 0u;
   transfer_count = 0u; ioc.fiq = 0u;
   unsigned multi_irq_before = irq_assertions;
   wd1770_write(0x00u, 0xb0u);
   uint64_t deadline = tsc + (uint64_t)1100000u * TIMER_USEC;
   while (jfd_commits < 2u && tsc < deadline) step_controller();
   assert(jfd_commits == 2u && (wd1770_read(0x00u) & 1u));
   while (!(ioc.fiq & IOC_FIQ_DISC_IRQ) && tsc < deadline) step_controller();
   assert(ioc.fiq & IOC_FIQ_DISC_IRQ);
   assert(wd1770_read(0x00u) == 0x10u);
   assert(!(ioc.fiq & IOC_FIQ_DISC_DATA) &&
          !timer_is_enabled(fdc_timer) && jfd_commits == 2u);
   assert_wd_refusal_quiet(multi_irq_before);
   wd_write_mode = false;
   assert(jfd_commits == 2u && image.overlay->data[0][0] == 0x7cu &&
          image.overlay->data[1][0] == 0x7cu);
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
   unsigned irq_before = irq_assertions;
   assert(finish_command(0xa0u, 1000u) == 0x40u);
   assert(transfer_count == 0u && drq_assertions == before);
   assert_wd_refusal_quiet(irq_before);
   before = drq_assertions;
   irq_before = irq_assertions;
   assert(finish_command(0xf0u, 1000u) == 0x40u);
   assert(transfer_count == 0u && drq_assertions == before);
   assert_wd_refusal_quiet(irq_before);
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
      {"reject-density8.jfd", "octal density"},
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

   /* SENSE DRIVE STATUS returns one ST3 byte with READY clear for selectors
    * 2/3 and must not poison the bounded current-drive state. */
   for (uint8_t drive = 2u; drive < 4u; ++drive)
   {
      c82c711_fdc_write(5u, 0x04u, NULL);
      c82c711_fdc_write(5u, drive, NULL);
      assert((c82c711_fdc_read(4u, NULL) & 0xf0u) == 0xd0u);
      assert(c82c711_fdc_read(5u, NULL) == (uint8_t)(0x08u | drive));
   }
   c82c711_fdc_write(5u, 0x08u, NULL);
   assert((c82c711_fdc_read(4u, NULL) & 0xf0u) == 0xd0u);
   (void)c82c711_fdc_read(5u, NULL);
   assert(c82c711_fdc_read(5u, NULL) == 0u);

   /* Guest-selected drives 2/3 must return an abnormal result without ever
    * indexing the two-drive track/eot arrays. */
   c82c711_fdc_write(5u, 0x06u, NULL);
   const uint8_t read_drive_2[8] = {2u, 0u, 0u, 0u, 1u, 0u, 0x1bu, 0xffu};
   for (unsigned i = 0; i < 8u; ++i) c82c711_fdc_write(5u, read_drive_2[i], NULL);
   assert((c82c711_fdc_read(4u, NULL) & 0xf0u) == 0xd0u);
   assert((c82c711_fdc_read(5u, NULL) & 0x43u) == 0x42u);
   for (unsigned i = 1; i < 7u; ++i) (void)c82c711_fdc_read(5u, NULL);

   assert(build_launch_helper());
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
      assert(build_launch_helper());
      raw_helper_length = ARC_LAUNCH_DISC_SIZE;
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

static void newio_start_id(unsigned side)
{
   c82c711_fdc_write(5u, 0x0au, NULL);
   c82c711_fdc_write(5u, side ? 4u : 0u, NULL);
}

static unsigned observed_index_pulses;
static void (*controller_indexpulse)(void *);
static unsigned observed_notfound_calls;
static void (*controller_notfound)(void *);
static fdc_funcs_t *original_controller_funcs;
static void observed_indexpulse(void *opaque)
{
   ++observed_index_pulses;
   controller_indexpulse(opaque);
}
static void observe_indexes(fdc_funcs_t *copy)
{
   original_controller_funcs = fdc_funcs;
   *copy = *fdc_funcs;
   controller_indexpulse = copy->indexpulse;
   copy->indexpulse = observed_indexpulse;
   fdc_funcs = copy;
   observed_index_pulses = 0u;
}

static void observed_notfound(void *opaque)
{
   ++observed_notfound_calls;
   controller_notfound(opaque);
}

static void observe_notfound(fdc_funcs_t *copy)
{
   original_controller_funcs = fdc_funcs;
   *copy = *fdc_funcs;
   controller_notfound = copy->notfound;
   copy->notfound = observed_notfound;
   fdc_funcs = copy;
   observed_notfound_calls = 0u;
}

static void test_82c711_quad(const char *root)
{
   uint8_t result[7];
   ArcJfdImage image = load_image(root, "quad-data.jfd");
   newio_reset(&image, false);
   c82c711_fdc_write(7u, 0u, NULL);
   newio_start_id(0u);
   newio_wait_irq(400000u);
   newio_result(result);
   assert(result[0] == 0u && result[3] == 0u && result[4] == 0u &&
          result[5] == 3u && result[6] == 3u);
   transfer_count = 0u;
   newio_transfer_limit = 1024u;
   newio_service_dma = true;
   newio_start_read(0u, 3u, 3u);
   newio_wait_irq(400000u);
   newio_result(result);
   assert(!(result[0] & 0xc0u) && transfer_count == 1024u &&
          transfer[0] == 0x5au && transfer[1023] == 0x5au);
   c82c711_fdc_shutdown();
   disc_close(0);
   arc_jfd_free(&image);

   image = load_image(root, "quad-id-only.jfd");
   newio_reset(&image, false);
   c82c711_fdc_write(7u, 0u, NULL);
   newio_start_id(0u);
   newio_wait_irq(400000u);
   newio_result(result);
   assert(result[5] == 3u);
   fdc_funcs_t observed;
   observe_indexes(&observed);
   newio_start_read(0u, 3u, 0u);
   while (observed_index_pulses < 1u) step_controller();
   assert(!(ioc.irqb & IOC_IRQB_DISC_IRQ));
   while (observed_index_pulses < 2u) step_controller();
   assert(ioc.irqb & IOC_IRQB_DISC_IRQ);
   (void)c82c711_fdc_read(4u, NULL);
   newio_result(result);
   assert((result[0] & 0x40u) && result[1] == 5u && result[2] == 0u);
   run_us(250000u);
   assert(!(ioc.irqb & IOC_IRQB_DISC_IRQ));
   fdc_funcs = original_controller_funcs;
   c82c711_fdc_shutdown();
   disc_close(0);
   arc_jfd_free(&image);
}

static void test_82c711_jfd_search_failures(const char *root)
{
   ArcJfdImage image = load_image(root, "quad-data.jfd");
   for (unsigned failure = 0u; failure < 5u; ++failure)
   {
      newio_reset(&image, false);
      c82c711_fdc_write(7u, failure == 0u ? 1u : failure == 1u ? 3u : 0u,
                        NULL);
      fdc_funcs_t observed;
      observe_indexes(&observed);
      if (failure == 3u) image.tracks[0].sector_count = 0u;
      if (failure == 4u) image.track_count = 1u;
      if (failure == 4u) newio_start_id(1u);
      else newio_start_read(0u, failure == 2u ? 99u : 3u, 3u);
      while (observed_index_pulses < 1u) step_controller();
      assert(!(ioc.irqb & IOC_IRQB_DISC_IRQ));
      while (observed_index_pulses < 2u) step_controller();
      assert(ioc.irqb & IOC_IRQB_DISC_IRQ);
      (void)c82c711_fdc_read(4u, NULL);
      uint8_t result[7];
      newio_result(result);
      assert((result[0] & 0x40u) && result[1] == 5u && result[2] == 0u);
      run_us(250000u);
      assert(!(ioc.irqb & IOC_IRQB_DISC_IRQ));
      fdc_funcs = original_controller_funcs;
      image.tracks[0].sector_count = 1u;
      image.track_count = 2u;
      c82c711_fdc_shutdown();
      disc_close(0);
   }
   arc_jfd_free(&image);

   image = load_image(root, "size-0.jfd");
   newio_reset(&image, false);
   newio_start_id(0u);
   newio_wait_irq(400000u);
   uint8_t result[7];
   newio_result(result);
   assert(!(result[0] & 0xc0u) && result[5] == 0u && result[6] == 0u);
   c82c711_fdc_shutdown();
   disc_close(0);
   arc_jfd_free(&image);

   image = load_image(root, "quad-data.jfd");
   newio_reset(&image, false);
   c82c711_fdc_write(7u, 1u, NULL); /* unmatched DD search */
   newio_start_read(0u, 3u, 3u);
   run_us(100000u);
   assert(!(ioc.irqb & IOC_IRQB_DISC_IRQ));
   c82c711_fdc_media_change(0u);
   assert(ioc.irqb & IOC_IRQB_DISC_IRQ);
   (void)c82c711_fdc_read(4u, NULL);
   newio_result(result);
   assert((result[0] & 0x40u) && result[1] == 0x04u);
   run_us(500000u);
   assert(!(ioc.irqb & IOC_IRQB_DISC_IRQ));
   c82c711_fdc_shutdown();
   disc_close(0);
   arc_jfd_free(&image);

   image = load_image(root, "quad-id-crc.jfd");
   newio_reset(&image, false);
   c82c711_fdc_write(7u, 0u, NULL);
   newio_start_id(0u);
   newio_wait_irq(400000u);
   newio_result(result);
   assert((result[0] & 0x40u) && result[1] == 0x20u);
   c82c711_fdc_shutdown();
   disc_close(0);
   arc_jfd_free(&image);

   image = load_image(root, "quad-data-crc.jfd");
   newio_reset(&image, false);
   c82c711_fdc_write(7u, 0u, NULL);
   transfer_count = 0u;
   newio_transfer_limit = 1024u;
   newio_service_dma = true;
   newio_start_read(0u, 3u, 3u);
   newio_wait_irq(400000u);
   newio_result(result);
   assert((result[0] & 0x40u) && result[1] == 0x20u &&
          result[2] == 0x20u && transfer_count == 1024u);
   c82c711_fdc_shutdown();
   disc_close(0);
   arc_jfd_free(&image);
}

static void test_82c711_search_cancellation(const char *root)
{
   ArcJfdImage image = load_image(root, "quad-data.jfd");
   for (unsigned reset = 0u; reset < 2u; ++reset)
   {
      newio_reset(&image, false);
      c82c711_fdc_write(7u, 1u, NULL); /* DD cannot match the HD sector. */
      fdc_funcs_t observed;
      observe_notfound(&observed);
      newio_start_read(0u, 3u, 3u);
      run_us(100000u);
      assert(observed_notfound_calls == 0u);
      if (reset)
         c82c711_fdc_write(4u, 0x80u, NULL); /* controller reset */
      else
         disc_stop(0); /* command cancellation */
      run_us(500000u);
      assert(observed_notfound_calls == 0u);
      fdc_funcs = original_controller_funcs;
      c82c711_fdc_shutdown();
      disc_close(0);
   }
   arc_jfd_free(&image);
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

static void newio_creation_command(uint8_t command, uint8_t selected_side,
                                    uint8_t c, uint8_t h, uint8_t sector,
                                    uint8_t n)
{
   const uint8_t params[8] = {
      (uint8_t)(selected_side << 2u), c, h, sector, n, sector, 0x1bu, 0xffu
   };
   c82c711_fdc_write(2u, 0x0cu, NULL);
   c82c711_fdc_write(2u, 0x1cu, NULL);
   c82c711_fdc_write(5u, command, NULL);
   for (unsigned i = 0u; i < sizeof(params); ++i)
      c82c711_fdc_write(5u, params[i], NULL);
}

static void test_wd_creation(const char *root)
{
   for (unsigned timed = 0u; timed < 3u; ++timed)
   {
      ArcJfdImage image = load_creation_image(root, timed == 2u ?
         "boundary.jfd" : timed ? "partial-timed.jfd" :
         "partial-untimed.jfd");
      make_jfd_creation_writable(&image);
      controller_reset(&image);
      wd1770_writelatch_a(0x8eu); /* physical side 1 */
      fdc_funcs_t observed;
      observe_indexes(&observed);
      for (unsigned address = 0u; address < 3u; ++address)
      {
         assert(finish_command(0xc0u, 1100000u) == 0x80u &&
                transfer_count == 6u && transfer[2] != 1u &&
                transfer[2] != 3u);
      }
      observed_index_pulses = 0u;
      wd1770_write(0x08u, 1u);
      ioc.fiq = 0u;
      wd1770_write(0x00u, 0x80u);
      uint64_t limit = tsc + (uint64_t)1100000u * TIMER_USEC;
      while (!(ioc.fiq & IOC_FIQ_DISC_IRQ) && tsc < limit) step_controller();
      assert((ioc.fiq & IOC_FIQ_DISC_IRQ) &&
             observed_index_pulses == 5u && wd1770_read(0x00u) == 0x10u);
      fdc_funcs = original_controller_funcs;
      wd1770_write(0x08u, 1u);
      wd_write_mode = true; wd_write_value = (uint8_t)(0x60u + timed);
      jfd_commits = 0u;
      assert(finish_command(0xa0u, 1100000u) == 0x80u &&
             transfer_count == 1024u && jfd_commits == 1u &&
             image.overlay->created[1][1][0] == wd_write_value);
      wd_write_mode = false;
      bool address_found = false;
      for (unsigned address = 0u; address < 6u; ++address)
      {
         assert(finish_command(0xc0u, 1100000u) == 0x80u &&
                transfer_count == 6u);
         if (transfer[2] == 1u) address_found = true;
      }
      assert(address_found);
      wd1770_write(0x08u, 1u);
      assert(finish_command(0x80u, 1100000u) == 0x80u &&
             transfer_count == 1024u && transfer[0] == wd_write_value);
      /* A Force Interrupt leaves the other prospective ID absent. */
      wd1770_write(0x08u, 3u);
      wd_write_mode = true;
      transfer_count = 0u; ioc.fiq = 0u;
      wd1770_write(0x00u, 0xa0u);
      limit = tsc + (uint64_t)1100000u * TIMER_USEC;
      while (transfer_count < 32u && tsc < limit) step_controller();
      assert(transfer_count == 32u);
      wd1770_write(0x00u, 0xd0u);
      wd_write_mode = false;
      assert(!image.overlay->created[1][3] && jfd_commits == 1u);
      wd1770_write(0x08u, 3u);
      assert(finish_command(0x80u, 1100000u) == 0x10u &&
             transfer_count == 0u);
      disc_close(0); arc_jfd_free(&image);
   }
}

static void test_newio_creation(const char *root)
{
   for (unsigned timed = 0u; timed < 3u; ++timed)
   {
      ArcJfdImage image = load_creation_image(root, timed == 2u ?
         "boundary.jfd" : timed ? "partial-timed.jfd" :
         "partial-untimed.jfd");
      make_jfd_creation_writable(&image);
      newio_reset(&image, false);
      uint8_t result[7];
      fdc_funcs_t observed;
      observe_indexes(&observed);
      newio_service_dma = newio_dma_write = false;
      for (unsigned address = 0u; address < 3u; ++address)
      {
         newio_start_id(1u); newio_wait_irq(500000u);
         newio_result(result);
         assert(!(result[0] & 0xc0u) && result[5] != 1u &&
                result[5] != 3u);
      }
      observed_index_pulses = 0u;
      newio_creation_command(0x06u, 1u, 0u, 1u, 1u, 3u);
      newio_wait_irq(500000u);
      assert(observed_index_pulses == 2u);
      newio_result(result);
      assert(result[0] == 0x44u && result[1] == 0x05u);
      assert_newio_refusal_quiet();
      fdc_funcs = original_controller_funcs;
      transfer_count = jfd_commits = 0u;
      newio_transfer_limit = 1024u;
      newio_service_dma = newio_dma_write = true;
      newio_write_value = (uint8_t)(0x70u + timed);
      newio_creation_command(0x05u, 1u, 0u, 1u, 1u, 3u);
      newio_wait_irq(1100000u);
      newio_result(result);
      assert(!(result[0] & 0xc0u) && transfer_count == 1024u &&
             jfd_commits == 1u &&
             image.overlay->created[1][1][0] == newio_write_value);
      newio_service_dma = false;
      c82c711_fdc_shutdown();
      c82c711_fdc_init();
      c82c711_fdc_write(2u, 0x1cu, NULL);
      c82c711_fdc_write(7u, 1u, NULL);
      bool newio_address_found = false;
      for (unsigned address = 0u; address < 6u; ++address)
      {
         newio_start_id(1u); newio_wait_irq(500000u);
         newio_result(result);
         assert(!(result[0] & 0xc0u));
         if (result[5] == 1u) newio_address_found = true;
      }
      assert(newio_address_found);
      newio_service_dma = true; newio_dma_write = false;
      transfer_count = 0u;
      newio_creation_command(0x06u, 1u, 0u, 1u, 1u, 3u);
      newio_wait_irq(1100000u);
      newio_result(result);
      assert(!(result[0] & 0xc0u) && transfer_count == 1024u &&
             transfer[0] == newio_write_value);
      newio_dma_write = true;
      for (unsigned invalid_n = 4u; invalid_n <= 7u; invalid_n += 3u)
      {
         transfer_count = 0u;
         newio_creation_command(0x05u, 1u, 0u, 1u, 3u,
                                (uint8_t)invalid_n);
         newio_wait_irq(1100000u); newio_result(result);
         assert(result[0] == 0x44u && result[1] == 0x20u &&
                result[2] == 0x20u && transfer_count <= 1u &&
                !image.overlay->created[1][3]);
         assert_newio_refusal_quiet();
      }
      newio_creation_command(0x05u, 0u, 0u, 1u, 3u, 3u);
      newio_wait_irq(1100000u); newio_result(result);
      assert(result[0] == 0x44u && result[1] == 0x20u &&
             result[2] == 0x20u && !image.overlay->created[1][3] &&
             jfd_commits == 1u);
      assert_newio_refusal_quiet();
      transfer_count = 0u;
      newio_creation_command(0x05u, 1u, 1u, 1u, 3u, 3u);
      newio_wait_irq(1100000u); newio_result(result);
      assert((result[0] & 0x40u) && !image.overlay->created[1][3] &&
             jfd_commits == 1u && transfer_count <= 1u);
      assert_newio_refusal_quiet();
      transfer_count = 0u; newio_transfer_limit = 128u;
      newio_creation_command(0x05u, 1u, 0u, 1u, 3u, 3u);
      newio_wait_irq(1100000u); newio_result(result);
      assert((result[0] & 0x40u) && !image.overlay->created[1][3] &&
             transfer_count == 128u);
      assert_newio_refusal_quiet();
      c82c711_fdc_shutdown();
      c82c711_fdc_init();
      c82c711_fdc_write(2u, 0x1cu, NULL);
      c82c711_fdc_write(7u, 1u, NULL);
      newio_service_dma = newio_dma_write = false;
      transfer_count = 0u;
      newio_creation_command(0x06u, 1u, 0u, 1u, 3u, 3u);
      newio_wait_irq(500000u); newio_result(result);
      assert(result[0] == 0x44u && result[1] == 0x05u &&
             !transfer_count && !image.overlay->created[1][3]);
      assert_newio_refusal_quiet();
      transfer_count = 0u; newio_transfer_limit = 1024u;
      newio_service_dma = newio_dma_write = true;
      jfd_fail_commit = true;
      newio_creation_command(0x05u, 1u, 0u, 1u, 3u, 3u);
      newio_wait_irq(1100000u); newio_result(result);
      assert((result[0] & 0x40u) && !image.overlay->created[1][3] &&
             jfd_commits == 1u);
      assert_newio_refusal_quiet();
      jfd_fail_commit = false;
      c82c711_fdc_shutdown(); disc_close(0); arc_jfd_free(&image);
   }
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

static void assert_newio_refusal_quiet(void)
{
   assert(!(ioc.irqb & IOC_IRQB_DISC_IRQ) &&
          !(ioc.fiq & IOC_FIQ_DISC_DATA) &&
          !timer_is_enabled(fdc_timer) &&
          c82c711_fdc_read(4u, NULL) == 0x80u);
}

static void test_newio_jfd_write(const char *root)
{
   for (unsigned code = 0u; code < 3u; ++code)
   {
      char name[32];
      snprintf(name, sizeof(name), "size-%u.jfd", code);
      ArcJfdImage sized = load_image(root, name);
      make_jfd_writable(&sized);
      newio_reset(&sized, false);
      uint8_t result[7];
      transfer_count = jfd_commits = 0u;
      newio_transfer_limit = (size_t)128u << code;
      newio_service_dma = newio_dma_write = true;
      newio_write_value = (uint8_t)(0x50u + code);
      newio_start_write(0u, (uint8_t)code, (uint8_t)code);
      newio_wait_irq(1100000u);
      newio_result(result);
      assert(!(result[0] & 0xc0u) &&
             transfer_count == ((size_t)128u << code) &&
             jfd_commits == 1u &&
             sized.overlay->data[0][0] == newio_write_value);
      c82c711_fdc_shutdown(); disc_close(0); arc_jfd_free(&sized);
   }
   static const struct { const char *name; uint8_t st1, st2; } refusals[] = {
      {"write-slow.jfd", 0x20u, 0x20u},
      {"write-duplicate.jfd", 0x20u, 0x20u},
      {"write-shared.jfd", 0x20u, 0x20u},
      {"data-crc.jfd", 0x20u, 0x20u},
      {"id-crc.jfd", 0x20u, 0u},
      {"id-only.jfd", 0x05u, 0u},
      {"blank-track.jfd", 0x05u, 0u}
   };
   for (size_t i = 0u; i < sizeof(refusals) / sizeof(refusals[0]); ++i)
   {
      ArcJfdImage refusal = load_image(root, refusals[i].name);
      make_jfd_writable(&refusal);
      newio_reset(&refusal, false);
      uint8_t refused[7];
      transfer_count = jfd_commits = 0u;
      newio_transfer_limit = 128u;
      newio_service_dma = newio_dma_write = true;
      newio_write_value = 0x90u;
      newio_start_write(0u, i < 3u ? 3u : 4u, 0u);
      newio_wait_irq(1100000u);
      newio_result(refused);
      if (!(refused[0] & 0x40u) || transfer_count > 1u || jfd_commits)
         fprintf(stderr, "82 %s st0=%02x st1=%02x st2=%02x transfer=%zu commits=%u\n",
                 refusals[i].name, refused[0], refused[1], refused[2],
                 transfer_count, jfd_commits);
      assert(refused[0] == 0x40u && refused[1] == refusals[i].st1 &&
             refused[2] == refusals[i].st2 && transfer_count <= 1u &&
             jfd_commits == 0u);
      assert_newio_refusal_quiet();
      c82c711_fdc_shutdown(); disc_close(0); arc_jfd_free(&refusal);
   }
   ArcJfdImage image = load_image(root, "size-3.jfd");
   make_jfd_writable(&image);
   newio_reset(&image, false);
   uint8_t result[7];
   transfer_count = 0u;
   newio_transfer_limit = 1024u;
   newio_service_dma = newio_dma_write = true;
   newio_write_value = 0x6bu;
   jfd_commits = 0u;
   newio_start_write(0u, 3u, 3u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert(!(result[0] & 0xc0u) && transfer_count == 1024u &&
          jfd_commits == 1u && image.overlay->data[0][0] == 0x6bu);
   newio_dma_write = false;
   transfer_count = 0u;
   newio_start_read(0u, 3u, 3u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert(!(result[0] & 0xc0u) && transfer[0] == 0x6bu);
   /* Wrong N is a synchronous refusal and must not request a DMA byte. */
   transfer_count = 0u;
   newio_dma_write = true;
   newio_start_write(0u, 3u, 2u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert((result[0] & 0x40u) && transfer_count == 0u && jfd_commits == 1u);
   assert_newio_refusal_quiet();
   for (uint8_t invalid_n = 4u; invalid_n <= 7u; invalid_n += 3u)
   {
      transfer_count = 0u;
      newio_start_write(0u, 3u, invalid_n);
      newio_wait_irq(1100000u);
      newio_result(result);
      assert(result[0] == 0x40u && result[1] == 0x20u &&
             result[2] == 0x20u && transfer_count == 0u &&
             jfd_commits == 1u && image.overlay->data[0][0] == 0x6bu &&
             !(ioc.fiq & IOC_FIQ_DISC_DATA) && !timer_is_enabled(fdc_timer));
      assert_newio_refusal_quiet();
   }
   /* Early terminal count cannot publish the incomplete sector. */
   newio_transfer_limit = 128u;
   transfer_count = 0u;
   newio_start_write(0u, 3u, 3u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert(result[0] == 0x40u && result[1] == 0x10u && result[2] == 0u &&
          transfer_count == 128u &&
          jfd_commits == 1u && image.overlay->data[0][0] == 0x6bu &&
          !(ioc.fiq & IOC_FIQ_DISC_DATA));
   assert_newio_refusal_quiet();
   transfer_count = 0u;
   newio_transfer_limit = 1024u;
   /* WRITE DELETED DATA is an invalid command on this 82C711 model. */
   c82c711_fdc_write(5u, 0x09u, NULL);
   for (unsigned poll = 0u; poll < 1000u &&
        (c82c711_fdc_read(4u, NULL) & 0xf0u) != 0xd0u; ++poll)
      step_controller();
   assert((c82c711_fdc_read(4u, NULL) & 0xf0u) == 0xd0u);
   assert(c82c711_fdc_read(5u, NULL) == 0x80u &&
          transfer_count == 0u && jfd_commits == 1u &&
          !(ioc.irqb & IOC_IRQB_DISC_IRQ) &&
          !(ioc.fiq & IOC_FIQ_DISC_DATA) &&
          !timer_is_enabled(fdc_timer) &&
          c82c711_fdc_read(4u, NULL) == 0x80u);
   newio_service_dma = false;
   transfer_count = 0u;
   newio_start_write(0u, 3u, 3u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert(result[0] == 0x40u && result[1] == 0x10u &&
          transfer_count == 0u && jfd_commits == 1u);
   assert_newio_refusal_quiet();
   newio_service_dma = true;
   newio_transfer_limit = 1024u;
   jfd_fail_commit = true;
   transfer_count = 0u;
   newio_start_write(0u, 3u, 3u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert(result[0] == 0x40u && result[1] == 0x02u &&
          jfd_commits == 1u &&
          image.overlay->failed && image.overlay->data[0][0] == 0x6bu);
   assert_newio_refusal_quiet();
   transfer_count = 0u;
   newio_start_write(0u, 3u, 3u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert(result[0] == 0x40u && result[1] == 0x02u &&
          transfer_count == 0u && jfd_commits == 1u);
   assert_newio_refusal_quiet();
   jfd_fail_commit = false;
   newio_dma_write = false;
   c82c711_fdc_shutdown(); disc_close(0); arc_jfd_free(&image);
}

static void test_newio_adf_early_tc(void)
{
   memset(raw_helper, 0xa5, ARC_LAUNCH_DISC_SIZE);
   raw_helper_length = ARC_LAUNCH_DISC_SIZE;
   newio_reset(NULL, false);
   adf_set_writable(1, 1);
   assert(disc_load_raw(1, "memory-helper.adl", ARC_GEOMETRY_640K_L));
   transfer_count = 0u;
   newio_transfer_limit = 128u;
   newio_service_dma = newio_dma_write = true;
   newio_write_value = 0x5au;
   newio_start_write(1u, 0u, 1u);
   newio_wait_irq(1100000u);
   uint8_t result[7];
   newio_result(result);
   assert(transfer_count == 128u && result[0] == 0x01u &&
          result[1] == 0u && result[2] == 0u &&
          c82c711_fdc_read(4u, NULL) == 0x80u &&
          !(ioc.fiq & IOC_FIQ_DISC_DATA) && !timer_is_enabled(fdc_timer));
   for (size_t i = 0u; i < 128u; ++i) assert(raw_helper[i] == 0x5au);
   for (size_t i = 128u; i < 256u; ++i) assert(raw_helper[i] == 0u);
   assert(raw_helper[256] == 0xa5u);
   newio_service_dma = newio_dma_write = false;
   c82c711_fdc_shutdown(); disc_close(1);
}

static void test_82c711_golden(const char *root)
{
   uint8_t result[7];
   for (unsigned code = 0u; code < 3u; ++code)
   {
      char name[32];
      snprintf(name, sizeof(name), "size-%u.jfd", code);
      ArcJfdImage family = load_image(root, name);
      newio_reset(&family, false);
      transfer_count = 0u;
      newio_transfer_limit = (size_t)128u << code;
      newio_service_dma = true;
      newio_dma_write = false;
      newio_start_read(0u, (uint8_t)code, (uint8_t)code);
      newio_wait_irq(1100000u);
      newio_result(result);
      assert(!(result[0] & 0xc0u) && transfer_count == ((size_t)128u << code) &&
             transfer[0] == (uint8_t)code);
      c82c711_fdc_shutdown();
      disc_close(0);
      arc_jfd_free(&family);
   }
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

   /* Restore drive 0 after the independent drive-isolation sequence. */
   c82c711_fdc_shutdown();
   disc_close(1);
   disc_close(0);
   newio_reset(&image, true);
   newio_service_dma = true;
   newio_dma_write = false;

   transfer_count = 0u;
   newio_transfer_limit = 1024u;
   newio_start_read(1u, 0xfeu, 1u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert((result[0] & 0x40u) && result[1] == 0x05u &&
          transfer_count == 0u && !(ioc.fiq & IOC_FIQ_DISC_DATA));

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

   ArcJfdImage header_crc = load_image(root, "id-crc.jfd");
   newio_reset(&header_crc, false);
   transfer_count = 0u;
   newio_transfer_limit = 128u;
   newio_service_dma = true;
   newio_dma_write = false;
   newio_start_read(0u, 4u, 0u);
   newio_wait_irq(1100000u);
   newio_result(result);
   assert((result[0] & 0x40u) && result[1] == 0x20u && result[2] == 0u &&
          transfer_count == 0u && !(ioc.fiq & IOC_FIQ_DISC_DATA));
   c82c711_fdc_shutdown();
   disc_close(0);
   arc_jfd_free(&header_crc);
}

static void test_82c711_raw_geometries(void)
{
   static const struct {
      ArcMediaGeometry geometry;
      size_t image_size;
      uint8_t sector_size_code;
      size_t transfer_size;
   } cases[] = {
      {ARC_GEOMETRY_320K, 327680u, 1u, 256u},
      {ARC_GEOMETRY_640K_L, 655360u, 1u, 256u},
      {ARC_GEOMETRY_800K_E, 819200u, 3u, 1024u},
      {ARC_GEOMETRY_1600K_F, 1638400u, 3u, 1024u}
   };
   for (unsigned index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
   {
      memset(raw_helper, (int)(0x40u + index), cases[index].image_size);
      raw_helper_length = cases[index].image_size;
      newio_reset(NULL, false);
      if (cases[index].geometry == ARC_GEOMETRY_1600K_F)
         c82c711_fdc_write(7u, 0u, NULL);
      adf_set_writable(0, 0);
      assert(disc_load_raw(0, "memory-helper.adl", cases[index].geometry));
      transfer_count = 0u;
      newio_transfer_limit = cases[index].transfer_size;
      newio_service_dma = true;
      newio_dma_write = false;
      newio_start_read(0u, 0u, cases[index].sector_size_code);
      newio_wait_irq(1100000u);
      uint8_t result[7];
      newio_result(result);
      assert(!(result[0] & 0xc0u) && transfer_count == cases[index].transfer_size &&
             transfer[0] == (uint8_t)(0x40u + index));
      c82c711_fdc_shutdown();
      disc_close(0);
   }
   raw_helper_length = ARC_LAUNCH_DISC_SIZE;
}

static void mixed_select(unsigned drive, uint8_t rate)
{
   c82c711_fdc_write(2u, (uint8_t)(0x3cu | drive), NULL);
   c82c711_fdc_write(7u, rate, NULL);
}

static void mixed_read_id(unsigned drive, uint8_t result[7])
{
   c82c711_fdc_write(5u, 0x0au, NULL);
   c82c711_fdc_write(5u, (uint8_t)drive, NULL);
   newio_wait_irq(400000u);
   newio_result(result);
}

static void mixed_seek_zero(unsigned drive)
{
   c82c711_fdc_write(2u, (uint8_t)(0x3cu | drive), NULL);
   c82c711_fdc_write(5u, 0x0fu, NULL);
   c82c711_fdc_write(5u, (uint8_t)drive, NULL);
   c82c711_fdc_write(5u, 0u, NULL);
   newio_wait_irq(400000u);
   c82c711_fdc_write(5u, 0x08u, NULL);
   assert((c82c711_fdc_read(4u, NULL) & 0xf0u) == 0xd0u);
   uint8_t status = c82c711_fdc_read(5u, NULL);
   uint8_t track = c82c711_fdc_read(5u, NULL);
   assert(!(status & 0xc0u) && track == 0u);
}

static void mixed_read_data(unsigned drive, uint8_t sector, uint8_t size,
                            size_t bytes, const uint8_t *expected)
{
   const uint8_t params[8] = {
      (uint8_t)drive, 0u, 0u, sector, size, sector, 0x1bu, 0xffu
   };
   transfer_count = 0u;
   newio_transfer_limit = bytes;
   newio_service_dma = true;
   newio_dma_write = false;
   c82c711_fdc_write(5u, 0x06u, NULL);
   for (unsigned i = 0u; i < sizeof(params); ++i)
      c82c711_fdc_write(5u, params[i], NULL);
   newio_wait_irq(400000u);
   uint8_t result[7];
   newio_result(result);
   assert(!(result[0] & 0xc0u) && transfer_count == bytes &&
          !memcmp(transfer, expected, bytes));
}

static void test_82c711_mixed_raw_drives(const char *root)
{
   char path[4096];
   assert(snprintf(path, sizeof(path), "%s/hd-family/valid-f.adf", root) <
          (int)sizeof(path));
   size_t image_size;
   uint8_t *image = read_all(path, &image_size);
   assert(image_size == sizeof(raw_hd));
   memcpy(raw_hd, image, image_size);
   free(image);
   raw_hd_length = image_size;
   assert(build_launch_helper());
   raw_helper_length = ARC_LAUNCH_DISC_SIZE;
   newio_reset(NULL, false);
   adf_set_writable(0, 0);
   adf_set_writable(1, 0);
   assert(disc_load_raw(0, "memory-hd.adf", ARC_GEOMETRY_1600K_F));
   assert(disc_load_raw(1, "memory-helper.adl", ARC_GEOMETRY_640K_L));

   uint8_t result[7];
   mixed_select(0u, 0u); /* HD in drive 0 at the controller's 500 kbps rate. */
   mixed_read_id(0u, result);
   assert(!(result[0] & 0xc0u) && result[5] == 0u && result[6] == 3u);
   mixed_read_data(0u, 0u, 3u, 1024u, raw_hd);

   mixed_select(1u, 0u); /* ADFS first probes the DD helper at the HD rate. */
   mixed_read_id(1u, result);
   assert((result[0] & 0x40u) && result[1] == 5u);
   mixed_select(1u, 2u);
   for (unsigned sector = 0u; sector < 10u; ++sector)
   {
      mixed_read_id(1u, result);
      assert(!(result[0] & 0xc0u) && result[5] == sector && result[6] == 1u);
   }
   mixed_read_data(1u, 0u, 1u, 256u, raw_helper);

   mixed_select(0u, 0u);
   mixed_seek_zero(0u);
   mixed_read_id(0u, result);
   assert(!(result[0] & 0xc0u) && result[5] == 1u && result[6] == 3u);
   mixed_read_data(0u, 3u, 3u, 1024u, raw_hd + 3072u);

   mixed_seek_zero(0u);
   mixed_select(0u, 2u); /* The reverse, DD probe of HD media, also recovers. */
   mixed_read_id(0u, result);
   assert((result[0] & 0x40u) && result[1] == 5u);
   mixed_select(1u, 2u);
   mixed_seek_zero(1u);
   mixed_read_id(1u, result);
   assert(!(result[0] & 0xc0u) && result[5] == 10u && result[6] == 1u);
   mixed_read_data(1u, 0u, 1u, 256u, raw_helper);
   mixed_select(0u, 0u);
   mixed_seek_zero(0u);
   mixed_read_id(0u, result);
   assert(!(result[0] & 0xc0u) && result[5] == 2u && result[6] == 3u);
   mixed_read_data(0u, 0u, 3u, 1024u, raw_hd);

   c82c711_fdc_shutdown();
   disc_close(1);
   disc_close(0);
}

static void test_raw_index_cadence(void)
{
   raw_helper_length = 1638400u;
   newio_reset(NULL, false);
   assert(disc_load_raw(0, "memory-helper.adl", ARC_GEOMETRY_1600K_F));
   fdc_funcs_t observed;
   observe_indexes(&observed);
   for (unsigned density = 1u; density <= 3u; ++density)
   {
      disc_set_density((int)density);
      unsigned period = disc_poll_period_us();
      observed_index_pulses = 0u;
      for (unsigned i = 0u; i < 200000u / period; ++i)
         drive_funcs[0]->poll();
      assert(observed_index_pulses == 1u);
   }
   disc_set_density(1);
   observed_index_pulses = 0u;
   for (unsigned i = 0u; i < 100000u / 32u; ++i)
      drive_funcs[0]->poll();
   assert(observed_index_pulses == 0u);
   disc_set_density(2);
   for (unsigned i = 0u; i < 100000u / 16u; ++i)
      drive_funcs[0]->poll();
   assert(observed_index_pulses == 1u);
   fdc_funcs = original_controller_funcs;
   c82c711_fdc_shutdown();
   disc_close(0);
   raw_helper_length = ARC_LAUNCH_DISC_SIZE;
}

int main(int argc, char **argv)
{
   bool run_wd1770 = true;
   bool run_82c711 = true;
   if (argc == 3 && !strcmp(argv[2], "--adf-tc"))
   {
      test_newio_adf_early_tc();
      puts("jfd_controller_tests: ADF early TC passed");
      return 0;
   }
   if (argc == 3)
   {
      run_wd1770 = !strcmp(argv[2], "--wd1770");
      run_82c711 = !strcmp(argv[2], "--82c711");
      if (!run_wd1770 && !run_82c711)
      {
         fprintf(stderr, "Usage: %s FIXTURES [--wd1770|--82c711]\n", argv[0]);
         return 2;
      }
   }
   else if (argc != 2)
   {
      fprintf(stderr, "Usage: %s FIXTURES [--wd1770|--82c711]\n", argv[0]);
      return 2;
   }
   if (run_wd1770)
   {
      test_family(argv[1]);
      test_rejections(argv[1]);
      test_empty_drive_cancellation();
      test_busy_jfd_read_is_not_replaced(argv[1]);
      test_drive_one_helper(argv[1]);
      test_wd_jfd_write(argv[1]);
      test_wd_creation(argv[1]);
   }
   if (run_82c711)
   {
      test_82c711_hardening();
      test_82c711_golden(argv[1]);
      test_82c711_quad(argv[1]);
      test_82c711_jfd_search_failures(argv[1]);
      test_82c711_search_cancellation(argv[1]);
      test_82c711_raw_geometries();
      test_82c711_mixed_raw_drives(argv[1]);
      test_raw_index_cadence();
      test_newio_jfd_write(argv[1]);
      test_newio_creation(argv[1]);
      test_newio_adf_early_tc();
   }
   puts("jfd_controller_tests: all checks passed");
   return 0;
}
