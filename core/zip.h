/* ArcDroid bounded ZIP reader. GPL-2.0-only. */
#ifndef ARCDROID_ZIP_H
#define ARCDROID_ZIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  size_t archive_bytes;
  unsigned entries;
  size_t member_bytes;
} ArcZipLimits;

typedef enum {
  ARC_ZIP_MEMBER_OK = 0,
  ARC_ZIP_MEMBER_UNSUPPORTED_METHOD,
  ARC_ZIP_MEMBER_TOO_LARGE
} ArcZipMemberStatus;

typedef struct {
  char name[256];
  uint16_t flags;
  uint16_t method;
  uint16_t version_made;
  uint32_t external_attributes;
  uint32_t crc32;
  uint32_t packed_size;
  uint32_t size;
  uint32_t local_offset;
  uint32_t data_offset;
  uint32_t record_end;
  const uint8_t *central_extra;
  uint16_t central_extra_size;
  ArcZipMemberStatus status;
} ArcZipEntry;

typedef struct {
  ArcZipEntry *entries;
  unsigned count;
  uint32_t central_offset;
} ArcZipArchive;

bool arc_zip_index(const uint8_t *archive, size_t archive_size,
                   const ArcZipLimits *limits, ArcZipArchive *index,
                   char *error, size_t error_size);
void arc_zip_free(ArcZipArchive *index);
bool arc_zip_extract(const uint8_t *archive, size_t archive_size,
                     const ArcZipEntry *entry, uint8_t **data,
                     size_t *size, char *error, size_t error_size);
const char *arc_zip_member_status_reason(ArcZipMemberStatus status);

#if defined(ARCDROID_TEST_FAULTS)
void arc_zip_test_fail_member_allocations(unsigned count);
#endif

#endif
