/* Test-only text/file admission outside RPCEmu. GPL-2.0-only. */
#include "arc_test_plan.h"
#include "engine_test.h"
#include "arc_io.h"
#include "rpc_cmos_store.h"
#include "rpc_hd_store.h"
#include "rpc_preparation.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TEXT_LIMIT = 16384, SCRIPT_LIMIT = 262144, EVENT_LIMIT = 4096, PATH_LIMIT = 1024 };
typedef struct { uint64_t frame; unsigned key; int x, y, value; bool mouse; } Event;
static Event events[EVENT_LIMIT];
static size_t event_count, next_event;
static uint64_t script_frame;
static bool script_active;
static ArcTestPlanMedia media_callbacks;
static char media_content[PATH_LIMIT],media_helper[PATH_LIMIT];
typedef struct { uint64_t frame; unsigned index,operation;char path[PATH_LIMIT]; } DiskEvent;
static DiskEvent disk_events[EVENT_LIMIT];
static size_t disk_count,disk_next;
void arc_test_plan_set_media(const ArcTestPlanMedia *callbacks) { media_callbacks=*callbacks; }
static bool media_start(char *error,size_t size)
{ return (!media_content[0]&&!media_helper[0]) || (media_callbacks.start && media_callbacks.start(media_content,media_helper,error,size)); }

static ArcRpcPreparation *preparation;
static ArcRpcHdStore *hard_disc;
static char hd_save[PATH_LIMIT],hd_content[41],hd_import[PATH_LIMIT],hd_system[PATH_LIMIT];
static bool hd_populate;
static void (*plan_notice)(const char *,bool);
static void (*plan_log)(const char *);
static unsigned pending_rate;
static bool observation_final_failure;
RETRO_API bool arc_test_plan_observation_complete(void) { return !observation_final_failure; }
static unsigned observer_cells;
static unsigned observer_disc_devices=ARC_DISC_DEVICE_UNSPECIFIED;
static uint32_t observer_poll_r1,observer_poll_r2,observer_forms;
static uint32_t observer_buttons[3];static bool observer_buttons_set;
static char observer_launch[256];
static bool observer_boot,observer_armed;
static uint64_t observer_arm_frame;
static unsigned observer_last_status=UINT_MAX;static uint64_t observer_last_generation=UINT64_MAX;
static bool observation_unmatched;
static unsigned observation_quota=200;
static ArcRpcTraceSelection observation_selection={.classes=ARC_RPC_OBS_SWI,.capacity=ARC_RPC_TRACE_LIMIT};
static bool observation_enabled,observation_paths,observation_failed;
static ArcIoFile *observation_file;
static char observation_path[PATH_LIMIT];
static bool observation_write(const char *text)
{
    size_t n=strlen(text);
    if(observation_file && arc_io_write_file(observation_file,text,n)==n)return true;
    observation_failed=observation_final_failure=true;return false;
}
static bool observation_sink(void *opaque,const ArcRpcObservationTrace *t)
{
    (void)opaque;const ArcRpcObservation *e=&t->event;char line[2048];
    if(e->kind==ARC_RPC_OBS_GAP){
        snprintf(line,sizeof(line),"{\"schema\":2,\"kind\":8,\"order\":%llu,\"instruction\":%llu,\"epoch\":%llu,\"ns\":%llu,\"lost_end_ns\":%llu,\"lost\":%llu,\"incomplete\":true}\n",
            (unsigned long long)e->order,(unsigned long long)e->instruction,(unsigned long long)e->epoch,
            (unsigned long long)e->ns,(unsigned long long)t->lost_end_ns,(unsigned long long)t->lost);
        return observation_write(line);
    }
    int n=snprintf(line,sizeof(line),"{\"schema\":2,\"order\":%llu,\"instruction\":%llu,\"epoch\":%llu,\"ns\":%llu,\"kind\":%u,\"swi\":%u,\"pc\":%u,\"backing\":%u,\"offset\":%u,\"old_mode\":%u,\"mode\":%u,\"width\":%u,\"privileged\":%u,\"nzcv\":%u,\"mask\":%u,\"watch\":%llu,\"matched\":%u,\"inspection\":%u,\"scalar_valid\":%u,\"scalar\":%u,\"registers\":[",
        (unsigned long long)e->order,(unsigned long long)e->instruction,(unsigned long long)e->epoch,
        (unsigned long long)e->ns,e->kind,e->swi,e->pc,e->backing.kind,e->backing.offset,
        e->old_mode,e->mode,e->width,e->privileged,e->nzcv,e->register_mask,
        (unsigned long long)e->watch,e->matched,t->inspection,t->scalar_valid,t->scalar);
    if(n<0 || (size_t)n>=sizeof(line))return false;
    size_t at=(size_t)n;
    for(unsigned i=0;i<15;++i){n=snprintf(line+at,sizeof(line)-at,"%s%u",i?",":"",e->registers[i]);if(n<0 || (size_t)n>=sizeof(line)-at)return false;at+=(size_t)n;}
    n=snprintf(line+at,sizeof(line)-at,"],\"path_hex\":\"");if(n<0 || (size_t)n>=sizeof(line)-at)return false;at+=(size_t)n;
    for(size_t i=0;t->path[i];++i){if(at+3>=sizeof(line))return false;
        n=snprintf(line+at,sizeof(line)-at,"%02x",(unsigned char)t->path[i]);at+=(size_t)n;}
    if(at+5>=sizeof(line))return false;
    memcpy(line+at,"\"}\n",4);return observation_write(line);
}
static void observation_start(void)
{
    arc_engine_test_observation_profile(observation_unmatched,observation_quota);
    if(!observation_enabled)return;
    if(!observation_file)observation_file=arc_io_create_file(observation_path);
    observation_failed=observation_file==NULL;
    arc_engine_test_observation_trace(true,observation_paths);
    (void)arc_engine_test_observation_select(&observation_selection);
    if(observation_failed){observation_final_failure=true;arc_engine_test_observation_drain(NULL,NULL);}
}
static void observation_frame(void)
{
    if(!observation_enabled)return;
    arc_engine_test_observation_drain(observation_failed?NULL:observation_sink,NULL);
    ArcRpcTraceStatus s;arc_engine_test_observation_status(&s);
    if(s.incomplete || observation_failed)observation_final_failure=true;
}

static bool hd_enabled,hd_cost_enabled;
static ArcTestHdCost hd_cost;
static ArcIdeBackend hd_delegate;
RETRO_API void arc_test_plan_hd_cost(ArcTestHdCost *out,bool enabled)
{ if(out)*out=hd_cost;hd_cost_enabled=enabled; }
static bool hd_read(void *opaque,uint32_t lba,uint32_t count,uint8_t *out)
{
    (void)opaque;if(!hd_cost_enabled)return hd_delegate.read(hd_delegate.opaque,lba,count,out);
    uint64_t start=arc_test_clock_ns();bool ok=hd_delegate.read(hd_delegate.opaque,lba,count,out);
    hd_cost.read_ns+=arc_test_clock_ns()-start;++hd_cost.read_commands;return ok;
}
static bool hd_write(void *opaque,uint32_t lba,uint32_t count,const uint8_t *in)
{
    (void)opaque;if(!hd_cost_enabled)return hd_delegate.write(hd_delegate.opaque,lba,count,in);
    uint64_t start=arc_test_clock_ns();bool ok=hd_delegate.write(hd_delegate.opaque,lba,count,in);
    hd_cost.acknowledgement_ns+=arc_test_clock_ns()-start;++hd_cost.write_commands;return ok;
}
RETRO_API bool arc_test_plan_hd_stats(ArcRpcHdStats *out)
{ hard_disc=arc_riscpc_preparation_disc(preparation);if(!hard_disc||!out)return false;arc_riscpc_hd_stats(hard_disc,out);return true; }
RETRO_API bool arc_test_plan_hd_write(uint32_t lba,uint32_t count,const uint8_t *bytes)
{ hard_disc=arc_riscpc_preparation_disc(preparation);return arc_test_plan_cmos_phase()==2&&hard_disc&&arc_riscpc_hd_write(hard_disc,lba,count,bytes); }

void arc_test_plan_set_notice(void (*notice)(const char *,bool)) { plan_notice=notice; }
void arc_test_plan_set_log(void (*log)(const char *)) { plan_log=log; }
static void preparation_notice(void *opaque,const char *s,bool error)
{ (void)opaque;if(plan_notice)plan_notice(s,error); }
static void preparation_log(void *opaque,const char *s)
{ (void)opaque;if(plan_log)plan_log(s); }
static void preparation_started(void *opaque,bool factory)
{ (void)opaque;if(!factory)arc_engine_test_plan_rate(pending_rate);else observation_start(); }
static void preparation_diagnostics(void *opaque) { (void)opaque;observation_frame(); }
static void observer_start(void)
{
    if(observer_cells){arc_engine_test_observers_override(observer_cells,observer_poll_r1,observer_poll_r2,observer_forms);
        (void)arc_engine_test_observers_disc_devices(observer_disc_devices);
        if(observer_buttons_set)arc_engine_test_observers_buttons(observer_buttons[0],observer_buttons[1],observer_buttons[2]);
        if(plan_log){char line[256];snprintf(line,sizeof(line),
            "RPC observers: TEST OVERRIDE; unqualified candidate profile revision=1 cells=0x%02x poll-r1=%u poll-r2=%u forms=0x%08x disc-devices=0x%x",
            observer_cells,observer_poll_r1,observer_poll_r2,observer_forms,observer_disc_devices);plan_log(line);}}
    observer_armed=false;observer_last_status=UINT_MAX;observer_last_generation=UINT64_MAX;
}
static void preparation_attached(void *opaque) { (void)opaque;observer_start(); }
static void observer_status(void)
{
    if(!observer_cells && !observer_launch[0] && !observer_boot)return;
    ArcErrorBox box;ArcLaunchOutcome launch;arc_engine_error_snapshot(&box);arc_engine_launch_outcome_snapshot(&launch);
    unsigned status=(box.recognised_open?1u:0u)|(box.answer_available?2u:0u)|(box.modal_unknown?4u:0u)|
        ((unsigned)launch.state<<3)|((unsigned)arc_engine_boot_watch_state()<<7);
    if((status!=observer_last_status || box.generation!=observer_last_generation) && plan_log){char line[256];snprintf(line,sizeof(line),
        "RPC observers: recognised-open=%u answer-available=%u modal-unknown=%u generation=%llu launch=%u boot=%u",
        box.recognised_open,box.answer_available,box.modal_unknown,(unsigned long long)box.generation,launch.state,arc_engine_boot_watch_state());plan_log(line);}
    observer_last_status=status;observer_last_generation=box.generation;
}
static bool preparation_attach(void *opaque,char *e,size_t n)
{
    (void)opaque;
    bool ok=media_start(e,n);
    hard_disc=arc_riscpc_preparation_disc(preparation);
    if(ok && hard_disc){ArcIdeBackend backend;arc_riscpc_hd_backend(hard_disc,&backend);
        hd_delegate=backend;backend.read=hd_read;backend.write=hd_write;
        ok=arc_engine_rpc_ide(&backend);}
    if(ok){observation_start();script_active=true;next_event=disk_next=0;script_frame=0;}
    return ok;
}
RETRO_API void arc_test_plan_preparation_stats(ArcRpcPreparationStats *s)
{ arc_riscpc_preparation_stats(preparation,s); }
RETRO_API unsigned arc_test_plan_cmos_phase(void)
{
    if(!preparation)return 0;
    ArcRpcPreparationState s=arc_riscpc_preparation_state(preparation);
    return s==ARC_RPC_PREP_RUNNING?2:s>=ARC_RPC_PREP_FAILED?3:1;
}
RETRO_API bool arc_test_plan_cmos_pacing(unsigned frames)
{ return arc_riscpc_preparation_pacing(preparation,frames); }
bool arc_test_plan_pending(void)
{ return preparation && arc_riscpc_preparation_state(preparation)!=ARC_RPC_PREP_RUNNING; }
void arc_test_plan_hd_begin_frame(void)
{ arc_engine_test_frame_begin();if(hd_cost_enabled)memset(&hd_cost,0,sizeof(hd_cost)); }
void arc_test_plan_diagnostics(void) { observation_frame();observer_status(); }
void arc_test_plan_hd_frame(void)
{
    hard_disc=arc_riscpc_preparation_disc(preparation);
    if(!preparation)return;
    ArcRpcHdStats stats={0};if(hard_disc)arc_riscpc_hd_stats(hard_disc,&stats);
    if(hd_cost_enabled){hd_cost.phase=stats.state;hd_cost.phase_step=stats.phase_step;hd_cost.attached=hard_disc!=NULL;}
    uint64_t start=arc_test_clock_ns();arc_riscpc_preparation_maintenance(preparation);
    uint64_t elapsed=arc_test_clock_ns()-start;arc_engine_test_frame_add_ns(elapsed);
    if(hd_cost_enabled && hard_disc){hd_cost.maintenance_ns=elapsed;arc_riscpc_hd_stats(hard_disc,&stats);
        hd_cost.namespace_operations=stats.tick_namespace_operations;hd_cost.next_phase=stats.state;hd_cost.generation=stats.generation;hd_cost.checkpoints=stats.checkpoints;}
}
bool arc_test_plan_persist(char *e,size_t n)
{ return arc_riscpc_preparation_persist(preparation,e,n); }
bool arc_test_plan_slice(char *error,size_t size)
{
    (void)error;(void)size;
    uint64_t start=arc_test_clock_ns();arc_riscpc_preparation_frame(preparation);
    arc_engine_test_frame_add_ns(arc_test_clock_ns()-start);
    return true; /* The module emits terminal errors exactly once. */
}
static bool fail(char *error, size_t size, const char *message)
{ if (error && size) snprintf(error, size, "%s", message); return false; }
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') ++s;
    size_t n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r')) s[--n] = 0;
    return s;
}
static bool number(const char *s, uint64_t maximum, uint64_t *value)
{
    uint64_t n = 0;
    if (!*s) return false;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9' || n > maximum / 10 ||
            (n == maximum / 10 && (unsigned)(*s-'0') > maximum % 10)) return false;
        n = n * 10 + (unsigned)(*s-'0');
    }
    *value = n; return true;
}
static bool text_file(const char *path, size_t limit, char **out)
{
    void *bytes = NULL; size_t size = 0;
    if (!arc_io_read_all_limit(path, &bytes, &size, limit, NULL)) return false;
    bool valid = size && !memchr(bytes, 0, size);
    for (size_t i = 0; valid && i < size; ++i) {
        unsigned char c = ((unsigned char *)bytes)[i];
        if (c != '\n' && c != '\r' && c != '\t' && (c < 32 || c > 126)) valid = false;
    }
    char *text = valid ? malloc(size + 1) : NULL;
    if (text) { memcpy(text, bytes, size); text[size] = 0; }
    free(bytes); *out = text; return text != NULL;
}
static bool resolve(const char *value, const char *system, char out[PATH_LIMIT])
{
    bool drive = isalpha((unsigned char)value[0]) && value[1] == ':' &&
                 (value[2] == '/' || value[2] == '\\');
    bool absolute = value[0] == '/' || (value[0] == '\\' && value[1] == '\\') || drive;
    if (!*value || strlen(value) >= PATH_LIMIT) return false;
    /* Relative paths stay below the declared system directory. No URI schemes,
     * drive-relative paths, single-backslash roots or traversal components. */
    if (!absolute) {
        if (!system || !*system || strchr(value, ':') || value[0] == '\\') return false;
        const char *p = value;
        while (*p) {
            const char *end = p;
            while (*end && *end != '/' && *end != '\\') ++end;
            if (end-p == 2 && p[0] == '.' && p[1] == '.') return false;
            p = *end ? end + 1 : end;
        }
    }
    int n = absolute ? snprintf(out, PATH_LIMIT, "%s", value) :
                       snprintf(out, PATH_LIMIT, "%s/%s", system, value);
    return n >= 0 && n < PATH_LIMIT;
}
bool arc_test_plan_path(const char *path)
{
    if (!path) return false;
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    const char *extension = ARC_TEST_PLAN_EXTENSION;
    for (++dot; *extension; ++dot, ++extension)
        if (tolower((unsigned char)*dot) != *extension) return false;
    return !*dot;
}
void arc_test_plan_clear(void)
{
    observation_frame();
    if(observation_file && !arc_io_close_file(observation_file)){
        observation_final_failure=true;

    }
    observation_unmatched=false;observation_quota=200;
    observer_cells=observer_poll_r1=observer_poll_r2=observer_forms=0;observer_disc_devices=ARC_DISC_DEVICE_UNSPECIFIED;
    observer_buttons_set=false;memset(observer_buttons,0,sizeof(observer_buttons));observer_launch[0]=0;
    observer_boot=observer_armed=false;observer_arm_frame=0;observer_last_status=UINT_MAX;
    observation_selection=(ArcRpcTraceSelection){.classes=ARC_RPC_OBS_SWI,.capacity=ARC_RPC_TRACE_LIMIT};
    observation_file=NULL;observation_enabled=observation_paths=observation_failed=false;observation_path[0]=0;
    arc_engine_test_observation_trace(false,false);
    char error[256]={0};(void)arc_riscpc_preparation_free(preparation,error,sizeof(error));preparation=NULL;hard_disc=NULL;
    hd_enabled=hd_cost_enabled=hd_populate=false;memset(&hd_cost,0,sizeof(hd_cost));memset(&hd_delegate,0,sizeof(hd_delegate));hd_save[0]=hd_content[0]=hd_import[0]=hd_system[0]=0;
    event_count=next_event=disk_count=disk_next=0;media_content[0]=media_helper[0]=0;script_frame=0;script_active=false;
}
void arc_test_plan_reset(void)
{
    if(preparation){
        bool running=arc_riscpc_preparation_state(preparation)==ARC_RPC_PREP_RUNNING;
        (void)arc_riscpc_preparation_reset(preparation);
        script_active=running;
    }
    next_event=disk_next=0;script_frame=0;
    if(script_active)arc_engine_test_plan_input_ready();
}
void arc_test_plan_reset_input(void)
{ next_event=disk_next=0;script_frame=0;observer_armed=false;if(script_active)arc_engine_test_plan_input_ready(); }
void arc_test_plan_input(ArcInputState *input)
{
    if(script_active && !observer_armed && script_frame>=observer_arm_frame){
        observer_armed=true;
        if(observer_launch[0])(void)arc_engine_launch_outcome_arm(observer_launch);
        if(observer_boot)arc_engine_boot_watch_arm();
    }
    if (!script_active) return;
    while (next_event < event_count && events[next_event].frame == script_frame) {
        Event *e = &events[next_event++];
        if (e->mouse) {
            arc_input_source_buttons(input, ARC_INPUT_SOURCE_OSK, (uint8_t)e->value);
            arc_input_motion(input, e->x, e->y, false);
        } else arc_input_source_key(input, ARC_INPUT_SOURCE_OSK, e->key, e->value != 0, true);
    }
    while(disk_next<disk_count && disk_events[disk_next].frame==script_frame){
        DiskEvent *e=&disk_events[disk_next++];bool ok=false;
        if(e->operation==0)ok=media_callbacks.eject && media_callbacks.eject(true);
        else if(e->operation==1)ok=media_callbacks.eject && media_callbacks.eject(false);
        else if(e->operation==2)ok=media_callbacks.select && media_callbacks.select(e->index);
        else if(e->operation==3){struct retro_game_info info={0};info.path=e->path;
            ok=media_callbacks.replace && media_callbacks.replace(e->index,&info);}
        else if(e->operation==4)ok=media_callbacks.replace && media_callbacks.replace(e->index,NULL);
        else if(e->operation==5)ok=media_callbacks.add && media_callbacks.add();
        if(!ok && plan_notice)plan_notice("Timed RPC disk-control operation refused",true);
    }
    ++script_frame;
}
static bool script_load(const char *path)
{
    char *text = NULL;
    if (!text_file(path, SCRIPT_LIMIT, &text)) return false;
    bool valid = true;
    for (char *line = text; line && valid;) {
        char *end = strchr(line, '\n'); if (end) *end++ = 0;
        char *s = trim(line);
        if (*s && *s != '#') {
            char f[32], kind[16], a[32], b[32], c[32], extra[2];
            int count = sscanf(s, "%31s %15s %31s %31s %31s %1s", f, kind, a, b, c, extra);
            uint64_t frame, key, down;
            Event e = {0};
            valid = event_count < EVENT_LIMIT && number(f, UINT32_MAX, &frame) &&
                    (!event_count || frame >= events[event_count-1].frame);
            if (valid && count == 4 && !strcmp(kind, "key")) {
                valid = number(a, ARC_INPUT_KEY_COUNT-1, &key) && number(b, 1, &down);
                if (valid) { e.key = (unsigned)key; e.value = (int)down; }
            } else if (valid && count == 5 && !strcmp(kind, "mouse")) {
                char *tail; long x = strtol(a, &tail, 10); valid = !*tail && x >= -32767 && x <= 32767;
                long y = strtol(b, &tail, 10); valid = valid && !*tail && y >= -32767 && y <= 32767 && number(c, 7, &down);
                if (valid) { e.mouse = true; e.x = (int)x; e.y = (int)y; e.value = (int)down; }
            } else valid = false;
            if (valid) { e.frame = frame; events[event_count++] = e; }
        }
        line = end;
    }
    free(text); return valid;
}
/* Frame ordinals are requested-session boundaries, so generation is never
 * mistaken for gameplay time. Commands use the frontend's disk-control path. */
static bool disk_script_load(const char *path,const char *system)
{
    char *text=NULL;if(!text_file(path,SCRIPT_LIMIT,&text))return false;
    bool valid=true;
    for(char *line=text;line&&valid;){
        char *end=strchr(line,'\n');if(end)*end++=0;char *s=trim(line);
        if(*s&&*s!='#'){
            char frame[32],kind[16],index[32],source[PATH_LIMIT],extra[2];
            int n=sscanf(s,"%31s %15s %31s %1023s %1s",frame,kind,index,source,extra);
            uint64_t f=0,i=0;DiskEvent e={0};
            valid=disk_count<EVENT_LIMIT && number(frame,UINT32_MAX,&f) &&
                (!disk_count||f>=disk_events[disk_count-1].frame);
            if(valid&&n==2&&(!strcmp(kind,"eject")||!strcmp(kind,"insert")||!strcmp(kind,"add")))
                e.operation=!strcmp(kind,"eject")?0:!strcmp(kind,"insert")?1:5;
            else if(valid&&n==3&&(!strcmp(kind,"select")||!strcmp(kind,"remove"))&&number(index,UINT32_MAX,&i)){
                e.operation=!strcmp(kind,"select")?2:4;e.index=(unsigned)i;
            }else if(valid&&n==4&&!strcmp(kind,"replace")&&number(index,UINT32_MAX,&i)&&resolve(source,system,e.path)){
                e.operation=3;e.index=(unsigned)i;
            }else valid=false;
            if(valid){e.frame=f;disk_events[disk_count++]=e;}
        }line=end;
    }free(text);return valid;
}
bool arc_test_plan_load(const char *path, const char *system, const char *save, char *error, size_t error_size)
{
    arc_test_plan_clear();observation_final_failure=false;
    char *text = NULL;
    if (!text_file(path, TEXT_LIMIT, &text)) return fail(error, error_size, "Plan must be bounded ASCII text");
    const char *keys[] = {"version", "model", "rate_mips", "firmware", "cmos", "seed", "rtc_epoch", "input", "content_id", "cmos_layout", "media", "drive1", "disk_control", "hard_disc", "hd_import", "hd_populate", "observation_trace", "observation_paths", "observation_profile", "observation_quota", "observation_classes", "observation_swis", "observation_capacity", "observer_cells", "observer_poll_r1", "observer_poll_r2", "observer_forms", "observer_launch", "observer_boot", "observer_arm_frame", "observer_ok_forms", "observer_cancel_forms", "observer_default_cancel_forms", "observer_disc_devices"};
    char *values[34] = {0}; bool valid = true;
    for (char *line = text; line && valid;) {
        char *end = strchr(line, '\n'); if (end) *end++ = 0;
        char *s = trim(line);
        if (*s && *s != '#') {
            char *equal = strchr(s, '=');
            if (!equal) { valid = false; break; }
            *equal++ = 0; s = trim(s); equal = trim(equal);
            unsigned k; for (k = 0; k < 34 && strcmp(keys[k], s); ++k) {}
            if (k == 34 || values[k] || !*equal) { valid = false; break; }
            values[k] = equal;
        }
        line = end;
    }
    unsigned model = 4; uint64_t rate = 0, epoch = 0;
    const char *models[] = {"arm610", "arm710", "sa110", "arm7500"};
    if (values[1]) for (unsigned i = 0; i < 4; ++i) if (!strcmp(values[1], models[i])) model = i;
    bool factory = values[4] && !strcmp(values[4], "factory");
    bool persist=values[0] && !strcmp(values[0],"2") && values[4] && !strcmp(values[4],"persist");
    ArcRpcCmosKey key={0};key.model=model;key.layout=arc_riscpc_cmos_layout(values[9]);
    bool version=persist?values[8] && arc_riscpc_cmos_hex(values[8],40,key.content) && key.layout && save && *save:
        values[0] && !strcmp(values[0],"1") && !values[8] && !values[9] && !values[10] && !values[11] && !values[12] && !values[13] && !values[14] && !values[15];
    valid = valid && version && model < 4 && values[3] &&
        values[4] && (persist || factory || !strcmp(values[4], "seed")) &&
        (persist || (factory ? !values[5] : values[5] != NULL)) && values[6] &&
        number(values[6], UINT64_C(13569465599), &epoch) &&
        (!values[2] || (number(values[2], 100, &rate) && rate));
    char firmware[PATH_LIMIT], seed_path[PATH_LIMIT], input_path[PATH_LIMIT];
    if (valid) valid = resolve(values[3], system, firmware) &&
        ((persist && !values[5]) || factory || resolve(values[5], system, seed_path)) &&
        (!values[7] || (resolve(values[7], system, input_path) && script_load(input_path)));
    if(valid && persist){char disks[PATH_LIMIT];valid=(!values[10]||resolve(values[10],system,media_content)) &&
        (!values[11]||resolve(values[11],system,media_helper)) &&
        (!values[12]||(resolve(values[12],system,disks)&&disk_script_load(disks,system)));}
    if(valid && persist){
        valid=(!values[13]||!strcmp(values[13],"on")||!strcmp(values[13],"off")) &&
            !((values[14]||values[15])&&values[13]&&!strcmp(values[13],"off")) &&
            (!values[15]||!strcmp(values[15],"on")) && !(values[14]&&values[15]);
        hd_populate=values[15]!=NULL;
        hd_enabled=hd_populate || values[14] || (values[13]&&!strcmp(values[13],"on"));
        if(hd_enabled){int n=snprintf(hd_save,sizeof(hd_save),"%s",save);valid=valid&&n>=0&&n<PATH_LIMIT;
            memcpy(hd_content,key.content,41);if(hd_populate){int copied=snprintf(hd_system,sizeof(hd_system),"%s",system?system:"");valid=valid&&copied>0&&copied<PATH_LIMIT;}if(values[14])valid=valid&&resolve(values[14],system,hd_import);}
    }
    if(valid && (values[20] || values[21] || values[22]) && !values[16])valid=false;
    if(valid && values[20]){
        observation_selection.classes=0;
        for(char *v=values[20];v && valid;){
            char *next=strchr(v,',');if(next)*next++=0;
            unsigned kind=!strcmp(v,"swi")?1:!strcmp(v,"continuation")?2:!strcmp(v,"mode")?4:0;
            valid=kind && !(observation_selection.classes&kind);
            observation_selection.classes|=kind;v=next;
        }
    }
    if(valid && values[21])for(char *v=values[21];v && valid;){
        char *next=strchr(v,',');if(next)*next++=0;
        char *end=NULL;unsigned long n=strtoul(v,&end,!strncmp(v,"0x",2)?16:10);
        valid=*v && *v!='-' && *v!='+' && end && !*end && n<=0xffffff && observation_selection.swi_count<64;
        for(unsigned i=0;i<observation_selection.swi_count;++i)if(observation_selection.swis[i]==n)valid=false;
        if(valid)observation_selection.swis[observation_selection.swi_count++]=(uint32_t)n;
        v=next;
    }
    if(valid && values[22]){
        uint64_t n=0;valid=number(values[22],ARC_RPC_TRACE_LIMIT,&n) && n;
        observation_selection.capacity=(unsigned)n;
    }
    if(valid && values[18]){valid=!strcmp(values[18],"unmatched");observation_unmatched=valid;}
    if(valid && values[19]){uint64_t q=0;valid=number(values[19],200,&q) && (q==1||q==7||q==200);observation_quota=(unsigned)q;}
    if(valid && values[16]){
        valid=resolve(values[16],save,observation_path) && (!values[17] || !strcmp(values[17],"private"));
        observation_enabled=valid;observation_paths=values[17]!=NULL;
    }else if(values[17])valid=false;
    for(unsigned k=23;k<=26 && valid;++k)if(values[k]){
        char *end=NULL;unsigned long long value=strtoull(values[k],&end,0);
        valid=*values[k]!='-' && *values[k]!='+' && end && !*end && value<=UINT32_MAX && (k!=23 || value<=255);
        if(valid){if(k==23)observer_cells=(unsigned)value;if(k==24)observer_poll_r1=(uint32_t)value;
            if(k==25)observer_poll_r2=(uint32_t)value;if(k==26)observer_forms=(uint32_t)value;}
    }
    if(valid && values[27]){size_t length=strlen(values[27]);valid=length>=6 && length<sizeof(observer_launch);if(valid)memcpy(observer_launch,values[27],length+1);}
    if(valid && values[28]){valid=!strcmp(values[28],"launch");observer_boot=valid;}
    if(valid && values[29])valid=number(values[29],UINT32_MAX,&observer_arm_frame);
    if(valid && (values[30] || values[31] || values[32])){
        valid=values[30] && values[31] && values[32];
        for(unsigned k=30;k<33 && valid;++k){char *end=NULL;unsigned long long value=strtoull(values[k],&end,0);
            valid=*values[k]!='-' && *values[k]!='+' && end && !*end && value<=UINT32_MAX;
            if(valid)observer_buttons[k-30]=(uint32_t)value;}
        observer_buttons_set=valid;
    }
    if(valid && values[33]){
        char *end=NULL;unsigned long long forms=strtoull(values[33],&end,0);
        valid=*values[33]!='-' && *values[33]!='+' && end && !*end && forms<=3;
        if(valid)observer_disc_devices=(unsigned)forms;
    }
    void *rom = NULL, *seed = NULL; size_t rom_size = 0, seed_size = 0;
    if (valid) valid = arc_io_read_all_limit(firmware, &rom, &rom_size, 8388608, NULL) &&
        (rom_size == 2097152 || rom_size == 4194304 || rom_size == 8388608) &&
        (persist || factory || (arc_io_read_all_limit(seed_path, &seed, &seed_size, 256, NULL) && seed_size == 256));
    if (!valid) fail(error, error_size, "Invalid plan, path, script, ROM size or seed size");
    else if(!persist)valid=arc_engine_test_plan_start(model,(unsigned)rate,factory,rom,rom_size,seed,epoch,error,error_size);
    else {
        if(valid){
            ArcRpcPreparationRequest request={0};request.firmware=rom;request.firmware_size=rom_size;
            request.key=key;request.save=save;request.system=system;request.epoch=epoch;
            request.seed_path=values[5]?seed_path:NULL;request.hard_disc=hd_enabled;request.populate=hd_populate;
            request.import_path=hd_import[0]?hd_import:NULL;
            ArcRpcPreparationHost host={0};host.notice=preparation_notice;host.log=preparation_log;
            host.attach=preparation_attach;host.started=preparation_started;host.diagnostics=preparation_diagnostics;host.attached=preparation_attached;
            pending_rate=(unsigned)rate;
            preparation=arc_riscpc_preparation_low_level(&request,&host,error,error_size);
            valid=preparation!=NULL;
        }
    }
    if(valid && !persist){
        ArcDigest digest=arc_digest(rom,rom_size);size_t count=0;
        const ArcRpcFirmwareFingerprint *table=arc_riscpc_firmware_catalogue(&count);
        const ArcRpcFirmwareFingerprint *fingerprint=arc_riscpc_firmware_recognize(&digest,table,count);
        arc_engine_rpc_observers_bind(fingerprint?(unsigned)fingerprint->os:0);
    }
    free(seed); free(rom); free(text);
    if (!valid) arc_test_plan_clear(); else {script_active = !persist;if(!persist){observation_start();observer_start();}}
    return valid;
}
