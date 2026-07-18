#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <cstdint>

namespace pet {

// v0.7: Screen-off power management.
//
// On `init()` we launch a low-priority FreeRTOS task that:
//   - Drains the BOOT-key queue (set up by bsp::BootKey)
//   - Tracks `last_input_ms_` against the user's timeout setting
//   - Calls `enter_off()` when idle and `exit_off()` when input arrives
//
// `note_input()` is the single hook for any input source — touch, shake,
// BOOT key, or a wake_up() from lock release — and is safe to call from
// any context (it just bumps a timestamp and conditionally calls wake_up).
enum class ScreenTimeout : uint8_t {
    Off = 0,   // never auto-off
    Min2 = 1,  // 2 minutes idle → off
    Min5 = 2,  // 5 minutes idle → off
};

enum class PowerState : uint8_t {
    On = 0,
    Off = 1,
};

class ScreenPower {
public:
    static ScreenPower &instance();

    // Read persisted timeout from NVS, then launch the background task.
    // Must be called after bsp::BootKey::init() so the queue is ready.
    void init();

    // Persist + apply a new timeout.
    void set_timeout(ScreenTimeout t);

    ScreenTimeout timeout() const { return timeout_; }
    PowerState    state()   const { return state_; }

    // Mark an input event. If currently Off, transitions to On.
    // Safe to call from any context.
    void note_input();

    // Force immediate Off. Used by the Settings "Lock" button.
    void lock_now();

    // Internal wake transition. Public so external wake sources can
    // bounce the screen on without going through note_input().
    void wake_up();

private:
    ScreenPower() = default;

    static void task_trampoline(void *);
    void task_loop();

    void enter_off();   // backlight 0 + lvgl_port_stop
    void exit_off();    // lvgl_port_resume + backlight 100

    ScreenTimeout timeout_        = ScreenTimeout::Min2;
    PowerState    state_          = PowerState::On;
    int64_t       last_input_ms_  = 0;
    TaskHandle_t  task_           = nullptr;
    QueueHandle_t boot_key_queue_ = nullptr;
};

}  // namespace pet
