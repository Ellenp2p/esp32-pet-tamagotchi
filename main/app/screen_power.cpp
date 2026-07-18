#include "screen_power.h"

#include "bsp/bsp_key.h"
#include "bsp/bsp_lcd.h"

#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

namespace pet {

static const char *TAG = "screen_power";

ScreenPower &ScreenPower::instance()
{
    static ScreenPower s;
    return s;
}

namespace {

constexpr int64_t kMsPerMin = 60 * 1000;
constexpr int64_t kLimitMs[3] = {
    [0] = 0,                       // Off  -> never auto-off
    [1] = 2  * kMsPerMin,          // Min2
    [2] = 5  * kMsPerMin,          // Min5
};
constexpr uint8_t kTimeoutToByte[3] = { 0, 1, 2 };

const char *kNsPower   = "pet_power";
const char *kKeyTimeout = "screen_to";

constexpr int kTaskStack = 3072;
constexpr int kTaskPrio  = 2;       // below UI / pet_task, idle-time work
constexpr int kTickMs    = 500;

}  // namespace

void ScreenPower::set_timeout(ScreenTimeout t)
{
    timeout_ = t;
    nvs_handle_t h;
    if (nvs_open(kNsPower, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, kKeyTimeout, kTimeoutToByte[(uint8_t)t]);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "timeout -> %s",
             t == ScreenTimeout::Off  ? "Off" :
             t == ScreenTimeout::Min2 ? "2 min" : "5 min");
    // Resetting the idle timer so the change doesn't immediately re-off.
    last_input_ms_ = esp_timer_get_time() / 1000;
}

void ScreenPower::note_input()
{
    last_input_ms_ = esp_timer_get_time() / 1000;
    if (state_ == PowerState::Off) {
        wake_up();
    }
}

void ScreenPower::lock_now()
{
    if (state_ == PowerState::Off) return;
    ESP_LOGI(TAG, "lock_now() -> enter_off");
    enter_off();
}

void ScreenPower::wake_up()
{
    if (state_ == PowerState::On) return;
    ESP_LOGI(TAG, "wake_up() -> exit_off");
    exit_off();
    last_input_ms_ = esp_timer_get_time() / 1000;
}

void ScreenPower::enter_off()
{
    state_ = PowerState::Off;
    // LVGL must be paused before we cut backlight so its tick stops
    // trying to render to the disabled panel.
    lvgl_port_stop();
    bsp::LCD::instance().backlight_off();
}

void ScreenPower::exit_off()
{
    bsp::LCD::instance().backlight_on();
    lvgl_port_resume();
    state_ = PowerState::On;
}

void ScreenPower::task_loop()
{
    boot_key_queue_ = bsp::BootKey::instance().queue();
    last_input_ms_  = esp_timer_get_time() / 1000;

    while (true) {
        // Drain BOOT-key events regardless of state. A press while On
        // just resets idle; a press while Off calls wake_up() inside
        // note_input().
        if (boot_key_queue_) {
            uint8_t evt;
            while (xQueueReceive(boot_key_queue_, &evt, 0) == pdTRUE) {
                ESP_LOGI(TAG, "BOOT key event");
                note_input();
            }
        }

        if (state_ == PowerState::On && timeout_ != ScreenTimeout::Off) {
            int64_t now     = esp_timer_get_time() / 1000;
            int64_t idle_ms = now - last_input_ms_;
            int64_t limit   = kLimitMs[(uint8_t)timeout_];
            if (idle_ms >= limit) {
                ESP_LOGI(TAG, "idle %lld ms >= %lld ms -> enter_off",
                         (long long)idle_ms, (long long)limit);
                enter_off();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(kTickMs));
    }
}

void ScreenPower::task_trampoline(void *arg)
{
    static_cast<ScreenPower *>(arg)->task_loop();
    vTaskDelete(nullptr);
}

void ScreenPower::init()
{
    if (task_) return;  // idempotent

    // Read persisted timeout. Defaults to Min2 if unset.
    nvs_handle_t h;
    if (nvs_open(kNsPower, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0xFF;
        if (nvs_get_u8(h, kKeyTimeout, &v) == ESP_OK && v < 3) {
            timeout_ = (ScreenTimeout)v;
        }
        nvs_close(h);
    }

    BaseType_t rc = xTaskCreate(task_trampoline, "screen_power",
                                kTaskStack, this, kTaskPrio, &task_);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        task_ = nullptr;
        return;
    }
    ESP_LOGI(TAG, "init: timeout=%s",
             timeout_ == ScreenTimeout::Off  ? "Off" :
             timeout_ == ScreenTimeout::Min2 ? "2 min" : "5 min");
}

}  // namespace pet
