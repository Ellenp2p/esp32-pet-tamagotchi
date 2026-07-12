#include "bsp_touch.h"
#include "bsp_config.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft5x06.h"

namespace bsp {

static const char *TAG = "bsp_touch";

Touch &Touch::instance()
{
    static Touch inst;
    return inst;
}

esp_err_t Touch::init()
{
    if (tp_handle_) {
        return ESP_OK;
    }

    esp_lcd_panel_io_handle_t tp_io_handle = nullptr;

    // Manually initialize to avoid macro warnings on GCC 15.
    // Values must match ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG(): FT5x06 uses a
    // plain register read with no control phase, so dc_bit_offset=0,
    // lcd_cmd_bits=8, and disable_control_phase=true.
    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = 0x38,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 0,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .flags = {
            .dc_low_on_data = false,
            .disable_control_phase = true,
        },
    };

    esp_err_t ret = esp_lcd_new_panel_io_i2c(I2C::instance().bus(), &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch IO: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_V_RES,
        .y_max = BSP_LCD_H_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
        .process_coordinates = nullptr,
        .interrupt_callback = nullptr,
        .user_data = nullptr,
        .driver_data = nullptr,
    };

    ret = esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create FT5x06 touch: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Touch initialized");
    return ESP_OK;
}

} // namespace bsp
