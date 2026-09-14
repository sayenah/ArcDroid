/* Local-only firmware identification probe. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
   if (argc < 2) return 2;
   int failures = 0;
   for (int arg = 1; arg < argc; ++arg)
   {
      FILE *file = fopen(argv[arg], "rb");
      if (!file || fseek(file, 0, SEEK_END)) { if (file) fclose(file); ++failures; continue; }
      long length = ftell(file);
      if (length < 0 || fseek(file, 0, SEEK_SET)) { fclose(file); ++failures; continue; }
      uint8_t *data = malloc(length ? (size_t)length : 1u);
      if (!data || fread(data, 1u, (size_t)length, file) != (size_t)length)
      { free(data); fclose(file); ++failures; continue; }
      fclose(file);
      ArcDigest digest = arc_digest(data, (size_t)length);
      free(data);
      const ArcFirmwareFingerprint *match = arc_firmware_match(&digest);
      if (match)
         printf("recognised %s size=%zu crc32=%08x sha1=%s recipe=%u lane=%u\n",
                arc_os_name(match->os), digest.size, digest.crc32, digest.sha1,
                (unsigned)match->recipe, match->lane);
      else
      {
         size_t nearest = 0u;
         (void)arc_firmware_nearest_size(digest.size, 4096u, &nearest);
         printf("unrecognised size=%zu crc32=%08x sha1=%s nearest-size=%zu\n",
                digest.size, digest.crc32, digest.sha1, nearest);
         ++failures;
      }
   }
   return failures ? 1 : 0;
}
