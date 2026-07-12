#pragma once

#include "esp_err.h"

namespace bsp {

class PCA9557 {
public:
    static PCA9557 &instance();

    esp_err_t init();

    // Control LCD CS (active low)
    esp_err_t set_lcd_cs(bool level);

    // Control PA enable (audio power amplifier)
    esp_err_t set_pa_en(bool level);

    // Control DVP power down (camera, not used in this project)
    esp_err_t set_dvp_pwdn(bool level);

    // Read current output port state (for debugging)
    esp_err_t read_output(uint8_t *value);

private:
    PCA9557() = default;
    esp_err_t write_reg(uint8_t reg, uint8_t value);
    esp_err_t read_reg(uint8_t reg, uint8_t *value);

    void *dev_handle_ = nullptr;
    uint8_t output_state_ = 0x05; // Default: LCD_CS=1, PA_EN=0, DVP_PWDN=1
};

} // namespace bsp
