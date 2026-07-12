#include "esp_log.h"
#include "nvs_flash.h"

#include "bsp/bsp_i2c.h"
#include "bsp/bsp_pca9557.h"
#include "bsp/bsp_lcd.h"
#include "bsp/bsp_touch.h"
#include "bsp/bsp_qmi8658.h"
#include "lvgl/lvgl_init.h"
#include "app/ble_pet.h"
#include "kos/kos.h"
#include "kos/kos_app_registry.h"
#include "kos/kos_input.h"
#include "esp_lvgl_port.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP32-S3 KOS starting...");

    // Board bring-up.
    ESP_ERROR_CHECK(bsp::I2C::instance().init());
    ESP_ERROR_CHECK(bsp::PCA9557::instance().init());
    ESP_ERROR_CHECK(bsp::LCD::instance().init());
    ESP_ERROR_CHECK(bsp::Touch::instance().init());
    ESP_ERROR_CHECK(bsp::QMI8658::instance().init());

    // LVGL.
    ESP_ERROR_CHECK(lvgl_app::init());

    // BLE 在 KOS 主循环之外独立运行(legacy 接口)。
    pet_ble::init();

    // KOS 启动:LVGL port 必须在 lock 状态下。
    if (lvgl_port_lock(0)) {
        kos::boot();
        // 启动 KOS tick task(每 100ms tick)。
        kos::start_task();
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock for KOS boot");
    }

    ESP_LOGI(TAG, "KOS boot complete");
}
