#pragma once

namespace pet {
namespace idle_events {

// Initialize idle-event scheduler. Safe to call once at startup.
void init();

// Tick the idle event scheduler. Call once per pet_task loop (100ms cadence).
// `current_age_ticks` is the pet's age in game seconds.
void tick(int current_age_ticks);

}  // namespace idle_events
}  // namespace pet