#include "pet_meta.h"
#include "pet_state.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <ctime>

namespace pet {

static const char *TAG = "pet_meta";
static const char *kNamespace  = "pet_meta";
static const char *kKeyStreak      = "streak";
static const char *kKeyLastOpenDay = "last_day";

PetMeta &PetMeta::instance() noexcept
{
    static PetMeta s;
    return s;
}

esp_err_t PetMeta::ensure_open() noexcept
{
    if (open_) return ESP_OK;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    open_ = true;
    return ESP_OK;
}

int PetMeta::today_epoch_day() noexcept
{
    time_t now = 0;
    time(&now);
    if (now < 86400) {
        return 0;
    }
    return (int)(now / 86400);
}

int PetMeta::record_open_day_and_reward(int today_epoch_day) noexcept
{
    if (today_epoch_day <= 0) {
        ESP_LOGW(TAG, "Skipping streak check: time not set (NTP?)");
        return 0;
    }
    esp_err_t err = ensure_open();
    if (err != ESP_OK) return 0;

    int32_t streak = 0;
    int32_t last_open = 0;
    nvs_get_i32(handle_, kKeyStreak, &streak);
    nvs_get_i32(handle_, kKeyLastOpenDay, &last_open);

    int new_streak = 0;
    int reward = 0;
    if (last_open == 0) {
        new_streak = 1;
        reward = 10;
    } else if (today_epoch_day == last_open) {
        ESP_LOGI(TAG, "Streak unchanged (same day, streak=%d)", streak);
        return streak;
    } else if (today_epoch_day == last_open + 1) {
        new_streak = streak + 1;
        reward = 10;
    } else {
        new_streak = 1;
        reward = 10;
    }

    Pet::instance().add_coins(reward);
    nvs_set_i32(handle_, kKeyStreak, new_streak);
    nvs_set_i32(handle_, kKeyLastOpenDay, today_epoch_day);
    nvs_commit(handle_);
    Pet::instance().record_open_day(today_epoch_day);
    ESP_LOGI(TAG, "Streak updated: streak %d -> %d, +%d coins",
             streak, new_streak, reward);
    return new_streak;
}

void PetMeta::clear() noexcept
{
    if (!open_) {
        esp_err_t err = ensure_open();
        if (err != ESP_OK) return;
    }
    nvs_erase_key(handle_, kKeyStreak);
    nvs_erase_key(handle_, kKeyLastOpenDay);
    nvs_commit(handle_);
}

} // namespace pet
