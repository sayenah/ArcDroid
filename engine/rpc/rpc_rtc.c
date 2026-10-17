/* ArcDroid deterministic Gregorian RTC conversion. GPL-2.0-only. */
#include "rpc_rtc.h"

static unsigned leap(unsigned year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
bool arc_rpc_calendar(uint64_t epoch, uint64_t elapsed_ns, ArcRpcCalendar *out)
{
    /* 2400-01-01 is the exclusive upper boundary. */
    const uint64_t end = UINT64_C(13569465600);
    uint64_t seconds = elapsed_ns / UINT64_C(1000000000);
    if (!out || epoch >= end || seconds >= end - epoch) return false;
    seconds += epoch;
    unsigned days = (unsigned)(seconds / 86400);
    unsigned weekday = (days + 4) % 7;
    unsigned year = 1970;
    /* At most 430 iterations, independent of device or firmware contents. */
    for (; year < 2400; ++year) {
        unsigned length = 365 + leap(year);
        if (days < length) break;
        days -= length;
    }
    static const unsigned lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned month = 0;
    for (; month < 11; ++month) {
        unsigned length = lengths[month] + (month == 1 ? leap(year) : 0);
        if (days < length) break;
        days -= length;
    }
    *out = (ArcRpcCalendar){year, month + 1, days + 1, weekday,
        (unsigned)(seconds / 3600 % 24), (unsigned)(seconds / 60 % 60),
        (unsigned)(seconds % 60), (unsigned)(elapsed_ns / 10000000 % 100)};
    return true;
}

#if defined(ARCDROID_RPC_LOCKSTEP)
ARC_RPC_LS_SERIALIZE_rpc_rtc
#endif
