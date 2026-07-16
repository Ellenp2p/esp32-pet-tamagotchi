#pragma once

#include "esp_err.h"

namespace pet {
namespace meta {

// Returns the number of days since 1970-01-01 UTC for the current system
// time. Falls back to 0 (invalid) if the system time is not yet synced
// (NTP or otherwise). Cheap to call — reads RTC time once.
int today_epoch_day();

// Boot-time Daily Streak. The caller passes the value of "today" computed
// from the system clock (or 0 if NTP is not yet available). Internally we
// compare against the persisted "last_open_day" and bump/reset the streak.
// The reward (streak * 10 coins) is added to Pet via Pet::add_coins.
// Returns the new streak value (0 if the day was invalid → skipped).
//
// If called twice on the same day, the second call is a no-op (the saved
// last_open_day is already today).
int record_open_day_and_reward(int today_epoch_day);

// Manual reset (used by the "factory reset" / "revive" button). Wipes the
// saved streak/last_open so the next boot counts as day 1 again.
void clear();

}  // namespace meta
}  // namespace pet