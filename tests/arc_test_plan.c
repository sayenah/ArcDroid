/* Test-only text/file admission outside RPCEmu. GPL-2.0-only. */
#include "arc_test_plan.h"
#include "engine_test.h"
#include "arc_io.h"
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
{ event_count = next_event = 0; script_frame = 0; script_active = false; }
void arc_test_plan_reset(void)
{
    next_event = 0; script_frame = 0;
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
bool arc_test_plan_load(const char *path, const char *system, char *error, size_t error_size)
{
    arc_test_plan_clear();
    char *text = NULL;
    if (!text_file(path, TEXT_LIMIT, &text)) return fail(error, error_size, "Plan must be bounded ASCII text");
    const char *keys[] = {"version", "model", "rate_mips", "firmware", "cmos", "seed", "rtc_epoch", "input"};
    char *values[8] = {0}; bool valid = true;
    for (char *line = text; line && valid;) {
        char *end = strchr(line, '\n'); if (end) *end++ = 0;
        char *s = trim(line);
        if (*s && *s != '#') {
            char *equal = strchr(s, '=');
            if (!equal) { valid = false; break; }
            *equal++ = 0; s = trim(s); equal = trim(equal);
            unsigned k; for (k = 0; k < 8 && strcmp(keys[k], s); ++k) {}
            if (k == 8 || values[k] || !*equal) { valid = false; break; }
            values[k] = equal;
        }
        line = end;
    }
    unsigned model = 4; uint64_t rate = 0, epoch = 0;
    const char *models[] = {"arm610", "arm710", "sa110", "arm7500"};
    if (values[1]) for (unsigned i = 0; i < 4; ++i) if (!strcmp(values[1], models[i])) model = i;
    bool factory = values[4] && !strcmp(values[4], "factory");
    valid = valid && values[0] && !strcmp(values[0], "1") && model < 4 && values[3] &&
        values[4] && (factory || !strcmp(values[4], "seed")) &&
        (factory ? !values[5] : values[5] != NULL) && values[6] &&
        number(values[6], UINT64_C(13569465599), &epoch) &&
        (!values[2] || (number(values[2], 100, &rate) && rate));
    char firmware[PATH_LIMIT], seed_path[PATH_LIMIT], input_path[PATH_LIMIT];
    if (valid) valid = resolve(values[3], system, firmware) &&
        (factory || resolve(values[5], system, seed_path)) &&
        (!values[7] || (resolve(values[7], system, input_path) && script_load(input_path)));
    void *rom = NULL, *seed = NULL; size_t rom_size = 0, seed_size = 0;
    if (valid) valid = arc_io_read_all_limit(firmware, &rom, &rom_size, 8388608, NULL) &&
        (rom_size == 2097152 || rom_size == 4194304 || rom_size == 8388608) &&
        (factory || (arc_io_read_all_limit(seed_path, &seed, &seed_size, 256, NULL) && seed_size == 256));
    if (!valid) fail(error, error_size, "Invalid plan, path, script, ROM size or seed size");
    else valid = arc_engine_test_plan_start(model, (unsigned)rate, factory, rom, rom_size, seed, epoch, error, error_size);
    free(seed); free(rom); free(text);
    if (!valid) arc_test_plan_clear(); else script_active = true;
    return valid;
}
