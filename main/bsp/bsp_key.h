#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace bsp {

// v0.7: BOOT-key (GPIO0) ISR → FreeRTOS queue.
//
// The handler runs in the GPIO ISR context — it must not allocate, take
// mutexes, or call into LVGL. It just posts a tiny event to the queue.
// A consumer (e.g. pet::ScreenPower::task_loop) drains the queue in normal
// task context and acts on the press.
class BootKey {
public:
    static BootKey &instance();

    // Configure GPIO0 as input + pullup + falling-edge interrupt and start
    // the GPIO ISR service. Safe to call once at boot.
    esp_err_t init();

    // Queue handle. Receives uint8_t press events (value unused; one event
    // per press). Returns nullptr before init() succeeds.
    QueueHandle_t queue() const { return evtq_; }

private:
    BootKey() = default;

    QueueHandle_t evtq_ = nullptr;
};

}  // namespace bsp
