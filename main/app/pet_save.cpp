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
static const char *kKeyV1     = "state";     // 29-byte Snapshot (v0.5.x)
static const char *kKeyV2     = "state_v2";   // 41-byte Snapshot (v0.6+)

// v0.6 wire format. Layout is intentionally append-only — new fields are
// added at the end so the first 29 bytes are bit-compatible with v0.5.x
// snapshots and v1 reads can copy old bytes verbatim.
struct Snapshot {
    int32_t fullness;
    int32_t happiness;
    int32_t energy;
    int32_t health;
    int32_t coins;
    int32_t level;
    int32_t age_ticks;
    uint8_t sleeping;

    // v0.6 additions (12 bytes):
    uint8_t  stage;              // LifeStage cast to int
    uint8_t  _pad0;              // explicit padding to keep 4-byte align
    uint8_t  _pad1;
    uint8_t  _pad2;
    int32_t   stage_entered_tick;  // age_ticks at last stage change
    int32_t   last_open_day;       // epoch days, 0 = never
} __attribute__((packed));

static_assert(sizeof(Snapshot) == 41, "Snapshot size changed; update kKeyV2 consumers");

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

// Apply a v1 (29-byte) snapshot to Pet. Used when we find a legacy
// kKeyV1 entry; the v0.6 fields default to Egg / stage_entered_tick=age_ticks
// / last_open_day=0. The streak counter is recomputed on the next
// record_open_day() call.
static void apply_v1(Pet &pet, const Snapshot &s)
{
    pet.apply_snapshot(
        s.fullness, s.happiness, s.energy, s.health,
        s.coins, s.level, s.age_ticks, s.sleeping != 0,
        /*stage=*/ 0,            // Egg
        /*stage_entered_tick=*/ s.age_ticks,
        /*last_open_day=*/ 0);
}

esp_err_t load(Pet &pet)
{
    if (!s_open) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }

    // Try v2 first.
    {
        Snapshot snap;
        size_t size = sizeof(snap);
        esp_err_t err = nvs_get_blob(s_handle, kKeyV2, &snap, &size);
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
        // fall through to v1 attempt
    }

    // Try v1 (29 bytes) — backwards-compat with v0.5.x firmware.
    {
        // We use a stack buffer of exactly 29 bytes. To avoid making a
        // second Snapshot type, reinterpret the first 29 bytes of a
        // zero-initialised Snapshot.
        Snapshot snap{};
        size_t size = 29;  // explicit v1 size
        esp_err_t err = nvs_get_blob(s_handle, kKeyV1, &snap, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved snapshot; using defaults");
            return ESP_OK;  // first boot is fine
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
        // Immediately upgrade to v2 in the background. We don't mark dirty
        // here because the data is freshly restored, but we DO save once
        // to lay down the v2 key.
        return save_if_dirty(pet, /*force=*/true);
    }
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

    esp_err_t err = nvs_set_blob(s_handle, kKeyV2, &snap, sizeof(snap));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob(v2) failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return err;
    }
    pet.clear_dirty();
    ESP_LOGD(TAG, "Snapshot saved (v2)");
    return ESP_OK;
}

esp_err_t clear()
{
    if (!s_open) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }
    // Erase both v1 and v2 keys. Ignore NOT_FOUND — keys may not exist.
    nvs_erase_key(s_handle, kKeyV1);
    nvs_erase_key(s_handle, kKeyV2);
    return nvs_commit(s_handle);
}

}  // namespace save
}  // namespace pet