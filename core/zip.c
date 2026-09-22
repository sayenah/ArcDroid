/* Hardened, bounded ZIP reader for project-owned consumers. GPL-2.0-only. */
#include "zip.h"
#include "arcdroid_core.h"
#include "zlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  ZIP_LOCAL_HEADER = 30u,
  ZIP_CENTRAL_HEADER = 46u,
  ZIP_END_RECORD = 22u,
  ZIP_MAX_COMMENT = 65535u
};

static uint16_t le16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static void fail(char *error, size_t error_size, const char *reason) {
  if (error && error_size)
    snprintf(error, error_size, "%s", reason);
}

static bool safe_name(const uint8_t *name, size_t size) {
  if (!size || name[0] == '/' || name[0] == '\\' ||
      memchr(name, '\\', size) || memchr(name, '\0', size))
    return false;
  size_t component = 0u;
  for (size_t i = 0u; i <= size; ++i) {
    if (i == size || name[i] == '/') {
      size_t length = i - component;
      if (!length && i == size && i && name[i - 1u] == '/')
        break;
      if (!length || (length == 1u && name[component] == '.') ||
          (length == 2u && name[component] == '.' &&
           name[component + 1u] == '.'))
        return false;
      component = i + 1u;
    }
  }
  return true;
}

static bool extra_valid(const uint8_t *extra, size_t size, bool *zip64) {
  if (zip64)
    *zip64 = false;
  while (size) {
    if (size < 4u)
      return false;
    uint16_t id = le16(extra), length = le16(extra + 2u);
    extra += 4u;
    size -= 4u;
    if (length > size)
      return false;
    if (zip64 && id == 1u)
      *zip64 = true;
    extra += length;
    size -= length;
  }
  return true;
}

static bool extra_find(const uint8_t *extra, size_t size, uint16_t wanted,
                       const uint8_t **payload, uint16_t *payload_size) {
  while (size >= 4u) {
    uint16_t id = le16(extra), length = le16(extra + 2u);
    extra += 4u;
    size -= 4u;
    if (length > size)
      return false;
    if (id == wanted) {
      if (payload)
        *payload = extra;
      if (payload_size)
        *payload_size = length;
      return true;
    }
    extra += length;
    size -= length;
  }
  return false;
}

static bool consumed_extra_matches(const uint8_t *central, size_t central_size,
                                   const uint8_t *local, size_t local_size) {
  const uint8_t *cp = NULL, *lp = NULL;
  uint16_t cn = 0u, ln = 0u;
  bool ch = extra_find(central, central_size, 0x4341u, &cp, &cn);
  bool lh = extra_find(local, local_size, 0x4341u, &lp, &ln);
  return !ch || !lh || (cn == ln && !memcmp(cp, lp, cn));
}

static bool find_end(const uint8_t *archive, size_t size, size_t *end) {
  if (size < ZIP_END_RECORD)
    return false;
  size_t low = size > ZIP_END_RECORD + ZIP_MAX_COMMENT
                   ? size - ZIP_END_RECORD - ZIP_MAX_COMMENT
                   : 0u;
  for (size_t at = size - ZIP_END_RECORD;; --at) {
    if (le32(archive + at) == 0x06054b50u) {
      uint16_t comment = le16(archive + at + 20u);
      if (at + ZIP_END_RECORD + comment == size) {
        *end = at;
        return true;
      }
    }
    if (at == low)
      break;
  }
  return false;
}

static bool ranges_overlap(uint32_t a_start, uint32_t a_end,
                           uint32_t b_start, uint32_t b_end) {
  return a_start < b_end && b_start < a_end;
}

static bool validate_local(const uint8_t *archive, size_t size,
                           uint32_t central_offset, ArcZipEntry *entry,
                           char *error, size_t error_size) {
  size_t at = entry->local_offset;
  if (at > size || size - at < ZIP_LOCAL_HEADER ||
      le32(archive + at) != 0x04034b50u) {
    fail(error, error_size, "local header is missing");
    return false;
  }
  uint16_t flags = le16(archive + at + 6u);
  uint16_t method = le16(archive + at + 8u);
  uint32_t crc = le32(archive + at + 14u);
  uint32_t packed = le32(archive + at + 18u);
  uint32_t unpacked = le32(archive + at + 22u);
  uint16_t name_size = le16(archive + at + 26u);
  uint16_t extra_size = le16(archive + at + 28u);
  size_t variable = (size_t)name_size + extra_size;
  if (variable > size - at - ZIP_LOCAL_HEADER ||
      strlen(entry->name) != name_size ||
      memcmp(archive + at + ZIP_LOCAL_HEADER, entry->name, name_size) ||
      flags != entry->flags || method != entry->method ||
      (!(flags & 8u) &&
       (crc != entry->crc32 || packed != entry->packed_size ||
        unpacked != entry->size)) ||
      ((flags & 8u) &&
       ((crc && crc != entry->crc32) ||
        (packed && packed != entry->packed_size) ||
        (unpacked && unpacked != entry->size)))) {
    fail(error, error_size, "local/central header mismatch");
    return false;
  }
  const uint8_t *local_extra =
      archive + at + ZIP_LOCAL_HEADER + name_size;
  bool zip64 = false;
  if (!extra_valid(local_extra, extra_size, &zip64)) {
    fail(error, error_size, "malformed local extra field");
    return false;
  }
  if (zip64) {
    fail(error, error_size, "zip64 archive is unsupported");
    return false;
  }
  if (!consumed_extra_matches(entry->central_extra,
                              entry->central_extra_size, local_extra,
                              extra_size)) {
    fail(error, error_size, "conflicting ARC0 metadata");
    return false;
  }
  size_t data_at = at + ZIP_LOCAL_HEADER + variable;
  if (data_at > central_offset || entry->packed_size > central_offset - data_at) {
    fail(error, error_size, "member data overlaps the central directory");
    return false;
  }
  size_t record_end = data_at + entry->packed_size;
  if (flags & 8u) {
    if (record_end > central_offset || central_offset - record_end < 12u) {
      fail(error, error_size, "truncated data descriptor");
      return false;
    }
    size_t descriptor = record_end;
    if (le32(archive + descriptor) == 0x08074b50u)
      descriptor += 4u;
    if (descriptor > central_offset || central_offset - descriptor < 12u) {
      fail(error, error_size, "truncated data descriptor");
      return false;
    }
    if (le32(archive + descriptor) != entry->crc32 ||
        le32(archive + descriptor + 4u) != entry->packed_size ||
        le32(archive + descriptor + 8u) != entry->size) {
      fail(error, error_size, "data descriptor mismatch");
      return false;
    }
    record_end = descriptor + 12u;
  }
  if (record_end > central_offset) {
    fail(error, error_size, "member data overlaps the central directory");
    return false;
  }
  entry->data_offset = (uint32_t)data_at;
  entry->record_end = (uint32_t)record_end;
  return true;
}

bool arc_zip_index(const uint8_t *archive, size_t archive_size,
                   const ArcZipLimits *limits, ArcZipArchive *index,
                   char *error, size_t error_size) {
  if (index)
    memset(index, 0, sizeof(*index));
  if (!archive || !limits || !index) {
    fail(error, error_size, "invalid archive input");
    return false;
  }
  if (archive_size > limits->archive_bytes) {
    fail(error, error_size, "archive exceeds configured size limit");
    return false;
  }
  size_t end = 0u;
  if (!find_end(archive, archive_size, &end)) {
    fail(error, error_size,
         archive_size < ZIP_END_RECORD ? "truncated archive"
                                       : "missing central directory");
    return false;
  }
  uint16_t disk = le16(archive + end + 4u);
  uint16_t central_disk = le16(archive + end + 6u);
  uint16_t disk_entries = le16(archive + end + 8u);
  uint16_t total = le16(archive + end + 10u);
  uint32_t central_size = le32(archive + end + 12u);
  uint32_t central_offset = le32(archive + end + 16u);
  if (total == 0xffffu || disk_entries == 0xffffu ||
      central_size == 0xffffffffu || central_offset == 0xffffffffu) {
    fail(error, error_size, "zip64 archive is unsupported");
    return false;
  }
  if (disk || central_disk || disk_entries != total) {
    fail(error, error_size, "multi-disk archive is unsupported");
    return false;
  }
  if (total > limits->entries) {
    char message[80];
    snprintf(message, sizeof(message), "archive has more than %u entries",
             limits->entries);
    fail(error, error_size, message);
    return false;
  }
  if ((size_t)central_offset > end ||
      (size_t)central_size > end - (size_t)central_offset) {
    fail(error, error_size, "central directory is out of bounds");
    return false;
  }
  if ((size_t)central_size != end - (size_t)central_offset) {
    fail(error, error_size, "central directory size mismatch");
    return false;
  }
  ArcZipEntry *entries = calloc(total ? total : 1u, sizeof(*entries));
  if (!entries) {
    fail(error, error_size, "out of memory");
    return false;
  }
  size_t at = central_offset;
  for (unsigned i = 0u; i < total; ++i) {
    ArcZipEntry *entry = &entries[i];
    if (at > end || end - at < ZIP_CENTRAL_HEADER ||
        le32(archive + at) != 0x02014b50u) {
      fail(error, error_size, "truncated central entry");
      goto reject;
    }
    uint16_t name_size = le16(archive + at + 28u);
    uint16_t extra_size = le16(archive + at + 30u);
    uint16_t comment_size = le16(archive + at + 32u);
    size_t variable = (size_t)name_size + extra_size + comment_size;
    if (!name_size || name_size >= sizeof(entry->name) ||
        variable > end - at - ZIP_CENTRAL_HEADER) {
      fail(error, error_size, "invalid central entry length");
      goto reject;
    }
    const uint8_t *name = archive + at + ZIP_CENTRAL_HEADER;
    if (!safe_name(name, name_size)) {
      fail(error, error_size, "unsafe archive name");
      goto reject;
    }
    memcpy(entry->name, name, name_size);
    entry->name[name_size] = '\0';
    for (unsigned previous = 0u; previous < i; ++previous)
      if (!strcmp(entries[previous].name, entry->name)) {
        fail(error, error_size, "duplicate archive member name");
        goto reject;
      }
    entry->version_made = le16(archive + at + 4u);
    entry->flags = le16(archive + at + 8u);
    entry->method = le16(archive + at + 10u);
    entry->crc32 = le32(archive + at + 16u);
    entry->packed_size = le32(archive + at + 20u);
    entry->size = le32(archive + at + 24u);
    entry->external_attributes = le32(archive + at + 38u);
    entry->local_offset = le32(archive + at + 42u);
    entry->central_extra = name + name_size;
    entry->central_extra_size = extra_size;
    if (le16(archive + at + 34u)) {
      fail(error, error_size, "multi-disk archive is unsupported");
      goto reject;
    }
    if (entry->packed_size == 0xffffffffu || entry->size == 0xffffffffu ||
        entry->local_offset == 0xffffffffu) {
      fail(error, error_size, "zip64 archive is unsupported");
      goto reject;
    }
    bool zip64 = false;
    if (!extra_valid(entry->central_extra, extra_size, &zip64)) {
      fail(error, error_size, "malformed central extra field");
      goto reject;
    }
    if (zip64) {
      fail(error, error_size, "zip64 archive is unsupported");
      goto reject;
    }
    if (entry->flags & ((1u << 0u) | (1u << 5u) | (1u << 6u) |
                        (1u << 13u))) {
      fail(error, error_size,
           entry->flags & 1u ? "encrypted entry"
                             : "unsupported protected entry flags");
      goto reject;
    }
    entry->status =
        entry->method != 0u && entry->method != 8u
            ? ARC_ZIP_MEMBER_UNSUPPORTED_METHOD
        : entry->size > limits->member_bytes ? ARC_ZIP_MEMBER_TOO_LARGE
                                              : ARC_ZIP_MEMBER_OK;
    at += ZIP_CENTRAL_HEADER + variable;
  }
  if (at != end) {
    fail(error, error_size, "central directory size mismatch");
    goto reject;
  }
  for (unsigned i = 0u; i < total; ++i) {
    if (!validate_local(archive, archive_size, central_offset, &entries[i],
                        error, error_size))
      goto reject;
    for (unsigned previous = 0u; previous < i; ++previous)
      if (ranges_overlap(entries[previous].local_offset,
                         entries[previous].record_end,
                         entries[i].local_offset, entries[i].record_end)) {
        fail(error, error_size, "overlapping archive members");
        goto reject;
      }
  }
  index->entries = entries;
  index->count = total;
  index->central_offset = central_offset;
  return true;

reject:
  free(entries);
  return false;
}

void arc_zip_free(ArcZipArchive *index) {
  if (!index)
    return;
  free(index->entries);
  memset(index, 0, sizeof(*index));
}

static voidpf zalloc_fn(voidpf opaque, uInt items, uInt size) {
  (void)opaque;
  if (size && items > SIZE_MAX / size)
    return Z_NULL;
  return calloc(items, size);
}
static void zfree_fn(voidpf opaque, voidpf address) {
  (void)opaque;
  free(address);
}

#if defined(ARCDROID_TEST_FAULTS)
static unsigned fail_member_allocations;
void arc_zip_test_fail_member_allocations(unsigned count) {
  fail_member_allocations = count;
}
#endif

const char *arc_zip_member_status_reason(ArcZipMemberStatus status) {
  switch (status) {
  case ARC_ZIP_MEMBER_UNSUPPORTED_METHOD:
    return "unsupported-method";
  case ARC_ZIP_MEMBER_TOO_LARGE:
    return "member-too-large";
  default:
    return NULL;
  }
}

bool arc_zip_extract(const uint8_t *archive, size_t archive_size,
                     const ArcZipEntry *entry, uint8_t **data,
                     size_t *size, char *error, size_t error_size) {
  if (data)
    *data = NULL;
  if (size)
    *size = 0u;
  if (!archive || !entry || !data || !size ||
      entry->data_offset > archive_size ||
      entry->packed_size > archive_size - entry->data_offset) {
    fail(error, error_size, "invalid member extraction input");
    return false;
  }
  const char *status = arc_zip_member_status_reason(entry->status);
  if (status) {
    fail(error, error_size, status);
    return false;
  }
#if defined(ARCDROID_TEST_FAULTS)
  if (fail_member_allocations) {
    --fail_member_allocations;
    fail(error, error_size, "out of memory");
    return false;
  }
#endif
  uint8_t *output = malloc(entry->size ? entry->size : 1u);
  if (!output) {
    fail(error, error_size, "out of memory");
    return false;
  }
  const uint8_t *input = archive + entry->data_offset;
  if (entry->method == 0u) {
    if (entry->packed_size != entry->size) {
      free(output);
      fail(error, error_size, "stored member size mismatch");
      return false;
    }
    memcpy(output, input, entry->size);
  } else {
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.zalloc = zalloc_fn;
    stream.zfree = zfree_fn;
    stream.next_in = (Bytef *)(uintptr_t)input;
    stream.avail_in = entry->packed_size;
    stream.next_out = output;
    stream.avail_out = entry->size;
    if (inflateInit2(&stream, -15) != Z_OK) {
      free(output);
      fail(error, error_size, "inflater initialization failed");
      return false;
    }
    int result = inflate(&stream, Z_FINISH);
    bool valid = result == Z_STREAM_END &&
                 stream.total_in == entry->packed_size &&
                 stream.total_out == entry->size;
    (void)inflateEnd(&stream);
    if (!valid) {
      free(output);
      fail(error, error_size, "inflated-size mismatch");
      return false;
    }
  }
  if (arc_crc32(output, entry->size) != entry->crc32) {
    free(output);
    fail(error, error_size, "member CRC32 mismatch");
    return false;
  }
  *data = output;
  *size = entry->size;
  return true;
}
