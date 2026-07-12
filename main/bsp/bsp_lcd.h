#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

namespace bsp {

class LCD {
public:
    static LCD &instance();

    esp_err_t init();
    esp_err_t init_backlight();
    esp_err_t set_brightness(int percent);
    esp_err_t backlight_on();
    esp_err_t backlight_off();
    esp_err_t fill_screen(uint16_t color);

    esp_lcd_panel_handle_t panel() const { return panel_handle_; }
    esp_lcd_panel_io_handle_t io() const { return io_handle_; }

private:
    LCD() = default;

    esp_lcd_panel_io_handle_t io_handle_ = nullptr;
    esp_lcd_panel_handle_t panel_handle_ = nullptr;
    bool backlight_inited_ = false;
};

} // namespace bsp
