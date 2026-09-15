/* ArcDroid core services. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#ifndef ARCDROID_CORE_H
#define ARCDROID_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfd.h"

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
   ARC_MEDIA_ADF_HD_UNSUPPORTED,
   ARC_MEDIA_JFD
} ArcMediaKind;

typedef enum {
   ARC_GEOMETRY_NONE = 0,
   ARC_GEOMETRY_320K,
   ARC_GEOMETRY_640K_L,
   ARC_GEOMETRY_800K_D,
   ARC_GEOMETRY_800K_E
} ArcMediaGeometry;

typedef enum {
   ARC_MEDIA_RECOGNITION_NONE = 0,
   ARC_MEDIA_RECOGNITION_SIZE_FALLBACK,
   ARC_MEDIA_RECOGNITION_DESCRIPTOR,
   ARC_MEDIA_RECOGNITION_CONTAINER
} ArcMediaRecognition;

typedef struct {
   ArcMediaKind kind;
   ArcMediaGeometry geometry;
   size_t declared_size;
   size_t available_size;
   size_t padding_required;
   ArcMediaRecognition recognition;
   bool short_image;
   unsigned boot_option;
   char disc_name[11];
} ArcMediaInfo;

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
   bool launch_helper;
   char launch_target[11];
} ArcLaunchPlan;

#define ARC_LAUNCH_MAX_CANDIDATES 77u
#define ARC_LAUNCH_DISC_SIZE 655360u

typedef enum {
   ARC_LAUNCH_REASON_UNREADABLE = 0,
   ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE,
   ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF,
   ARC_LAUNCH_REASON_NO_CANDIDATES,
   ARC_LAUNCH_REASON_NO_APPLICATIONS,
   ARC_LAUNCH_REASON_AMBIGUOUS,
   ARC_LAUNCH_REASON_SINGLE_APPLICATION,
   ARC_LAUNCH_REASON_DISC_NAME_MATCH
} ArcLaunchReason;

typedef struct {
   char name[11];
   bool application;
} ArcLaunchCandidate;

typedef struct {
   bool readable;
   unsigned boot_option;
   char disc_name[11];
   ArcMediaGeometry geometry;
   ArcLaunchCandidate candidates[ARC_LAUNCH_MAX_CANDIDATES];
   unsigned candidate_count;
   unsigned application_count;
   int target_index;
   ArcLaunchReason reason;
   char candidate_fingerprint[41];
} ArcLaunchDiscovery;

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

bool arc_media_inspect(const char *path, const uint8_t *data, size_t size,
                       ArcMediaInfo *info, char *error, size_t error_size);
bool arc_media_is_recognised_adfs(const uint8_t *data, size_t size);
bool arc_launch_discover(const uint8_t *data, size_t size,
                         const ArcMediaInfo *media, const ArcJfdImage *jfd,
                         ArcLaunchDiscovery *discovery);
const char *arc_launch_reason_name(ArcLaunchReason reason);
bool arc_launch_disc_build(const char *disc_name, const char *target,
                           uint8_t output[ARC_LAUNCH_DISC_SIZE]);
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
                            bool launch_helper,
                            uint8_t output[ARC_CMOS_MACHINE_SIZE]);
void arc_cmos_normalise_payload(uint8_t payload[ARC_CMOS_PAYLOAD_SIZE]);

#define ARC_INPUT_KEY_COUNT 512u
#define ARC_INPUT_FIFO_CAPACITY 256u
#define ARC_INPUT_MOTION_STARVATION_BOUND 8u
#define ARC_PAD_PORTS 2u
#define ARC_PAD_INPUT_COUNT 10u

typedef enum {
   ARC_INPUT_SOURCE_KEYBOARD = 0,
   ARC_INPUT_SOURCE_PAD0,
   ARC_INPUT_SOURCE_PAD1,
   ARC_INPUT_SOURCE_TOUCH,
   ARC_INPUT_SOURCE_MOUSE0,
   ARC_INPUT_SOURCE_MOUSE1,
   ARC_INPUT_SOURCE_OSK,
   ARC_INPUT_SOURCE_COUNT
} ArcInputSource;

typedef enum {
   ARC_PAD_UP = 0,
   ARC_PAD_DOWN,
   ARC_PAD_LEFT,
   ARC_PAD_RIGHT,
   ARC_PAD_A,
   ARC_PAD_B,
   ARC_PAD_X,
   ARC_PAD_Y,
   ARC_PAD_START,
   ARC_PAD_SELECT
} ArcPadInput;

typedef struct {
   bool bound;
   unsigned key;
} ArcInputBinding;

typedef enum {
   ARC_MAPPING_SOURCE_GENERIC = 0,
   ARC_MAPPING_SOURCE_PROFILE,
   ARC_MAPPING_SOURCE_PRESET,
   ARC_MAPPING_SOURCE_OPTION
} ArcMappingSource;

typedef struct {
   bool automatic;
   ArcInputBinding binding;
} ArcInputOverride;

typedef struct {
   ArcInputBinding binding[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT];
   ArcMappingSource source[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT];
   const char *profile_id;
   const char *profile_name;
   bool custom;
} ArcInputResolution;

#define ARC_INPUT_PROFILE_MAX_DIGESTS 4u
typedef struct {
   const char *id;
   const char *name;
   const char *digests[ARC_INPUT_PROFILE_MAX_DIGESTS];
   unsigned digest_count;
   const char *launch_target;
   ArcInputBinding mapping[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT];
   bool has_port1;
   const char *evidence;
} ArcInputProfile;

typedef struct {
   unsigned key;
   const char *value;
   const char *label;
   unsigned kart_row;
   unsigned kart_column;
   bool modifier;
} ArcInputKey;

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
   uint64_t reconcile_generation;
} ArcInputState;

const ArcInputBinding *arc_input_generic_mapping(void);
const ArcInputKey *arc_input_supported_keys(size_t *count);
const ArcInputKey *arc_input_key(unsigned key);
const ArcInputKey *arc_input_key_value(const char *value);
bool arc_input_is_modifier(unsigned key);
const char *arc_pad_input_name(unsigned input);
bool arc_input_resolve(const char content_id[41], const char *launch_target,
                       const char *profile_selection,
                       const ArcInputOverride overrides[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT],
                       ArcInputResolution *resolution);
const ArcInputProfile *arc_input_profiles(size_t *count);
bool arc_input_profiles_validate(char *error, size_t error_size);
#if defined(ARCDROID_TEST_FAULTS)
bool arc_input_profile_test_register(const ArcInputProfile *profile);
void arc_input_profile_test_clear(void);
#endif

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
unsigned arc_input_available(const ArcInputState *state);
uint64_t arc_input_reconcile_generation(const ArcInputState *state);
bool arc_input_source_level(const ArcInputState *state, ArcInputSource source,
                            unsigned key);
bool arc_input_effective_level(const ArcInputState *state, unsigned key);
bool arc_input_applied_level(const ArcInputState *state, unsigned key);
void arc_input_pad_key_sets(ArcInputState *state,
                            const bool port0[ARC_INPUT_KEY_COUNT],
                            const bool port1[ARC_INPUT_KEY_COUNT]);

typedef enum {
   ARC_OSK_MOD_OFF = 0,
   ARC_OSK_MOD_ONESHOT,
   ARC_OSK_MOD_LOCKED
} ArcOskModifier;

typedef enum {
   ARC_OSK_ACTION_NONE = 0,
   ARC_OSK_ACTION_KEY,
   ARC_OSK_ACTION_MODIFIER,
   ARC_OSK_ACTION_PAGE
} ArcOskAction;

typedef struct {
   ArcOskAction action;
   unsigned key;
   const char *label;
   unsigned x;
   unsigned y;
   unsigned width;
   unsigned height;
} ArcOskKey;

#define ARC_OSK_MAX_KEYS 104u
typedef struct {
   bool visible;
   bool disabled;
   bool compact;
   bool transparent;
   bool busy;
   bool interrupted_pending;
   bool contact;
   bool contact_valid;
   bool activation_held;
   unsigned width;
   unsigned height;
   unsigned page;
   unsigned page_count;
   unsigned focus;
   unsigned contact_key;
   uint64_t repeat_at_us;
   int repeat_dx;
   int repeat_dy;
   ArcOskModifier modifiers[3];
   ArcOskKey keys[ARC_OSK_MAX_KEYS];
   unsigned key_count;
   unsigned activation_key;
   unsigned activation_modifiers[3];
   unsigned activation_modifier_count;
   unsigned activation_phase;
   uint64_t activation_generation;
} ArcOsk;

void arc_osk_reset(ArcOsk *osk, ArcInputState *input);
bool arc_osk_open(ArcOsk *osk, unsigned width, unsigned height);
void arc_osk_close(ArcOsk *osk, ArcInputState *input);
bool arc_osk_layout(ArcOsk *osk, unsigned width, unsigned height);
void arc_osk_move(ArcOsk *osk, int dx, int dy, uint64_t guest_us,
                  bool initial);
bool arc_osk_press_focused(ArcOsk *osk, ArcInputState *input);
void arc_osk_release_activation(ArcOsk *osk);
bool arc_osk_shortcut(ArcOsk *osk, ArcInputState *input, unsigned key);
void arc_osk_update(ArcOsk *osk, ArcInputState *input);
bool arc_osk_touch(ArcOsk *osk, int x, int y, bool down,
                   ArcInputState *input);
bool arc_osk_take_interrupted(ArcOsk *osk);
bool arc_osk_render(const ArcOsk *osk, uint32_t *pixels, unsigned width,
                    unsigned height, size_t pitch, const char *profile);

#endif
