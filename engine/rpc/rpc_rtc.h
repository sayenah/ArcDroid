/* ArcDroid deterministic Gregorian RTC conversion. GPL-2.0-only. */
#ifndef ARC_RPC_RTC_H
#define ARC_RPC_RTC_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    unsigned year, month, day, weekday, hour, minute, second, centisecond;
} ArcRpcCalendar;
/* Epoch seconds are local civil time represented on a UTC-like integer axis.
 * No timezone or DST lookup occurs after start. Range: 1970 through 2399. */
bool arc_rpc_calendar(uint64_t epoch, uint64_t elapsed_ns, ArcRpcCalendar *out);
/* The session supplies its pinned epoch and scheduler time to the device. */
bool arc_rpc_rtc_read(ArcRpcCalendar *out);
void arc_rpc_cmos_seed(const uint8_t bytes[256]);
void arc_rpc_cmos_snapshot(uint8_t bytes[256]);
uint64_t arc_rpc_cmos_writes(void);
void arc_rpc_cmos_set_write_clock(uint64_t (*clock)(void));
uint64_t arc_rpc_cmos_payload_evidence(uint8_t mask[32], uint64_t *last);
#endif
