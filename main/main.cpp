#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/bsp_i2c.h"
#include "bsp/bsp_pca9557.h"
#include "bsp/bsp_lcd.h"
#include "bsp/bsp_touch.h"
#include "bsp/bsp_qmi8658.h"
#include "lvgl/lvgl_init.h"
#include "app/pet_ui.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP32-S3 Pet starting...");

    // Board bring-up: I2C bus, IO expander, LCD panel.
    ESP_ERROR_CHECK(bsp::I2C::instance().init());
    ESP_ERROR_CHECK(bsp::PCA9557::instance().init());
    ESP_ERROR_CHECK(bsp::LCD::instance().init());

    // Sensors / input.
    ESP_ERROR_CHECK(bsp::Touch::instance().init());
    ESP_ERROR_CHECK(bsp::QMI8658::instance().init());

    // LVGL (display + touch registered against the already-initialized panel).
    ESP_ERROR_CHECK(lvgl_app::init());

    // Build the pet UI and start its logic task.
    ESP_ERROR_CHECK(pet::start_ui());

    ESP_LOGI(TAG, "Init complete");
}

