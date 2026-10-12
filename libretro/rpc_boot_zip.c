/* R3b bounded streaming ZIP reader. GPL-2.0-only.
 * Framing rules adapted from ArcDroid core/zip.c; no shared-code changes. */
#include "rpc_boot_zip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t get(const uint8_t *p, unsigned n) {
  uint32_t v = 0;
  for (unsigned i = 0; i < n; ++i)
    v |= (uint32_t)p[i] << (8 * i);
  return v;
}
static bool bad(char *e, size_t n, const char *reason) {
  if (e && n)
    snprintf(e, n, "Boot package: %s", reason);
  return false;
}
static bool raw_read(ArcBootZip *z, uint32_t at, void *p, size_t n) {
  if (!z->file || at > z->length || n > z->length - at)
    return false;
  z->operations += 3;
  if (arc_io_seek_file(z->file, at, RETRO_VFS_SEEK_POSITION_START) < 0 ||
      arc_io_tell_file(z->file) != at)
    return false;
  size_t got = arc_io_read_file(z->file, p, n);
  z->io_bytes += got;
  return got == n;
}
void arc_boot_zip_pin(ArcBootZip *z, uint32_t at, const void *data,
                      size_t bytes) {
  ArcSha256 h;
  arc_sha256_init(&h);
  arc_sha256_update(&h, data, bytes);
  arc_sha256_final(&h, z->pins[at / ARC_BOOT_QUANTUM]);
}
bool arc_boot_zip_read(ArcBootZip *z, uint32_t at, void *output, size_t bytes) {
  if (!z->pinned)
    return raw_read(z, at, output, bytes);
  if (at > z->length || bytes > z->length - at)
    return false;
  uint8_t *out = output;
  while (bytes) {
    unsigned index = at / ARC_BOOT_QUANTUM;
    uint32_t first = index * ARC_BOOT_QUANTUM;
    unsigned count = z->length - first;
    if (count > ARC_BOOT_QUANTUM)
      count = ARC_BOOT_QUANTUM;
    if (!raw_read(z, first, z->verify, count))
      return false;
    ArcSha256 h;
    uint8_t digest[32];
    arc_sha256_init(&h);
    arc_sha256_update(&h, z->verify, count);
    arc_sha256_final(&h, digest);
    if (memcmp(digest, z->pins[index], 32))
      return false;
    unsigned offset = at - first, take = count - offset;
    if (take > bytes)
      take = (unsigned)bytes;
    memcpy(out, z->verify + offset, take);
    out += take;
    at += take;
    bytes -= take;
  }
  return true;
}
bool arc_boot_zip_open(ArcBootZip *z, const char *path, char *e, size_t n) {
  z->pinned = false;
  ++z->operations;
  z->file = arc_io_open_stream_counted(path, false, &z->operations);
  if (!z->file)
    return bad(e, n, "missing or unreadable ZIP");
  ++z->operations;
  int64_t length = arc_io_file_length(z->file);
  if (length < 22 || length > ARC_BOOT_INPUT_CAP) {
    arc_boot_zip_close(z);
    return bad(e, n, "compressed input cap or truncated ZIP");
  }
  z->length = (uint32_t)length;
  return true;
}
bool arc_boot_zip_close(ArcBootZip *z) {
  if (!z->file)
    return true;
  ArcIoFile *f = z->file;
  z->file = NULL;
  ++z->operations;
  return arc_io_close_file(f);
}
/* Reject traversal before any mapping; dots within components remain literal
 * RISC OS slash characters, rather than host-directory separators. */
static bool safe_name(const uint8_t *p, unsigned n) {
  if (!n || p[0] == '/' || p[0] == '\\' || memchr(p, 0, n) ||
      memchr(p, '\\', n))
    return false;
  unsigned begin = 0;
  for (unsigned i = 0; i <= n; ++i)
    if (i == n || p[i] == '/') {
      unsigned len = i - begin;
      if (!len && i == n && i && p[i - 1] == '/')
        break;
      if (!len || (len == 1 && p[begin] == '.') ||
          (len == 2 && p[begin] == '.' && p[begin + 1] == '.'))
        return false;
      begin = i + 1;
    }
  return true;
}
/* Every consumed ARC0 copy is present and unambiguous. Other extra fields are
 * framed too; ZIP64 is not representable by these bounded offsets. */
static bool extra(const uint8_t *p, unsigned n, uint32_t meta[3],
                  uint8_t digest[32]) {
  bool found = false;
  while (n) {
    if (n < 4)
      return false;
    unsigned id = get(p, 2), len = get(p + 2, 2);
    p += 4;
    n -= 4;
    if (len > n || id == 1)
      return false;
    if (id == 0x4341) {
      if (found || len < 16 || memcmp(p, "ARC0", 4))
        return false;
      found = true;
      for (unsigned i = 0; i < 3; ++i)
        meta[i] = get(p + 4 + 4 * i, 4);
      ArcSha256 h;
      arc_sha256_init(&h);
      arc_sha256_update(&h, p, len);
      arc_sha256_final(&h, digest);
    }
    p += len;
    n -= len;
  }
  return found;
}
bool arc_boot_zip_directory(ArcBootZip *z, char *e, size_t n) {
  unsigned take =
      z->length < sizeof(z->scratch) ? z->length : sizeof(z->scratch);
  uint32_t first = z->length - take;
  if (!arc_boot_zip_read(z, first, z->scratch, take))
    return bad(e, n, "ZIP end read failed");
  for (int at = (int)take - 22; at >= 0; --at) {
    const uint8_t *p = z->scratch + at;
    if (get(p, 4) != 0x06054b50 || at + 22u + get(p + 20, 2) != take)
      continue;
    unsigned count = get(p + 10, 2);
    uint32_t bytes = get(p + 12, 4), start = get(p + 16, 4);
    if (get(p + 4, 2) || get(p + 6, 2) || get(p + 8, 2) != count ||
        count == 65535 || count > ARC_BOOT_ZIP_ENTRIES || bytes == UINT32_MAX ||
        start == UINT32_MAX || start > first + (unsigned)at ||
        bytes != first + (unsigned)at - start)
      return bad(e, n, "entry cap or central directory framing");
    z->count = count;
    z->central = z->central_at = start;
    z->central_end = start + bytes;
    z->indexed = 0;
    return true;
  }
  return bad(e, n, "missing ZIP end record");
}
bool arc_boot_zip_index_step(ArcBootZip *z, char *e, size_t n) {
  if (z->indexed == z->count)
    return z->central_at == z->central_end ||
           bad(e, n, "central directory length");
  uint8_t h[46], local[30], cdigest[32], ldigest[32];
  uint32_t cm[3], lm[3];
  ArcBootZipEntry *a = &z->entries[z->indexed];
  if (z->central_at > z->central_end || z->central_end - z->central_at < 46 ||
      !arc_boot_zip_read(z, z->central_at, h, 46) || get(h, 4) != 0x02014b50)
    return bad(e, n, "central entry read or signature");
  unsigned names = get(h + 28, 2), extras = get(h + 30, 2),
           comments = get(h + 32, 2);
  uint64_t end = (uint64_t)z->central_at + 46 + names + extras + comments;
  if (!names || names >= 256 || end > z->central_end || get(h + 34, 2))
    return bad(e, n, "central entry length or multi-disc ZIP");
  if (!arc_boot_zip_read(z, z->central_at + 46, z->scratch, names) ||
      !safe_name(z->scratch, names))
    return bad(e, n, "unsafe ZIP name");
  memcpy(a->name, z->scratch, names);
  a->name[names] = 0;
  a->directory = a->name[names - 1] == '/';
  a->flags = (uint16_t)get(h + 8, 2);
  a->method = (uint16_t)get(h + 10, 2);
  a->crc = get(h + 16, 4);
  a->packed = get(h + 20, 4);
  a->size = get(h + 24, 4);
  a->local = get(h + 42, 4);
  if (a->flags & 0x2061 || !(a->method == 0 || a->method == 8) ||
      a->size > ARC_BOOT_MEMBER_CAP || a->packed > ARC_BOOT_INPUT_CAP ||
      a->local >= z->central || (a->directory && a->size) ||
      (a->method == 0 && a->size != a->packed))
    return bad(e, n, "member cap, flags, method or directory size");
  if (!arc_boot_zip_read(z, z->central_at + 46 + names, z->scratch, extras) ||
      !extra(z->scratch, extras, cm, cdigest))
    return bad(e, n, "missing or malformed central ARC0");
  if (!arc_boot_zip_read(z, a->local, local, 30) ||
      get(local, 4) != 0x04034b50 || get(local + 6, 2) != a->flags ||
      get(local + 8, 2) != a->method || get(local + 26, 2) != names)
    return bad(e, n, "local/central disagreement");
  unsigned lextra = get(local + 28, 2);
  if (!arc_boot_zip_read(z, a->local + 30, z->scratch, names) ||
      memcmp(z->scratch, a->name, names) ||
      !arc_boot_zip_read(z, a->local + 30 + names, z->scratch, lextra) ||
      !extra(z->scratch, lextra, lm, ldigest) || memcmp(cm, lm, sizeof(cm)) ||
      memcmp(cdigest, ldigest, 32))
    return bad(e, n, "local/central ARC0 disagreement");
  uint32_t lc = get(local + 14, 4), lp = get(local + 18, 4),
           ls = get(local + 22, 4);
  if ((!(a->flags & 8) && (lc != a->crc || lp != a->packed || ls != a->size)) ||
      ((a->flags & 8) && ((lc && lc != a->crc) || (lp && lp != a->packed) ||
                          (ls && ls != a->size))))
    return bad(e, n, "local member fields disagree");
  uint64_t data = (uint64_t)a->local + 30 + names + lextra,
           finish = data + a->packed;
  if (finish > z->central)
    return bad(e, n, "member range overlaps directory");
  a->data = (uint32_t)data;
  if (a->flags & 8) {
    uint8_t descriptor[16];
    if (finish + 12 > z->central ||
        !arc_boot_zip_read(z, (uint32_t)finish, descriptor, 12))
      return bad(e, n, "truncated descriptor");
    if (get(descriptor, 4) == 0x08074b50) {
      finish += 4;
      if (finish + 12 > z->central ||
          !arc_boot_zip_read(z, (uint32_t)finish, descriptor, 12))
        return bad(e, n, "truncated signed descriptor");
    }
    if (get(descriptor, 4) != a->crc || get(descriptor + 4, 4) != a->packed ||
        get(descriptor + 8, 4) != a->size)
      return bad(e, n, "descriptor mismatch");
    finish += 12;
  }
  a->end = (uint32_t)finish;
  a->load = cm[0];
  a->exec = cm[1];
  a->attr = cm[2];
  /* ARC0 carries OS_File attributes, not Nick entry flags. FileCore can
   * represent owner/public R/W and lock; the other bits have no Nick form. */
  if (a->attr & ~0x3bu)
    return bad(e, n, "unrepresentable attributes");
  for (unsigned i = 0; i < z->indexed; ++i) {
    ArcBootZipEntry *b = &z->entries[i];
    if (!strcmp(a->name, b->name) || (a->local < b->end && b->local < a->end))
      return bad(e, n, "duplicate or overlapping ZIP member");
  }
  z->expanded += a->size;
  if (z->expanded > ARC_BOOT_EXPANDED_CAP)
    return bad(e, n, "expanded byte cap");
  z->central_at = (uint32_t)end;
  ++z->indexed;
  return z->indexed != z->count || z->central_at == z->central_end ||
         bad(e, n, "central directory length");
}
static voidpf alloc(voidpf opaque, uInt count, uInt bytes) {
  ArcBootZip *z = opaque;
  if (bytes && count > SIZE_MAX / bytes)
    return NULL;
  size_t n = (size_t)count * bytes;
  if (n > 131072 || z->zbytes + n > 131072)
    return NULL;
  size_t *p = calloc(1, n + sizeof(size_t));
  if (!p)
    return NULL;
  *p = n;
  z->zbytes += n;
  if (z->zbytes > z->zhigh)
    z->zhigh = z->zbytes;
  return p + 1;
}
static void release(voidpf opaque, voidpf data) {
  ArcBootZip *z = opaque;
  size_t *p = (size_t *)data - 1;
  z->zbytes -= *p;
  free(p);
}
void arc_boot_zip_extract_end(ArcBootZip *z) {
  if (z->inflating) {
    inflateEnd(&z->stream);
    z->inflating = false;
  }
  z->extracting_active = false;
}
bool arc_boot_zip_extract_begin(ArcBootZip *z, unsigned index) {
  if (z->extracting_active || index >= z->count)
    return false;
  memset(&z->stream, 0, sizeof(z->stream));
  z->extracting = index;
  z->packed_at = z->output_at = z->crc = 0;
  z->ended = false;
  z->extracting_active = true;
  arc_sha256_init(&z->hash);
  if (z->entries[index].method == 8) {
    z->stream.opaque = z;
    z->stream.zalloc = alloc;
    z->stream.zfree = release;
    if (inflateInit2(&z->stream, -15) != Z_OK) {
      z->extracting_active = false;
      return false;
    }
    z->inflating = true;
  }
  return true;
}
bool arc_boot_zip_extract_step(ArcBootZip *z, size_t *bytes, bool *done,
                               char *e, size_t n) {
  *bytes = 0;
  *done = false;
  if (!z->extracting_active)
    return bad(e, n, "extractor state");
  ArcBootZipEntry *a = &z->entries[z->extracting];
  if (a->method == 0) {
    unsigned take = a->size - z->output_at;
    if (take > ARC_BOOT_QUANTUM)
      take = ARC_BOOT_QUANTUM;
    if (take && !arc_boot_zip_read(z, a->data + z->output_at, z->output, take))
      return bad(e, n, "member read failed");
    *bytes = take;
    z->packed_at += take;
    z->ended = z->output_at + take == a->size;
  } else {
    /* At most one input quantum and one output quantum per work unit. */
    if (!z->stream.avail_in && z->packed_at < a->packed) {
      unsigned take = a->packed - z->packed_at;
      if (take > ARC_BOOT_QUANTUM)
        take = ARC_BOOT_QUANTUM;
      if (!arc_boot_zip_read(z, a->data + z->packed_at, z->packed, take))
        return bad(e, n, "deflate input read failed");
      z->stream.next_in = z->packed;
      z->stream.avail_in = take;
      z->packed_at += take;
    }
    z->stream.next_out = z->output;
    z->stream.avail_out = ARC_BOOT_QUANTUM;
    uLong before = z->stream.total_in;
    int result = inflate(&z->stream, Z_NO_FLUSH);
    *bytes = ARC_BOOT_QUANTUM - z->stream.avail_out;
    z->ended = result == Z_STREAM_END;
    if ((result != Z_OK && result != Z_STREAM_END) ||
        (!*bytes && before == z->stream.total_in && !z->ended))
      return bad(e, n, "corrupt or truncated deflate member");
    if (z->ended && (z->stream.total_in != a->packed || z->stream.avail_in ||
                     z->packed_at != a->packed))
      return bad(e, n, "deflate trailing data");
  }
  if (*bytes > a->size - z->output_at)
    return bad(e, n, "expanded member size mismatch");
  z->output_at += (uint32_t)*bytes;
  z->crc = (uint32_t)crc32(z->crc, z->output, (uInt)*bytes);
  arc_sha256_update(&z->hash, z->output, *bytes);
  if (z->ended) {
    if (z->output_at != a->size || z->crc != a->crc)
      return bad(e, n, "member length or CRC failure");
    *done = true;
  }
  return true;
}
