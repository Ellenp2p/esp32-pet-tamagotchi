#pragma once

#include "esp_err.h"
#include "nvs.h"

namespace pet {

class PetMeta {
public:
    static PetMeta &instance() noexcept;

    int  today_epoch_day() noexcept;
    int  record_open_day_and_reward(int today_epoch_day) noexcept;
    void clear() noexcept;

private:
    PetMeta() = default;

    esp_err_t ensure_open() noexcept;

    nvs_handle_t handle_ = 0;
    bool         open_   = false;
};

} // namespace pet
