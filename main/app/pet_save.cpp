#include "pet_save.h"
#include "pet_state.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <cstring>

namespace pet {
namespace save {

static const char *TAG = "pet_save";
static const char *kNamespace = "pet_save";
static const char *kKey = "state";

// Wire format kept tight: 6 ints (24 bytes) + 1 byte sleeping flag.
struct Snapshot {
    int32_t fullness;
    int32_t happiness;
    int32_t energy;
    int32_t health;
    int32_t coins;
    int32_t level;
    int32_t age_ticks;
    uint8_t sleeping;
} __attribute__((packed));

static_assert(sizeof(Snapshot) == 29, "Snapshot size changed; bump kKey version");

static nvs_handle_t s_handle = 0;
static bool s_open = false;

esp_err_t init()
{
    if (s_open) return ESP_OK;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    s_open = true;
    ESP_LOGI(TAG, "NVS namespace '%s' ready", kNamespace);
    return ESP_OK;
}

esp_err_t load(Pet &pet)
{
    if (!s_open) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }

    Snapshot snap;
    size_t size = sizeof(snap);
    esp_err_t err = nvs_get_blob(s_handle, kKey, &snap, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved snapshot; using defaults");
        return ESP_OK;  // first boot is fine
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob failed: %s", esp_err_to_name(err));
        return err;
    }
    if (size != sizeof(snap)) {
        ESP_LOGW(TAG, "Snapshot size mismatch (%u != %u); ignoring", size, sizeof(snap));
        return ESP_OK;
    }

    // Reach into the singleton via the public apply_snapshot() hook.
    pet.apply_snapshot(snap.fullness, snap.happiness, snap.energy,
                       snap.health, snap.coins, snap.level,
                       snap.age_ticks, snap.sleeping != 0);

    ESP_LOGI(TAG, "Loaded snapshot: F=%d Ha=%d E=%d He=%d coins=%d Lv%d age=%ds sleeping=%d",
             snap.fullness, snap.happiness, snap.energy, snap.health,
             snap.coins, snap.level, snap.age_ticks, snap.sleeping);
    return ESP_OK;
}

esp_err_t save_if_dirty(Pet &pet, bool force)
{
    if (!s_open) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }
    if (!force && !pet.is_dirty()) {
        return ESP_OK;
    }

    State s = pet.get_state();
    uint8_t sleeping_byte = pet.is_sleeping() ? 1 : 0;
    Snapshot snap = {
        .fullness  = s.fullness,
        .happiness = s.happiness,
        .energy    = s.energy,
        .health    = s.health,
        .coins     = s.coins,
        .level     = s.level,
        .age_ticks = s.age_ticks,
        .sleeping  = sleeping_byte,
    };

    esp_err_t err = nvs_set_blob(s_handle, kKey, &snap, sizeof(snap));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return err;
    }
    pet.clear_dirty();
    ESP_LOGD(TAG, "Snapshot saved");
    return ESP_OK;
}

esp_err_t clear()
{
    if (!s_open) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }
    esp_err_t err = nvs_erase_key(s_handle, kKey);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_key failed: %s", esp_err_to_name(err));
        return err;
    }
    return nvs_commit(s_handle);
}

}  // namespace save
}  // namespace pet