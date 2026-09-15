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

typedef enum {
   ARC_DISK_WRITES_SHADOW = 0,
   ARC_DISK_WRITES_ORIGINAL,
   ARC_DISK_WRITES_OFF
} ArcDiskWriteMode;

typedef struct {
   ArcOsClass os;
   const char *machine;
   const char *cpu;
   unsigned memory_kib;
   bool a3010_joystick;
   bool pointer_relative;
   ArcDiskWriteMode disk_writes;
   ArcMediaKind media;
   char content_id[41];
} ArcLaunchPlan;

#define ARC_CMOS_RECORD_SIZE 256u
#define ARC_CMOS_PAYLOAD_SIZE 240u
#define ARC_CMOS_MACHINE_SIZE 256u

uint32_t arc_crc32(const void *data, size_t size);
void arc_sha1_hex(const void *data, size_t size, char output[41]);
ArcDigest arc_digest(const void *data, size_t size);
static inline uint64_t arc_timestamp_elapsed_cycles(uint64_t start, uint64_t end)
{
   return (end - start) >> 32;
}

const ArcFirmwareFingerprint *arc_firmware_table(size_t *count);
const ArcFirmwareFingerprint *arc_firmware_match(const ArcDigest *digest);
unsigned arc_firmware_auto_priority(ArcOsClass os);
const char *arc_os_name(ArcOsClass os);
bool arc_firmware_assemble(const uint8_t *const *parts, const size_t *sizes,
                           unsigned count, ArcFirmwareRecipe recipe,
                           uint8_t *output, size_t output_size,
                           char *error, size_t error_size);

ArcMediaKind arc_media_inspect(const char *path, size_t size,
                               char *error, size_t error_size);
bool arc_media_is_recognised_adfs(const uint8_t *data, size_t size);
bool arc_firmware_nearest_size(size_t actual, size_t margin, size_t *expected);
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
bool arc_cmos_default(ArcOsClass os, uint8_t output[ARC_CMOS_PAYLOAD_SIZE]);
bool arc_cmos_machine_image(ArcOsClass os,
                            const uint8_t payload[ARC_CMOS_PAYLOAD_SIZE],
                            uint8_t output[ARC_CMOS_MACHINE_SIZE]);

#define ARC_INPUT_KEY_COUNT 512u
#define ARC_INPUT_FIFO_CAPACITY 256u
#define ARC_INPUT_MOTION_STARVATION_BOUND 8u

typedef enum {
   ARC_INPUT_SOURCE_KEYBOARD = 0,
   ARC_INPUT_SOURCE_PAD0,
   ARC_INPUT_SOURCE_PAD1,
   ARC_INPUT_SOURCE_TOUCH,
   ARC_INPUT_SOURCE_MOUSE0,
   ARC_INPUT_SOURCE_MOUSE1,
   ARC_INPUT_SOURCE_COUNT
} ArcInputSource;

typedef enum {
   ARC_INPUT_EVENT_NONE = 0,
   ARC_INPUT_EVENT_KEY,
   ARC_INPUT_EVENT_BUTTON,
   ARC_INPUT_EVENT_MOTION
} ArcInputEventType;

typedef struct {
   ArcInputEventType type;
   unsigned code;
   bool down;
   bool velocity;
   int64_t dx;
   int64_t dy;
} ArcInputEvent;

typedef struct {
   ArcInputEventType type;
   unsigned code;
   bool down;
   int dx;
   int dy;
   uint64_t guest_us;
} ArcKartEvent;

typedef struct {
   bool source_keys[ARC_INPUT_SOURCE_COUNT][ARC_INPUT_KEY_COUNT];
   uint8_t source_buttons[ARC_INPUT_SOURCE_COUNT];
   bool effective_keys[ARC_INPUT_KEY_COUNT];
   uint8_t effective_buttons;
   bool applied_keys[ARC_INPUT_KEY_COUNT];
   uint8_t applied_buttons;
   ArcInputEvent fifo[ARC_INPUT_FIFO_CAPACITY];
   unsigned fifo_count;
   unsigned discrete_since_motion;
   bool overflow_report_pending;
   bool overflow_reported;
} ArcInputState;

void arc_input_reset(ArcInputState *state);
void arc_input_release_all(ArcInputState *state);
void arc_input_clear_source(ArcInputState *state, ArcInputSource source);
void arc_input_source_key(ArcInputState *state, ArcInputSource source,
                          unsigned scancode, bool down, bool supported);
void arc_input_source_buttons(ArcInputState *state, ArcInputSource source,
                              unsigned buttons);
void arc_input_source_click(ArcInputState *state, ArcInputSource source,
                            unsigned button);
void arc_input_motion(ArcInputState *state, int64_t dx, int64_t dy,
                      bool velocity);
/* When the guest mouse channel is disabled, consumption discards both queued
 * velocity and displacement motion so stale motion cannot block discrete input
 * or jump after re-enable. Relative order among discrete events is preserved. */
bool arc_input_consume(ArcInputState *state, bool mouse_enabled,
                       ArcInputEvent *event);
bool arc_input_take_overflow_report(ArcInputState *state);
unsigned arc_input_pending(const ArcInputState *state);

#endif
