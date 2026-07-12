#include "bsp_qmi8658.h"
#include "bsp_config.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "math.h"

namespace bsp {

static const char *TAG = "bsp_qmi8658";

// QMI8658 register map from board example
enum qmi8658_reg {
    QMI8658_WHO_AM_I = 0,
    QMI8658_REVISION_ID,
    QMI8658_CTRL1,
    QMI8658_CTRL2,
    QMI8658_CTRL3,
    QMI8658_CTRL4,
    QMI8658_CTRL5,
    QMI8658_CTRL6,
    QMI8658_CTRL7,
    QMI8658_STATUSINT = 45,
    QMI8658_STATUS0,
    QMI8658_STATUS1,
    QMI8658_AX_L,
    QMI8658_AX_H,
    QMI8658_AY_L,
    QMI8658_AY_H,
    QMI8658_AZ_L,
    QMI8658_AZ_H,
    QMI8658_GX_L,
    QMI8658_GX_H,
    QMI8658_GY_L,
    QMI8658_GY_H,
    QMI8658_GZ_L,
    QMI8658_GZ_H,
    QMI8658_RESET = 96
};

QMI8658 &QMI8658::instance()
{
    static QMI8658 inst;
    return inst;
}

esp_err_t QMI8658::write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(static_cast<i2c_master_dev_handle_t>(dev_handle_), buf, sizeof(buf), 100);
}

esp_err_t QMI8658::read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(static_cast<i2c_master_dev_handle_t>(dev_handle_), &reg, 1, data, len, 100);
}

esp_err_t QMI8658::init()
{
    if (dev_handle_) {
        return ESP_OK;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMI8658_SENSOR_ADDR,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = false,
        },
    };

    esp_err_t ret = i2c_master_bus_add_device(I2C::instance().bus(), &dev_config,
                                               reinterpret_cast<i2c_master_dev_handle_t *>(&dev_handle_));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add QMI8658 device: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t id = 0;
    for (int i = 0; i < 10; i++) {
        ret = read_reg(QMI8658_WHO_AM_I, &id, 1);
        if (ret == ESP_OK && id == 0x05) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (id != 0x05) {
        ESP_LOGE(TAG, "QMI8658 ID mismatch: 0x%02X", id);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "QMI8658 detected, ID=0x%02X", id);

    write_reg(QMI8658_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(10));
    write_reg(QMI8658_CTRL1, 0x40); // Address auto increment
    write_reg(QMI8658_CTRL7, 0x03); // Enable accel and gyro
    write_reg(QMI8658_CTRL2, 0x95); // Accel 4g, 250Hz
    write_reg(QMI8658_CTRL3, 0xD5); // Gyro 512dps, 250Hz

    ESP_LOGI(TAG, "QMI8658 initialized");
    return ESP_OK;
}

esp_err_t QMI8658::read(QMI8658_Data *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t status = 0;
    esp_err_t ret = read_reg(QMI8658_STATUS0, &status, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    if ((status & 0x03) == 0) {
        return ESP_FAIL;
    }

    int16_t buf[6];
    ret = read_reg(QMI8658_AX_L, reinterpret_cast<uint8_t *>(buf), sizeof(buf));
    if (ret != ESP_OK) {
        return ret;
    }

    out->acc_x = buf[0];
    out->acc_y = buf[1];
    out->acc_z = buf[2];
    out->gyr_x = buf[3];
    out->gyr_y = buf[4];
    out->gyr_z = buf[5];

    float temp;
    temp = static_cast<float>(out->acc_x) / sqrtf(static_cast<float>(out->acc_y) * out->acc_y + static_cast<float>(out->acc_z) * out->acc_z);
    out->angle_x = atanf(temp) * 57.29578f;
    temp = static_cast<float>(out->acc_y) / sqrtf(static_cast<float>(out->acc_x) * out->acc_x + static_cast<float>(out->acc_z) * out->acc_z);
    out->angle_y = atanf(temp) * 57.29578f;
    temp = sqrtf(static_cast<float>(out->acc_x) * out->acc_x + static_cast<float>(out->acc_y) * out->acc_y) / static_cast<float>(out->acc_z);
    out->angle_z = atanf(temp) * 57.29578f;

    return ESP_OK;
}

float QMI8658::accel_magnitude(const QMI8658_Data &data) const
{
    return sqrtf(static_cast<float>(data.acc_x) * data.acc_x +
                 static_cast<float>(data.acc_y) * data.acc_y +
                 static_cast<float>(data.acc_z) * data.acc_z);
}

bool QMI8658::detect_shake(QMI8658_Data &data)
{
    // Accel range is 4g (CTRL2=0x95). The exact LSB/g is board-specific
    // (observed resting magnitude ~17000–18000 here), so derive the 1g
    // baseline empirically. Use a slow EMA of the magnitude, gated by a
    // spike guard so transient impulses cannot lift the baseline.
    const float SHAKE_EMA_ALPHA = 0.005f;       // ~20s time constant at 100Hz
    const float SHAKE_DELTA = 8000.0f;         // dynamic deviation required
    const float SPIKE_GUARD = 4000.0f;         // ignore samples jumping > this
    const uint32_t SHAKE_COOLDOWN_MS = 500;
    const uint32_t EMA_SETTLE_SAMPLES = 30;

    float mag = accel_magnitude(data);
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    static float baseline = 0.0f;
    static float last_mag = 0.0f;
    static uint32_t sample_count = 0;

    // Spike guard: drop the sample if it jumped far from the previous one,
    // otherwise a single shake would permanently inflate the baseline.
    bool is_spike = (sample_count > 0) && (fabsf(mag - last_mag) > SPIKE_GUARD);
    last_mag = mag;

    if (sample_count < EMA_SETTLE_SAMPLES) {
        if (!is_spike) {
            baseline = (baseline * sample_count + mag) / (sample_count + 1);
            sample_count++;
        }
    } else if (!is_spike) {
        baseline += SHAKE_EMA_ALPHA * (mag - baseline);
    }

    // Temporary calibration aid: log the magnitude ~once per second so the
    // still-baseline and shake peaks can be read from the serial monitor.
    static int log_div = 0;
    if (++log_div >= 10) {
        log_div = 0;
        ESP_LOGI(TAG, "accel mag=%.0f base=%.0f dev=%.0f",
                 mag, baseline, fabsf(mag - baseline));
    }

    bool currently_shaking = fabsf(mag - baseline) > SHAKE_DELTA;
    bool shake_event = false;

    if (currently_shaking && !last_shake_state_ && (now - last_shake_tick_ > SHAKE_COOLDOWN_MS)) {
        shake_event = true;
        last_shake_tick_ = now;
    }

    last_shake_state_ = currently_shaking;
    return shake_event;
}

} // namespace bsp
