#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pet {

// v0.8 Phase 3e (simplified): PetMainTask owns the 100 ms worker loop
// that drives pet decay, save throttling, IMU sampling, shake / wake
// detection and BLE notifications. Extracted from pet_ui.cpp so the
// task's lifecycle and counters live as class members instead of file-
// scope statics, mirroring the AiUsageWorker / WifiManager pattern.
//
// Behavioural parity: the loop body, sleep interval (100 ms), save
// throttling (50 ticks), BLE notify throttling (20 ticks) and the
// shake-vs-wake-motion dance are preserved verbatim from the original
// free function `pet_task()`.
class PetMainTask {
public:
    static PetMainTask &instance() noexcept;

    void start() noexcept;

private:
    PetMainTask() = default;

    static void task_trampoline(void *arg);
    void task_loop();

    TaskHandle_t task_ = nullptr;
};

}  // namespace pet
