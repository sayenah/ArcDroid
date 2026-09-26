/* ArcDroid deterministic JFD engine-reader tests. GPL-2.0-only. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "disc.h"
#include "disc_jfd.h"

disc_funcs_t *drive_funcs[4];
fdc_funcs_t *fdc_funcs;
void *fdc_p;
int writeprot[4];
static unsigned test_period_us = 32u;
unsigned disc_poll_period_us(void) { return test_period_us; }

static unsigned data_count, finish_count, notfound_count, data_crc_count;
static unsigned header_crc_count, write_protect_count, index_count;
static bool cancel_on_data;
static uint8_t received[2048];
static uint8_t offered[1024], committed[1024];
static size_t offer_size, offer_at, commit_count;
static bool offer_enabled, commit_allowed;

void rpclog(const char *format, ...)
{
   (void)format;
}

static void on_data(uint8_t value, void *opaque)
{
   (void)opaque;
   assert(data_count < sizeof(received));
   received[data_count++] = value;
   if (cancel_on_data) drive_funcs[0]->stop();
}
static void on_void(void *opaque) { (void)opaque; }
static void on_finish(void *opaque) { (void)opaque; ++finish_count; }
static void on_notfound(void *opaque) { (void)opaque; ++notfound_count; }
static void on_data_crc(void *opaque) { (void)opaque; ++data_crc_count; }
static void on_header_crc(void *opaque) { (void)opaque; ++header_crc_count; }
static void on_write_protect(void *opaque) { (void)opaque; ++write_protect_count; }
static int on_get_data(int last, void *opaque)
{
   (void)last; (void)opaque;
   if (!offer_enabled || offer_at == offer_size) return -1;
   return offered[offer_at++];
}
static bool on_commit(ArcJfdImage *image, size_t index, const uint8_t *data)
{
   if (!commit_allowed) return false;
   assert(index == 0u);
   memcpy(committed, data, image->sectors[index].size);
   image->overlay->data[index] = committed;
   ++commit_count;
   return true;
}
static void on_sector_id(uint8_t track, uint8_t side, uint8_t sector,
                         uint8_t size, uint8_t crc1, uint8_t crc2, void *opaque)
{
   (void)track; (void)side; (void)sector; (void)size;
   (void)crc1; (void)crc2; (void)opaque;
}
static void on_index(void *opaque) { (void)opaque; ++index_count; }

static fdc_funcs_t callbacks = {
   on_data, on_void, on_finish, on_notfound, on_data_crc, on_header_crc,
   on_write_protect, on_get_data, NULL, on_index
};

static void reset_results(void)
{
   data_count = finish_count = notfound_count = data_crc_count = 0u;
   header_crc_count = write_protect_count = index_count = 0u;
   cancel_on_data = false;
   memset(received, 0, sizeof(received));
}

static ArcJfdImage make_image(uint8_t *decoded, ArcJfdSector *sectors,
                              unsigned count)
{
   ArcJfdImage image;
   memset(&image, 0, sizeof(image));
   image.decoded = decoded;
   image.decoded_size = 2048u;
   image.sectors = sectors;
   image.sector_count = count;
   image.track_count = 2u;
   image.tracks[0].first_sector = 0u;
   image.tracks[0].sector_count = (uint16_t)count;
   image.tracks[0].timed = true;
   return image;
}

static void poll_count(unsigned count)
{
   for (unsigned i = 0u; i < count; ++i) drive_funcs[0]->poll();
}

static void test_transfer_results(void)
{
   uint8_t decoded[2048];
   for (unsigned i = 0u; i < sizeof(decoded); ++i) decoded[i] = (uint8_t)i;
   ArcJfdSector sector = {0u, 256u, 128u, 4u, 2u, 0u};
   ArcJfdImage image = make_image(decoded, &sector, 1u);
   assert(jfd_load(0, &image) && writeprot[0]);

   reset_results();
   drive_funcs[0]->readsector(0, 4, 0, 0, 1);
   poll_count(130u);
   assert(data_count == 128u && finish_count == 1u &&
          !data_crc_count && !header_crc_count);
   assert(!memcmp(received, decoded + 256u, 128u));
   poll_count(6250u);
   assert(finish_count == 1u && !data_crc_count && !header_crc_count);

   jfd_motor_start();
   sector.flags = ARC_JFD_DATA_CRC_BAD;
   reset_results();
   drive_funcs[0]->readsector(0, 4, 0, 0, 1);
   poll_count(130u);
   assert(data_count == 128u && data_crc_count == 1u && !finish_count);

   jfd_motor_start();
   sector.flags = ARC_JFD_ID_CRC_BAD | ARC_JFD_DATA_CRC_BAD;
   reset_results();
   drive_funcs[0]->readsector(0, 4, 0, 0, 1);
   poll_count(2u);
   assert(header_crc_count == 1u && !data_count && !data_crc_count && !finish_count);

   jfd_motor_start();
   sector.flags = 0u;
   reset_results();
   drive_funcs[0]->readaddress(0, 0, 0, 1);
   poll_count(8u);
   assert(data_count == 6u && finish_count == 1u);
   assert(received[0] == 0u && received[1] == 0u &&
          received[2] == 4u && received[3] == 0u);

   jfd_motor_start();
   reset_results();
   cancel_on_data = true;
   drive_funcs[0]->readsector(0, 4, 0, 0, 1);
   poll_count(300u);
   assert(data_count == 1u && !finish_count && !data_crc_count);

   reset_results();
   drive_funcs[0]->writesector(0, 4, 0, 0, 1, -1, 0);
   assert(write_protect_count == 1u);
   drive_funcs[0]->format(0, 0, 0, 1);
   assert(write_protect_count == 2u);
   drive_funcs[0]->close(0);
}

static void test_rotation_and_search(void)
{
   uint8_t decoded[2048] = {0};
   decoded[300] = 0x11u;
   decoded[500] = 0x22u;
   ArcJfdSector sectors[2] = {
      {1000u, 300u, 128u, 7u, 2u, 0u},
      {2000u, 500u, 128u, 7u, 2u, 0u}
   };
   ArcJfdImage image = make_image(decoded, sectors, 2u);
   assert(jfd_load(0, &image));

   poll_count(46u); /* phase 1472 us: the next duplicate is at 2000 us. */
   reset_results();
   drive_funcs[0]->readsector(0, 7, 0, 0, 1);
   poll_count(150u);
   assert(data_count == 128u && received[0] == 0x22u && finish_count == 1u);

   jfd_motor_start();
   sectors[0].time_us = sectors[1].time_us = 0u;
   reset_results();
   drive_funcs[0]->readsector(0, 7, 0, 0, 1);
   poll_count(130u);
   assert(data_count == 128u && received[0] == 0x11u && finish_count == 1u);

   jfd_motor_start();
   sectors[0].data_offset = UINT32_MAX;
   sectors[0].id = 9u;
   sectors[1].id = 10u;
   reset_results();
   drive_funcs[0]->readsector(0, 9, 0, 0, 1);
   poll_count(31251u);
   assert(index_count == 5u && !data_count && !finish_count &&
          !header_crc_count && !data_crc_count && !notfound_count);
   drive_funcs[0]->stop();

   image.tracks[0].sector_count = 0u;
   jfd_motor_start();
   reset_results();
   drive_funcs[0]->readsector(0, 9, 0, 0, 1);
   poll_count(31251u);
   assert(index_count == 5u && !data_count && !finish_count);
   drive_funcs[0]->close(0);
}

static void test_index_cadence(void)
{
   uint8_t decoded[2048] = {0};
   ArcJfdSector sector = {0u, 0u, 128u, 0u, 2u, 0u};
   ArcJfdImage image = make_image(decoded, &sector, 1u);
   assert(jfd_load(0, &image));
   for (unsigned period = 32u; period >= 8u; period /= 2u)
   {
      test_period_us = period;
      jfd_motor_start();
      reset_results();
      poll_count(200000u / period);
      assert(index_count == 1u);
   }
   test_period_us = 32u;
   jfd_motor_start();
   reset_results();
   poll_count(100000u / 32u);
   assert(index_count == 0u);
   test_period_us = 16u;
   poll_count(100000u / 16u);
   assert(index_count == 1u);
   drive_funcs[0]->close(0);
   test_period_us = 32u;
}

static void test_writable_reader(void)
{
   uint8_t decoded[2048] = {0};
   ArcJfdSector sector = {0u, 256u, 128u, 4u, 2u, 0u};
   ArcJfdImage image = make_image(decoded, &sector, 1u);
   uint8_t *replacements[1] = {NULL};
   ArcJfdOverlay overlay = {0};
   overlay.data = replacements;
   overlay.writable = overlay.visible = true;
   overlay.commit = on_commit;
   image.overlay = &overlay;
   offer_enabled = commit_allowed = true;
   commit_count = offer_at = 0u;
   offer_size = 128u;
   memset(offered, 0xa5, offer_size);
   assert(jfd_load(0, &image) && !writeprot[0]);
   reset_results();
   drive_funcs[0]->writesector(0, 4, 0, 0, 1, -1, 0);
   poll_count(140u);
   assert(commit_count == 1u && finish_count == 1u &&
          !write_protect_count && offer_at == offer_size);
   jfd_motor_start();
   reset_results();
   drive_funcs[0]->readsector(0, 4, 0, 0, 1);
   poll_count(140u);
   assert(data_count == 128u && received[0] == 0xa5u);

   offer_at = 0u;
   memset(offered, 0x3cu, offer_size);
   jfd_motor_start(); reset_results();
   drive_funcs[0]->writesector(0, 4, 0, 0, 1, -1, 0);
   poll_count(20u);
   assert(offer_at > 0u && offer_at < offer_size && commit_count == 1u);
   drive_funcs[0]->stop(); /* Force Interrupt / reset cancels partial data. */
   poll_count(140u);
   assert(commit_count == 1u);
   drive_funcs[0]->close(0); /* Eject has the same discard rule. */
   assert(jfd_load(0, &image));
   jfd_motor_start(); reset_results();
   drive_funcs[0]->readsector(0, 4, 0, 0, 1);
   poll_count(140u);
   assert(received[0] == 0xa5u && commit_count == 1u);
   offer_at = 0u;
   jfd_motor_start(); reset_results();
   drive_funcs[0]->writesector(0, 4, 0, 0, 1, -1, 0);
   poll_count(20u);
   assert(offer_at > 0u && offer_at < offer_size && commit_count == 1u);
   jfd_init(); /* Controller reset removes the in-flight transfer. */
   assert(jfd_load(0, &image));
   poll_count(140u);
   assert(commit_count == 1u);

   /* A complete buffer cannot escape before the commit callback succeeds. */
   commit_allowed = false;
   offer_at = 0u;
   jfd_motor_start();
   reset_results();
   drive_funcs[0]->writesector(0, 4, 0, 0, 1, -1, 0);
   poll_count(140u);
   assert(commit_count == 1u && write_protect_count == 1u && !finish_count);
   commit_allowed = true;
   sector.flags = ARC_JFD_DATA_CRC_BAD;
   jfd_motor_start(); reset_results();
   drive_funcs[0]->writesector(0, 4, 0, 0, 1, -1, 0);
   poll_count(5u);
   assert(data_crc_count == 1u && commit_count == 1u);
   sector.flags = ARC_JFD_ID_CRC_BAD;
   jfd_motor_start(); reset_results();
   drive_funcs[0]->writesector(0, 4, 0, 0, 1, -1, 0);
   poll_count(5u);
   assert(header_crc_count == 1u && commit_count == 1u);
   sector.flags = 0u;
   jfd_motor_start(); reset_results();
   drive_funcs[0]->writesector(0, 4, 0, 0, 1, 256, 0);
   poll_count(5u);
   assert(data_crc_count == 1u && commit_count == 1u);
   jfd_motor_start(); reset_results();
   drive_funcs[0]->writesector(0, 4, 0, 0, 1, -1, 1);
   assert(write_protect_count == 1u && commit_count == 1u);
   offer_enabled = false;
   jfd_motor_start(); reset_results();
   drive_funcs[0]->writesector(0, 4, 0, 0, 1, -1, 0);
   poll_count(20u);
   assert(!finish_count && commit_count == 1u && write_protect_count == 1u);
   reset_results();
   drive_funcs[0]->format(0, 0, 0, 1);
   assert(write_protect_count == 1u && commit_count == 1u);
   drive_funcs[0]->close(0);
}

static void test_write_sizes_and_density(void)
{
   for (unsigned density_case = 0u; density_case < 3u; ++density_case)
      for (unsigned size_code = 0u; size_code < 4u; ++size_code)
      {
         uint8_t decoded[2048] = {0};
         unsigned size = 128u << size_code;
         unsigned density = density_case == 0u ? 1u :
                            density_case == 1u ? 2u : 4u;
         ArcJfdSector sector = {density_case & 1u ? 1000u : 0u,
                                256u, (uint16_t)size, 6u,
                                (uint8_t)density, 0u};
         ArcJfdImage image = make_image(decoded, &sector, 1u);
         uint8_t *replacements[1] = {NULL};
         ArcJfdOverlay overlay = {0};
         overlay.data = replacements;
         overlay.writable = overlay.visible = true;
         overlay.commit = on_commit;
         image.overlay = &overlay;
         callbacks.sectorid = density_case == 0u ? NULL : on_sector_id;
         int fdc_density = density_case == 0u ? 0 : density_case == 1u ? 1 : 2;
         offer_enabled = commit_allowed = true;
         offer_at = commit_count = 0u;
         offer_size = size;
         memset(offered, (int)(0x40u + size_code + density_case * 4u), size);
         assert(jfd_load(0, &image) && !writeprot[0]);
         reset_results();
         drive_funcs[0]->writesector(0, 6, 0, 0, fdc_density,
                                     (int)size, 0);
         poll_count(1200u);
         assert(commit_count == 1u && finish_count == 1u &&
                offer_at == size && !memcmp(committed, offered, size));
         jfd_motor_start(); reset_results();
         drive_funcs[0]->readsector(0, 6, 0, 0, fdc_density);
         poll_count(1200u);
         assert(data_count == size && finish_count == 1u &&
                !memcmp(received, offered, size));
         drive_funcs[0]->close(0);
      }
   callbacks.sectorid = on_sector_id;
}

int main(void)
{
   fdc_funcs = &callbacks;
   jfd_init();
   test_transfer_results();
   test_rotation_and_search();
   test_index_cadence();
   test_writable_reader();
   test_write_sizes_and_density();
   puts("jfd_reader_tests: all checks passed");
   return 0;
}
