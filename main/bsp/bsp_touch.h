#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"

namespace bsp {

class Touch {
public:
    static Touch &instance();

    esp_err_t init();
    esp_lcd_touch_handle_t handle() const { return tp_handle_; }

private:
    Touch() = default;
    esp_lcd_touch_handle_t tp_handle_ = nullptr;
};

} // namespace bsp
