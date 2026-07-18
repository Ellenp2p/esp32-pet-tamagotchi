#include "bsp_key.h"
#include "bsp_config.h"

#include "driver/gpio.h"
#include "esp_log.h"

namespace bsp {

BootKey &BootKey::instance()
{
    static BootKey s;
    return s;
}

static const char *TAG = "bsp_key";

// IRAM_ATTR required: GPIO ISR handlers default to flash-resident in
// ESP-IDF 6.x and trigger an abort if they touch non-IRAM code.
static void IRAM_ATTR boot_key_isr(void * /*arg*/)
{
    static const uint8_t kEvtPress = 1;
    BootKey &self = BootKey::instance();
    // Best-effort: drop if the queue is full. We never block here.
    if (self.queue()) {
        xQueueSendFromISR(self.queue(), &kEvtPress, nullptr);
    }
}

esp_err_t BootKey::init()
{
    if (evtq_) return ESP_OK;  // idempotent

    evtq_ = xQueueCreate(4, sizeof(uint8_t));
    if (!evtq_) {
        ESP_LOGE(TAG, "queue create failed");
        return ESP_ERR_NO_MEM;
    }

    // Install the global GPIO ISR service. ESP-IDF allows multiple calls
    // but only the first wins (returns ESP_ERR_INVALID_STATE on duplicate),
    // which we treat as success so this is composable with other drivers
    // that may install their own service.
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(err));
        return err;
    }

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << BSP_BOOT_KEY_GPIO;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_NEGEDGE;
    err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_isr_handler_add(BSP_BOOT_KEY_GPIO, boot_key_isr, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "BOOT key ready on GPIO%d", BSP_BOOT_KEY_GPIO);
    return ESP_OK;
}

}  // namespace bsp
