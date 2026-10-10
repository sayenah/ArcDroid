/* Non-shipping plan frontend. GPL-2.0-only. */
#ifndef ARC_TEST_PLAN_H
#define ARC_TEST_PLAN_H
#include "arcdroid_core.h"
#include "libretro.h"
#include "rpc_hd_store.h"
#include <stdbool.h>
#include <stddef.h>
#define ARC_TEST_PLAN_EXTENSION "arcplan"
bool arc_test_plan_path(const char *path);
bool arc_test_plan_load(const char *path, const char *system, const char *save,
                        char *error, size_t error_size);
void arc_test_plan_clear(void);
typedef struct {
    bool (*start)(const char *content, const char *helper, char *error, size_t size);
    bool (*eject)(bool value);
    bool (*select)(unsigned index);
    bool (*replace)(unsigned index,const struct retro_game_info *info);
    bool (*add)(void);
} ArcTestPlanMedia;
void arc_test_plan_set_media(const ArcTestPlanMedia *callbacks);
void arc_test_plan_set_notice(void (*notice)(const char *, bool));
void arc_test_plan_set_log(void (*log)(const char *));
bool arc_test_plan_pending(void);
bool arc_test_plan_slice(char *error, size_t size);
void arc_test_plan_hd_frame(void);
void arc_test_plan_hd_begin_frame(void);
bool arc_test_plan_persist(char *error, size_t size);
typedef struct {
    uint64_t read_ns, acknowledgement_ns, maintenance_ns, read_commands, write_commands,
        generation, checkpoints, open_ns;
    unsigned phase, next_phase;
    bool attached;
} ArcTestHdCost;
/* Opt-in test-only clocks; no host measurement controls guest execution. */
RETRO_API void arc_test_plan_hd_cost(ArcTestHdCost *out,bool enabled);
RETRO_API bool arc_test_plan_hd_stats(ArcRpcHdStats *out);
RETRO_API bool arc_test_plan_hd_write(uint32_t lba,uint32_t count,const uint8_t *bytes);
RETRO_API unsigned arc_test_plan_cmos_phase(void);
RETRO_API bool arc_test_plan_cmos_pacing(unsigned frames);
void arc_test_plan_reset(void);
void arc_test_plan_input(ArcInputState *input);
#endif
