#include "clock_util.h"

#include <cstdio>
#include <ctime>

namespace pet {
namespace clock_util {

void format_iso_utc_to_cn(const char *iso,
                          char       *out,
                          size_t      sz) noexcept
{
    if (!iso || !iso[0]) { out[0] = 0; return; }
    int yr = 0, mo = 0, da = 0, hh = 0, mi = 0;
    if (sscanf(iso, "%d-%d-%dT%d:%d", &yr, &mo, &da, &hh, &mi) < 5) {
        out[0] = 0;
        return;
    }
    // Leap-aware month day table (inline to keep this translation
    // unit dependency-free for callers).
    constexpr int8_t mdays[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    auto is_leap = [](int y) noexcept {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    };
    int mdays_y[12];
    for (int i = 0; i < 12; i++) {
        mdays_y[i] = mdays[i] + ((i == 1 && is_leap(yr)) ? 1 : 0);
    }

    hh += 8;
    if (hh >= 24) {
        hh -= 24;
        da += 1;
        if (da > mdays_y[mo - 1]) {
            da = 1;
            mo += 1;
            if (mo > 12) { mo = 1; yr += 1; }
        }
    }
    snprintf(out, sz, "%02d-%02d %02d:%02d", mo, da, hh, mi);
}

void format_epoch_ms_utc_to_cn(int64_t ms,
                               char    *out,
                               size_t   sz) noexcept
{
    out[0] = 0;
    if (ms <= 0) return;
    time_t t = static_cast<time_t>(ms / 1000);
    t += 8 * 3600;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(out, sz, "%02d-%02d %02d:%02d",
             tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}

}  // namespace clock_util
}  // namespace pet
