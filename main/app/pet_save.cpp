#include "pet_save.h"
#include "pet_state.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include <cstring>

namespace pet {

static const char *TAG = "pet_save";
static const char *kNamespace = "pet_save";
static const char *kKeyV1     = "state";
static const char *kKeyV2     = "state_v2";

struct Snapshot {
    int32_t fullness;
    int32_t happiness;
    int32_t energy;
    int32_t health;
    int32_t coins;
    int32_t level;
    int32_t age_ticks;
    uint8_t sleeping;

    uint8_t  stage;
    uint8_t  _pad0;
    uint8_t  _pad1;
    uint8_t  _pad2;
    int32_t  stage_entered_tick;
    int32_t  last_open_day;
} __attribute__((packed));

static_assert(sizeof(Snapshot) == 41, "Snapshot size changed; update kKeyV2 consumers");

PetSave &PetSave::instance() noexcept
{
    static PetSave s;
    return s;
}

esp_err_t PetSave::init() noexcept
{
    if (open_) return ESP_OK;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    open_ = true;
    ESP_LOGI(TAG, "NVS namespace '%s' ready", kNamespace);
    return ESP_OK;
}

static void apply_v1(Pet &pet, const Snapshot &s)
{
    pet.apply_snapshot(
        s.fullness, s.happiness, s.energy, s.health,
        s.coins, s.level, s.age_ticks, s.sleeping != 0,
        0, s.age_ticks, 0);
}

esp_err_t PetSave::load(Pet &pet) noexcept
{
    if (!open_) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }

    {
        Snapshot snap;
        size_t size = sizeof(snap);
        esp_err_t err = nvs_get_blob(handle_, kKeyV2, &snap, &size);
        if (err == ESP_OK) {
            if (size == sizeof(snap)) {
                pet.apply_snapshot(
                    snap.fullness, snap.happiness, snap.energy, snap.health,
                    snap.coins, snap.level, snap.age_ticks, snap.sleeping != 0,
                    snap.stage, snap.stage_entered_tick, snap.last_open_day);
                ESP_LOGI(TAG, "Loaded v2 snapshot: F=%d Ha=%d E=%d He=%d coins=%d Lv%d age=%ds sleep=%d stage=%d last_open=%d",
                         snap.fullness, snap.happiness, snap.energy, snap.health,
                         snap.coins, snap.level, snap.age_ticks, snap.sleeping,
                         snap.stage, snap.last_open_day);
                return ESP_OK;
            }
            ESP_LOGW(TAG, "v2 snapshot size mismatch (%u != %u); trying v1",
                     size, sizeof(snap));
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "nvs_get_blob(v2) failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    {
        Snapshot snap{};
        size_t size = 29;
        esp_err_t err = nvs_get_blob(handle_, kKeyV1, &snap, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved snapshot; using defaults");
            return ESP_OK;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_blob(v1) failed: %s", esp_err_to_name(err));
            return err;
        }
        if (size != 29) {
            ESP_LOGW(TAG, "v1 snapshot size mismatch (%u != 29); ignoring", size);
            return ESP_OK;
        }
        apply_v1(pet, snap);
        ESP_LOGI(TAG, "Loaded v1 snapshot (v0.5.x); migrated: F=%d Ha=%d E=%d He=%d coins=%d Lv%d age=%ds sleep=%d",
                  snap.fullness, snap.happiness, snap.energy, snap.health,
                  snap.coins, snap.level, snap.age_ticks, snap.sleeping);
        return save_if_dirty(pet, true);
    }
}

esp_err_t PetSave::save_if_dirty(Pet &pet, bool force) noexcept
{
    if (!open_) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }
    if (!force && !pet.is_dirty()) {
        return ESP_OK;
    }

    State s = pet.get_state();
    Snapshot snap = {
        .fullness         = s.fullness,
        .happiness        = s.happiness,
        .energy           = s.energy,
        .health           = s.health,
        .coins            = s.coins,
        .level            = s.level,
        .age_ticks        = s.age_ticks,
        .sleeping         = (uint8_t)(pet.is_sleeping() ? 1 : 0),
        .stage            = (uint8_t)s.stage,
        ._pad0 = 0, ._pad1 = 0, ._pad2 = 0,
        .stage_entered_tick = pet.stage_entered_tick(),
        .last_open_day    = pet.last_open_day(),
    };

    esp_err_t err = nvs_set_blob(handle_, kKeyV2, &snap, sizeof(snap));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob(v2) failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return err;
    }
    pet.clear_dirty();
    ESP_LOGD(TAG, "Snapshot saved (v2)");
    return ESP_OK;
}

esp_err_t PetSave::clear() noexcept
{
    if (!open_) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }
    nvs_erase_key(handle_, kKeyV1);
    nvs_erase_key(handle_, kKeyV2);
    return nvs_commit(handle_);
}

} // namespace pet
