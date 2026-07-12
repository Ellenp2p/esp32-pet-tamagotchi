#include "bsp_i2c.h"
#include "bsp_config.h"
#include "esp_log.h"

namespace bsp {

static const char *TAG = "bsp_i2c";

I2C &I2C::instance()
{
    static I2C inst;
    return inst;
}

esp_err_t I2C::init()
{
    if (bus_handle_) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = static_cast<i2c_port_num_t>(BSP_I2C_NUM),
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
            .allow_pd = false,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &bus_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C master bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C master bus initialized");
    return ESP_OK;
}

} // namespace bsp
