#include "bsp_lcd.h"
#include "bsp_config.h"
#include "bsp_pca9557.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_st7789.h"

namespace bsp {

static const char *TAG = "bsp_lcd";

LCD &LCD::instance()
{
    static LCD inst;
    return inst;
}

esp_err_t LCD::init_backlight()
{
    if (backlight_inited_) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {
        .gpio_num = BSP_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = true,
        },
        .deconfigure = false,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));

    backlight_inited_ = true;
    return ESP_OK;
}

esp_err_t LCD::init()
{
    if (panel_handle_) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(init_backlight());

    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .iocfg = {BSP_LCD_SPI_MOSI, GPIO_NUM_NC, BSP_LCD_SPI_CLK, GPIO_NUM_NC, GPIO_NUM_NC,
                  GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC},
        .data_io_default_level = false,
        .max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t),
        .flags = 0,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = BSP_LCD_SPI_CS,
        .dc_gpio_num = BSP_LCD_DC,
        .spi_mode = 2,
        .pclk_hz = static_cast<unsigned int>(BSP_LCD_PIXEL_CLOCK_HZ),
        .trans_queue_depth = 10,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .flags = {
            // ST7789: DC low for command, high for data/parameter (defaults)
            .dc_high_on_cmd = false,
            .dc_low_on_data = false,
            .dc_low_on_param = false,
            .octal_mode = false,
            .quad_mode = false,
            .sio_mode = false,
            .psram_dma_direct = false,
            .lsb_first = false,
            .cs_high_active = false,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(BSP_LCD_SPI_NUM, &io_config, &io_handle_));

    ESP_LOGI(TAG, "Install ST7789 panel driver");
    esp_lcd_panel_dev_config_t panel_config = {
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,  // ESP32 stores RGB565 in little-endian
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .reset_gpio_num = BSP_LCD_RST,
        .vendor_config = nullptr,
        .flags = {
            .reset_active_high = false,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle_, &panel_config, &panel_handle_));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle_));

    // Pull down CS before panel init (software controlled through PCA9557)
    PCA9557::instance().set_lcd_cs(false);

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle_));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle_, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle_, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle_, true, false));
    ESP_ERROR_CHECK(fill_screen(0xFFFF));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle_, true));
    ESP_ERROR_CHECK(backlight_on());

    ESP_LOGI(TAG, "LCD initialized");
    return ESP_OK;
}

esp_err_t LCD::set_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    uint32_t duty = (1023 * percent) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));
    return ESP_OK;
}

esp_err_t LCD::backlight_on()
{
    return set_brightness(100);
}

esp_err_t LCD::backlight_off()
{
    return set_brightness(0);
}

esp_err_t LCD::fill_screen(uint16_t color)
{
    if (!panel_handle_) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t line_size = BSP_LCD_H_RES * sizeof(uint16_t);
    uint16_t *line = static_cast<uint16_t *>(heap_caps_malloc(line_size, MALLOC_CAP_DMA));
    if (!line) {
        ESP_LOGE(TAG, "Failed to allocate line buffer for fill");
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < BSP_LCD_H_RES; i++) {
        line[i] = color;
    }

    for (int y = 0; y < BSP_LCD_V_RES; y++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle_, 0, y, BSP_LCD_H_RES, y + 1, line));
    }

    heap_caps_free(line);
    return ESP_OK;
}

} // namespace bsp
