#include "kos.h"
#include "kos_app.h"
#include "kos_app_registry.h"
#include "kos_display.h"
#include "kos_input.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace kos {

static const char *TAG = "kos";
static const char *s_active_id = "none";

void set_active_id(const char *id) { s_active_id = id; }

void boot()
{
    ESP_LOGI(TAG, "KOS boot");
    registry::init();

    int n = registry::count();
    ESP_LOGI(TAG, "Registered %d app(s)", n);
    for (int i = 0; i < n; i++) {
        const AppManifest &m = registry::app(i).manifest();
        ESP_LOGI(TAG, "  [%d] %s v%s — %s", i, m.id, m.version, m.name);
    }
    if (n == 0) {
        ESP_LOGE(TAG, "No apps registered — check KOS_APP_DEFINE usage");
    }
}

// FreeRTOS task body. Polls input + ticks the active app.
static void kos_task_body(void *arg)
{
    (void)arg;
    while (true) {
        if (lvgl_port_lock(0)) {
            input::poll();
            App *cur = registry::current();
            if (cur) {
                uint32_t now_ms =
                    static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
                cur->on_tick(now_ms);
            }
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void start_task()
{
    xTaskCreate(kos_task_body, "kos_task", 4096, nullptr, 5, nullptr);
}

const char *active_app_id() { return s_active_id; }

}  // namespace kos
