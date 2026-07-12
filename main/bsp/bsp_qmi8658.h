#pragma once

#include "esp_err.h"
#include <cstdint>

namespace bsp {

struct QMI8658_Data {
    int16_t acc_x;
    int16_t acc_y;
    int16_t acc_z;
    int16_t gyr_x;
    int16_t gyr_y;
    int16_t gyr_z;
    float angle_x;
    float angle_y;
    float angle_z;
};

class QMI8658 {
public:
    static QMI8658 &instance();

    esp_err_t init();
    esp_err_t read(QMI8658_Data *out);

    // Return magnitude of acceleration vector
    float accel_magnitude(const QMI8658_Data &data) const;

    // Return true if a shake is detected (uses simple threshold + cooldown)
    bool detect_shake(QMI8658_Data &data);

private:
    QMI8658() = default;
    esp_err_t write_reg(uint8_t reg, uint8_t value);
    esp_err_t read_reg(uint8_t reg, uint8_t *data, size_t len);

    void *dev_handle_ = nullptr;
    uint32_t last_shake_tick_ = 0;
    bool last_shake_state_ = false;
};

} // namespace bsp
