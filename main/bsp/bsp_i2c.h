#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

namespace bsp {

class I2C {
public:
    static I2C &instance();

    esp_err_t init();
    i2c_master_bus_handle_t bus() const { return bus_handle_; }

private:
    I2C() = default;
    i2c_master_bus_handle_t bus_handle_ = nullptr;
};

} // namespace bsp
