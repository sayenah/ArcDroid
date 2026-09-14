/* ArcDroid core services. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#ifndef ARCDROID_CORE_H
#define ARCDROID_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
   ARC_OS_UNKNOWN = 0,
   ARC_OS_TEST,
   ARC_OS_RISCOS_311,
   ARC_OS_RISCOS_310,
   ARC_OS_RISCOS_319,
   ARC_OS_RISCOS_300,
   ARC_OS_RISCOS_201,
   ARC_OS_RISCOS_200,
   ARC_OS_ARTHUR_120,
   ARC_OS_ARTHUR_030
} ArcOsClass;

typedef enum {
   ARC_RECIPE_COMBINED = 0,
   ARC_RECIPE_BYTE_LANES_4,
   ARC_RECIPE_HALFWORD_LANES_2
} ArcFirmwareRecipe;

typedef struct {
   size_t size;
   uint32_t crc32;
   char sha1[41];
} ArcDigest;

typedef struct {
   ArcOsClass os;
   const char *name;
   size_t size;
   uint32_t crc32;
   const char *sha1;
   ArcFirmwareRecipe recipe;
   unsigned lane;
} ArcFirmwareFingerprint;

typedef enum {
   ARC_MEDIA_INVALID = 0,
   ARC_MEDIA_ADF_320K,
   ARC_MEDIA_ADF_800K,
   ARC_MEDIA_ADL_640K,
   ARC_MEDIA_ADF_HD_UNSUPPORTED
} ArcMediaKind;

typedef struct {
   ArcOsClass os;
   const char *machine;
   const char *cpu;
   unsigned memory_kib;
   bool a3010_joystick;
   ArcMediaKind media;
   char content_id[41];
} ArcLaunchPlan;

#define ARC_CMOS_RECORD_SIZE 256u
#define ARC_CMOS_PAYLOAD_SIZE 192u

uint32_t arc_crc32(const void *data, size_t size);
void arc_sha1_hex(const void *data, size_t size, char output[41]);
ArcDigest arc_digest(const void *data, size_t size);

const ArcFirmwareFingerprint *arc_firmware_table(size_t *count);
const ArcFirmwareFingerprint *arc_firmware_match(const ArcDigest *digest);
const char *arc_os_name(ArcOsClass os);
bool arc_firmware_assemble(const uint8_t *const *parts, const size_t *sizes,
                           unsigned count, ArcFirmwareRecipe recipe,
                           uint8_t *output, size_t output_size,
                           char *error, size_t error_size);

ArcMediaKind arc_media_inspect(const char *path, size_t size,
                               char *error, size_t error_size);
bool arc_launch_plan_default(ArcLaunchPlan *plan, ArcMediaKind media,
                             ArcOsClass os, const char content_sha1[41],
                             bool request_a3010_joystick,
                             char *error, size_t error_size);

bool arc_cmos_encode(uint8_t record[ARC_CMOS_RECORD_SIZE],
                     const ArcLaunchPlan *plan,
                     const uint8_t payload[ARC_CMOS_PAYLOAD_SIZE]);
bool arc_cmos_decode(const uint8_t record[ARC_CMOS_RECORD_SIZE],
                     const ArcLaunchPlan *plan,
                     uint8_t payload[ARC_CMOS_PAYLOAD_SIZE]);

#endif
