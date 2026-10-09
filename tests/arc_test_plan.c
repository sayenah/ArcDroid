/* Test-only text/file admission outside RPCEmu. GPL-2.0-only. */
#include "arc_test_plan.h"
#include "engine_test.h"
#include "arc_io.h"
#include "rpc_cmos_store.h"
#include <ctype.h>
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

static unsigned cmos_phase; /* 0=v1, 1=generating, 2=requested, 3=failed */
static unsigned cmos_frames=8;
static bool refusal_reported;
static ArcRpcCmosStore record, defaults;
static void *pending_rom;
static size_t pending_size;
static uint64_t pending_epoch;
static unsigned pending_model, pending_rate;
static void (*plan_notice)(const char *,bool);
void arc_test_plan_set_notice(void (*notice)(const char *,bool)) { plan_notice=notice; }
RETRO_API unsigned arc_test_plan_cmos_phase(void) { return cmos_phase; }
static void evidence(uint8_t bytes[256],ArcRpcCmosEvidence *e,uint64_t *now,unsigned *state);
RETRO_API bool arc_test_plan_cmos_pacing(unsigned frames)
{
    if(cmos_phase!=1 || (frames!=1 && frames!=8))return false;
    uint8_t bytes[256];ArcRpcCmosEvidence e;uint64_t now;unsigned state;
    evidence(bytes,&e,&now,&state);
    if(now)return false;
    cmos_frames=frames;return true;
}
bool arc_test_plan_pending(void) { return cmos_phase==1 || cmos_phase==3; }
static void evidence(uint8_t bytes[256],ArcRpcCmosEvidence *e,uint64_t *now,unsigned *state)
{ arc_engine_test_cmos_evidence(bytes,e->mask,&e->writes,&e->last_write,now,state); }
bool arc_test_plan_persist(char *error,size_t size)
{
    if(cmos_phase!=2 || !arc_engine_is_rpc() || !arc_engine_active())return true;
    uint8_t bytes[256];ArcRpcCmosEvidence e;uint64_t now;unsigned state;
    evidence(bytes,&e,&now,&state);
    bool ok=arc_riscpc_cmos_store_put(&record,bytes+16,NULL,error,size);
    if(record.refused){
        if(refusal_reported && error && size)error[0]=0;
        refusal_reported=true;
    }else refusal_reported=false;
    return ok;
}
static bool requested_start(char *error,size_t size)
{
    uint8_t seed[256]={0};memcpy(seed+16,record.payload,240);
    arc_engine_stop();
    bool ok=arc_engine_test_plan_start(pending_model,pending_rate,false,pending_rom,pending_size,seed,pending_epoch,error,size);
    free(pending_rom);pending_rom=NULL;if(ok)ok=media_start(error,size);
    if(!ok)arc_engine_stop();
    cmos_phase=ok?2:3;script_active=ok;
    return ok;
}
bool arc_test_plan_slice(char *error,size_t size)
{
    if(cmos_phase!=1)return true;
    bool ok=arc_engine_test_cmos_slice(UINT64_C(16666667)*cmos_frames);
    uint8_t bytes[256];ArcRpcCmosEvidence e={0};uint64_t now;unsigned state;
    evidence(bytes,&e,&now,&state);
    if(ok && state==1 && now<ARC_RPC_CMOS_CAPTURE_NS)return true;
    e.epoch=ARC_RPC_CMOS_GENERATION_EPOCH;static const unsigned rates[]={20,35,100,35};e.rate=rates[pending_model];
    ok=ok && state==2 && now==ARC_RPC_CMOS_CAPTURE_NS && arc_riscpc_cmos_oracle(record.key.layout,bytes+16,&e);
    if(!ok){if(error&&size)snprintf(error,size,"RPC default failed: required mask, stability, checksum or interrupted capture (writes=%llu)",(unsigned long long)e.writes);}
    else ok=arc_riscpc_cmos_store_put(&defaults,bytes+16,&e,error,size);
    if(ok)ok=arc_riscpc_cmos_store_put(&record,bytes+16,NULL,error,size);
    if(ok)return requested_start(error,size);
    arc_engine_stop();free(pending_rom);pending_rom=NULL;cmos_phase=3;script_active=false;return false;
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
    char error[256];if(!arc_test_plan_persist(error,sizeof(error)) && error[0] && plan_notice)plan_notice(error,true);
    free(pending_rom);pending_rom=NULL;cmos_phase=0;cmos_frames=8;refusal_reported=false;
    memset(&record,0,sizeof(record));memset(&defaults,0,sizeof(defaults));
    event_count = next_event = disk_count = disk_next = 0; media_content[0]=media_helper[0]=0; script_frame = 0; script_active = false;
}
void arc_test_plan_reset(void)
{
    if(cmos_phase==1){arc_engine_stop();free(pending_rom);pending_rom=NULL;cmos_phase=3;script_active=false;
        if(plan_notice)plan_notice("RPC default capture interrupted by reset; no default published",true);
        return;}
    next_event = disk_next = 0; script_frame = 0;
    if (script_active) arc_engine_test_plan_input_ready();
}
void arc_test_plan_input(ArcInputState *input)
{
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
    arc_test_plan_clear();
    char *text = NULL;
    if (!text_file(path, TEXT_LIMIT, &text)) return fail(error, error_size, "Plan must be bounded ASCII text");
    const char *keys[] = {"version", "model", "rate_mips", "firmware", "cmos", "seed", "rtc_epoch", "input", "content_id", "cmos_layout", "media", "drive1", "disk_control"};
    char *values[13] = {0}; bool valid = true;
    for (char *line = text; line && valid;) {
        char *end = strchr(line, '\n'); if (end) *end++ = 0;
        char *s = trim(line);
        if (*s && *s != '#') {
            char *equal = strchr(s, '=');
            if (!equal) { valid = false; break; }
            *equal++ = 0; s = trim(s); equal = trim(equal);
            unsigned k; for (k = 0; k < 13 && strcmp(keys[k], s); ++k) {}
            if (k == 13 || values[k] || !*equal) { valid = false; break; }
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
    bool version=persist?values[8] && arc_riscpc_cmos_hex(values[8],40,key.content) && key.layout && !values[5] && save && *save:
        values[0] && !strcmp(values[0],"1") && !values[8] && !values[9] && !values[10] && !values[11] && !values[12];
    valid = valid && version && model < 4 && values[3] &&
        values[4] && (persist || factory || !strcmp(values[4], "seed")) &&
        (persist || (factory ? !values[5] : values[5] != NULL)) && values[6] &&
        number(values[6], UINT64_C(13569465599), &epoch) &&
        (!values[2] || (number(values[2], 100, &rate) && rate));
    char firmware[PATH_LIMIT], seed_path[PATH_LIMIT], input_path[PATH_LIMIT];
    if (valid) valid = resolve(values[3], system, firmware) &&
        (persist || factory || resolve(values[5], system, seed_path)) &&
        (!values[7] || (resolve(values[7], system, input_path) && script_load(input_path)));
    if(valid && persist){char disks[PATH_LIMIT];valid=(!values[10]||resolve(values[10],system,media_content)) &&
        (!values[11]||resolve(values[11],system,media_helper)) &&
        (!values[12]||(resolve(values[12],system,disks)&&disk_script_load(disks,system)));}
    void *rom = NULL, *seed = NULL; size_t rom_size = 0, seed_size = 0;
    if (valid) valid = arc_io_read_all_limit(firmware, &rom, &rom_size, 8388608, NULL) &&
        (rom_size == 2097152 || rom_size == 4194304 || rom_size == 8388608) &&
        (persist || factory || (arc_io_read_all_limit(seed_path, &seed, &seed_size, 256, NULL) && seed_size == 256));
    if (!valid) fail(error, error_size, "Invalid plan, path, script, ROM size or seed size");
    else if(!persist)valid=arc_engine_test_plan_start(model,(unsigned)rate,factory,rom,rom_size,seed,epoch,error,error_size);
    else {
        arc_sha256_hex(rom,rom_size,key.firmware);
        ArcRpcCmosResult found=arc_riscpc_cmos_store_load(&record,save,&key,error,error_size);
        if(error&&*error && plan_notice){plan_notice(error,true);refusal_reported=record.refused;}
        valid=found!=ARC_RPC_CMOS_REFUSED;
        if(valid){
            pending_rom=rom;rom=NULL;pending_size=rom_size;pending_model=model;pending_rate=(unsigned)rate;pending_epoch=epoch;
            if(found==ARC_RPC_CMOS_FOUND)valid=requested_start(error,error_size);
            else {
                key.defaults=true;key.procedure=ARC_RPC_CMOS_PROCEDURE_REVISION;key.timing=ARC_RPC_CMOS_TIMING_REVISION;
                found=arc_riscpc_cmos_store_load(&defaults,save,&key,error,error_size);
                if(error&&*error && plan_notice)plan_notice(error,true);
                valid=found!=ARC_RPC_CMOS_REFUSED;
                if(valid && found==ARC_RPC_CMOS_FOUND){valid=arc_riscpc_cmos_store_put(&record,defaults.payload,NULL,error,error_size);if(valid)valid=requested_start(error,error_size);}
                else if(valid){
                    valid=arc_engine_test_plan_start(model,0,true,pending_rom,pending_size,NULL,ARC_RPC_CMOS_GENERATION_EPOCH,error,error_size);
                    if(valid)valid=arc_engine_test_cmos_schedule(ARC_RPC_CMOS_CAPTURE_NS,ARC_RPC_CMOS_DELETE_NS);
                    if(valid){cmos_phase=1;if(plan_notice)plan_notice("RPC default generation: isolated held-Delete capture, 45 emulated seconds",false);}
                }
            }
        }
    }
    free(seed); free(rom); free(text);
    if (!valid) arc_test_plan_clear(); else script_active = cmos_phase!=1;
    return valid;
}
