#include "lvgl_init.h"
#include "bsp/bsp_config.h"
#include "bsp/bsp_lcd.h"
#include "bsp/bsp_touch.h"
#include "app/screen_power.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

namespace lvgl_app {

static const char *TAG = "lvgl_app";

static lv_display_t *disp_ = nullptr;   // LVGL 9: was lv_disp_t
static lv_indev_t *touch_indev_ = nullptr;

esp_err_t init()
{
    ESP_LOGI(TAG, "Initialize LVGL library");
    // NOTE: lv_init() is called internally by lvgl_port_init() in esp_lvgl_port 2.x.
    // Do NOT call it again — double init corrupts internal state.

    // LVGL port init
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // Add display to LVGL (LCD driver was already initialized by bsp::LCD).
    // LVGL 9 + esp_lvgl_port 2.x: color_format and full flag set are required.
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = bsp::LCD::instance().io(),
        .panel_handle  = bsp::LCD::instance().panel(),
        .buffer_size   = BSP_LCD_H_RES * BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = false,
        .hres          = BSP_LCD_H_RES,
        .vres          = BSP_LCD_V_RES,
        .monochrome    = false,
        .rotation      = {
            .swap_xy  = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma     = false,
            .buff_spiram  = true,
            .sw_rotate    = false,
            .swap_bytes   = false,
            .full_refresh = false,
            .direct_mode  = false,
        },
    };
    disp_ = lvgl_port_add_disp(&disp_cfg);

    // Add touch to LVGL
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = disp_,
        .handle = bsp::Touch::instance().handle(),
    };
    touch_indev_ = lvgl_port_add_touch(&touch_cfg);

    // v0.7: hook the touch input device so any press resets the screen
    // idle timer (and wakes the screen if it was off).
    lv_indev_add_event_cb(touch_indev_,
                          [](lv_event_t *ev) {
                              (void)ev;
                              pet::ScreenPower::instance().note_input();
                          },
                          LV_EVENT_PRESSED, nullptr);

    ESP_LOGI(TAG, "LVGL initialized");
    return ESP_OK;
}

} // namespace lvgl_app
