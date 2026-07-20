#pragma once

#include <cstddef>
#include <cstdint>

namespace pet {
namespace clock_util {

// Parse an ISO-8601 / RFC-3339 timestamp ("YYYY-MM-DDTHH:MM:SS..."),
// assume it's UTC, shift to CN (UTC+8), and render as "MM-DD HH:MM".
// Cross-day / cross-month / leap-year boundaries are handled. Output is
// always 0-terminated; on parse failure, out[0] == 0.
void format_iso_utc_to_cn(const char *iso,
                          char       *out,
                          size_t      sz) noexcept;

// Same as above but for epoch milliseconds. Treats ms as UTC epoch,
// shifts by +8h, normalises into MM-DD HH:MM in CN.
void format_epoch_ms_utc_to_cn(int64_t ms,
                               char    *out,
                               size_t   sz) noexcept;

}  // namespace clock_util
}  // namespace pet