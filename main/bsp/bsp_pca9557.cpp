#include "bsp_pca9557.h"
#include "bsp_config.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

namespace bsp {

static const char *TAG = "bsp_pca9557";

PCA9557 &PCA9557::instance()
{
    static PCA9557 inst;
    return inst;
}

esp_err_t PCA9557::write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(static_cast<i2c_master_dev_handle_t>(dev_handle_), buf, sizeof(buf), 100);
}

esp_err_t PCA9557::read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(static_cast<i2c_master_dev_handle_t>(dev_handle_), &reg, 1, value, 1, 100);
}

esp_err_t PCA9557::init()
{
    if (dev_handle_) {
        return ESP_OK;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA9557_SENSOR_ADDR,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = false,
        },
    };

    esp_err_t ret = i2c_master_bus_add_device(I2C::instance().bus(), &dev_config,
                                               reinterpret_cast<i2c_master_dev_handle_t *>(&dev_handle_));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add PCA9557 device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set default output values: LCD_CS=1, PA_EN=0, DVP_PWDN=1 -> 0x05
    output_state_ = 0x05;
    ESP_ERROR_CHECK(write_reg(PCA9557_OUTPUT_PORT, output_state_));
    // Configure IO0, IO1, IO2 as outputs, others as inputs -> 0xF8
    ESP_ERROR_CHECK(write_reg(PCA9557_CONFIGURATION_PORT, 0xF8));

    ESP_LOGI(TAG, "PCA9557 initialized");
    return ESP_OK;
}

esp_err_t PCA9557::set_lcd_cs(bool level)
{
    output_state_ = level ? (output_state_ | LCD_CS_GPIO) : (output_state_ & ~LCD_CS_GPIO);
    return write_reg(PCA9557_OUTPUT_PORT, output_state_);
}

esp_err_t PCA9557::set_pa_en(bool level)
{
    output_state_ = level ? (output_state_ | PA_EN_GPIO) : (output_state_ & ~PA_EN_GPIO);
    return write_reg(PCA9557_OUTPUT_PORT, output_state_);
}

esp_err_t PCA9557::set_dvp_pwdn(bool level)
{
    output_state_ = level ? (output_state_ | DVP_PWDN_GPIO) : (output_state_ & ~DVP_PWDN_GPIO);
    return write_reg(PCA9557_OUTPUT_PORT, output_state_);
}

esp_err_t PCA9557::read_output(uint8_t *value)
{
    return read_reg(PCA9557_OUTPUT_PORT, value);
}

} // namespace bsp
