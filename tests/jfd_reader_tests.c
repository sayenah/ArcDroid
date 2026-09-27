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

static uint8_t ident_checksum(const uint8_t *block)
{
   unsigned checksum = 0xffu;
   for (unsigned index = 255u; index-- > 0u;)
   {
      if (checksum > 0xffu) checksum = (checksum + 1u) & 0xffu;
      checksum += block[index];
   }
   return (uint8_t)checksum;
}

static void ident_u24(uint8_t *p, unsigned value)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8u);
   p[2] = (uint8_t)(value >> 16u);
}

static void ident_map(uint8_t *data, unsigned first, unsigned length,
                      unsigned disc_sectors)
{
   memset(data, 0, 512u);
   ident_u24(data, first);
   ident_u24(data + 256u, length);
   ident_u24(data + 252u, disc_sectors);
   data[510] = 3u;
   data[255] = ident_checksum(data);
   data[511] = ident_checksum(data + 256u);
}

static void test_identification(void)
{
   uint8_t decoded[3072] = {0};
   ArcJfdSector sectors[3] = {
      {0u, 0u, 1024u, 0u, 2u, 0u},
      {40000u, 1024u, 1024u, 1u, 2u, 0u},
      {80000u, 2048u, 1024u, 2u, 2u, 0u}
   };
   ArcJfdImage image = make_image(decoded, sectors, 3u);
   image.decoded_size = sizeof(decoded);
   ident_map(decoded, 12u, 3188u, 3200u);
   arc_jfd_prepare_identification(&image, true);
   assert(!strcmp(image.identification_reason, "presented"));
   assert(image.identification_mask == 0x18u &&
          image.identification_schedule == 5u &&
          image.identification_time[3] == 116352u &&
          image.identification_time[4] == 152704u);
   ArcJfdView view;
   assert(arc_jfd_effective_sector(&image, 0u, 6u, false, &view) &&
          view.id_visible && !view.data && !view.write_eligible &&
          view.sector.id == 3u && view.sector.size == 1024u &&
          view.sector.density == 2u);
   assert(jfd_load(0, &image));
   callbacks.sectorid = NULL;
   jfd_motor_start(); reset_results();
   drive_funcs[0]->readsector(0, 3, 0, 0, 1);
   poll_count(31251u);
   assert(index_count == 5u && data_count == 0u && finish_count == 0u);
   drive_funcs[0]->stop();
   callbacks.sectorid = on_sector_id;
   jfd_motor_start(); reset_results();
   drive_funcs[0]->readsector(0, 3, 0, 0, 1);
   poll_count(12501u);
   assert(index_count == 2u && notfound_count == 1u &&
          data_count == 0u && finish_count == 0u);
   drive_funcs[0]->close(0);

   ident_map(decoded, 13u, 3187u, 3200u);
   arc_jfd_prepare_identification(&image, true);
   assert(!strcmp(image.identification_reason, "not-free") &&
          !image.identification_mask);
   ident_map(decoded, 12u, 3188u, 3200u);
   ident_u24(decoded + 256u, 2u);
   ident_u24(decoded + 3u, 14u);
   ident_u24(decoded + 259u, 3186u);
   decoded[510] = 6u;
   decoded[255] = ident_checksum(decoded);
   decoded[511] = ident_checksum(decoded + 256u);
   arc_jfd_prepare_identification(&image, true);
   assert(!strcmp(image.identification_reason, "presented"));

   sectors[1].flags = ARC_JFD_DATA_CRC_BAD;
   arc_jfd_prepare_identification(&image, true);
   assert(!strcmp(image.identification_reason, "irregular"));
   sectors[1].flags = 0u;
   sectors[1].time_us = sectors[0].time_us;
   arc_jfd_prepare_identification(&image, true);
   assert(!strcmp(image.identification_reason, "irregular"));
   sectors[1].time_us = 40000u;
   ident_map(decoded, 12u, 3188u, 3201u);
   arc_jfd_prepare_identification(&image, true);
   assert(!strcmp(image.identification_reason, "map-invalid"));
   ident_map(decoded, 12u, 3188u, 3200u);
   decoded[255] ^= 1u;
   arc_jfd_prepare_identification(&image, true);
   assert(!strcmp(image.identification_reason, "map-invalid"));
   ident_map(decoded, 12u, 0u, 3200u);
   arc_jfd_prepare_identification(&image, true);
   assert(!strcmp(image.identification_reason, "map-invalid"));
   ident_map(decoded, 12u, 2u, 3200u);
   ident_u24(decoded + 3u, 13u);
   ident_u24(decoded + 259u, 3187u);
   decoded[510] = 6u;
   decoded[255] = ident_checksum(decoded);
   decoded[511] = ident_checksum(decoded + 256u);
   arc_jfd_prepare_identification(&image, true);
   assert(!strcmp(image.identification_reason, "map-invalid"));
   ident_map(decoded, 12u, 3188u, 3200u);
   arc_jfd_prepare_identification(&image, false);
   assert(!strcmp(image.identification_reason, "not-native") &&
          !image.identification_mask);
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
   test_identification();
   puts("jfd_reader_tests: all checks passed");
   return 0;
}
