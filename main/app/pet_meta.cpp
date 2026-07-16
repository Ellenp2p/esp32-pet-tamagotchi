#include "pet_meta.h"
#include "pet_state.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <ctime>

namespace pet {
namespace meta {

static const char *TAG = "pet_meta";
static const char *kNamespace = "pet_meta";
static const char *kKeyStreak     = "streak";
static const char *kKeyLastOpenDay = "last_day";

static nvs_handle_t s_handle = 0;
static bool s_open = false;

static esp_err_t ensure_open()
{
    if (s_open) return ESP_OK;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    s_open = true;
    return ESP_OK;
}

int today_epoch_day()
{
    time_t now = 0;
    time(&now);
    if (now < 86400) {
        // System time not set yet (epoch zero or one day); NTP not ready.
        return 0;
    }
    // Days since 1970-01-01. Whole-day math; localised by the user later
    // when they want TZ support. For v0.6 we just use UTC consistently.
    return (int)(now / 86400);
}

int record_open_day_and_reward(int today_epoch_day)
{
    if (today_epoch_day <= 0) {
        ESP_LOGW(TAG, "Skipping streak check: time not set (NTP?)");
        return 0;
    }
    esp_err_t err = ensure_open();
    if (err != ESP_OK) return 0;

    int32_t streak = 0;
    int32_t last_open = 0;
    nvs_get_i32(s_handle, kKeyStreak, &streak);
    nvs_get_i32(s_handle, kKeyLastOpenDay, &last_open);

    int new_streak = 0;
    int reward = 0;
    if (last_open == 0) {
        // First ever open.
        new_streak = 1;
        reward = 10;
    } else if (today_epoch_day == last_open) {
        // Same day, no change.
        ESP_LOGI(TAG, "Streak unchanged (same day, streak=%d)", streak);
        return streak;
    } else if (today_epoch_day == last_open + 1) {
        // Consecutive day.
        new_streak = streak + 1;
        reward = 10;  // simple: 10 coins per day
    } else {
        // Gap ≥ 2 days, streak broken.
        new_streak = 1;
        reward = 10;
    }

    Pet::instance().add_coins(reward);
    nvs_set_i32(s_handle, kKeyStreak, new_streak);
    nvs_set_i32(s_handle, kKeyLastOpenDay, today_epoch_day);
    nvs_commit(s_handle);
    Pet::instance().record_open_day(today_epoch_day);
    ESP_LOGI(TAG, "Streak updated: streak %d -> %d, +%d coins",
             streak, new_streak, reward);
    return new_streak;
}

void clear()
{
    if (!s_open) {
        esp_err_t err = ensure_open();
        if (err != ESP_OK) return;
    }
    nvs_erase_key(s_handle, kKeyStreak);
    nvs_erase_key(s_handle, kKeyLastOpenDay);
    nvs_commit(s_handle);
}

}  // namespace meta
}  // namespace pet