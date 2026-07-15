#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"

namespace pet {

static const char *TAG __attribute__((unused)) = "pet_state";

// One level-up every 30 decay ticks = 30 * 120s = 60 minutes of game time.
// Tunable.
static constexpr int kLevelTicks = 30;

// ============================================================================
// v0.6 Pacing model — same as v0.5.1 (see header for math) plus stage transitions:
//
//   age_ticks (×120s)    stage     unlocks
//   0-9   (0-3 min)       Egg       feed/pet/sleep only
//   10-89 (3-30 min)      Baby      all 4 base actions
//   90-179 (30-60 min)    Child     + Sequence Tap
//   180-269 (60-90 min)   Teen      + Whack + Gacha
//   270-449 (90-150 min)  Adult     Shop fully open
//   450+                  Adult (continues)
//   Adult + low_stats_ticks_ > 50 → Tombstone (~100 min all < 20)
//   User taps tombstone → 1-min grace, then back to Egg
//
// Stage-entered-tick tracks when the current stage started, used to detect
// "has the pet been Adult long enough that I should give up on it."
// ============================================================================

// Threshold for "low stats" (drives the Adult → Tombstone transition).
static constexpr int kLowStatThreshold = 20;
// Number of decay-ticks all stats must be below threshold before
// Adult transitions to Tombstone.
static constexpr int kLowStatTicksForTombstone = 50;

const char *life_stage_name(LifeStage s)
{
    switch (s) {
        case LifeStage::Egg:       return "Egg";
        case LifeStage::Baby:      return "Baby";
        case LifeStage::Child:     return "Child";
        case LifeStage::Teen:      return "Teen";
        case LifeStage::Adult:     return "Adult";
        case LifeStage::Tombstone: return "Tombstone";
        case LifeStage::Count:     return "?";
    }
    return "?";
}

static LifeStage stage_for_age_ticks(int age_ticks)
{
    if (age_ticks < 10)  return LifeStage::Egg;
    if (age_ticks < 90)  return LifeStage::Baby;
    if (age_ticks < 180) return LifeStage::Child;
    if (age_ticks < 270) return LifeStage::Teen;
    return LifeStage::Adult;
}

static inline bool p50() { return (esp_random() & 1) == 0; }

Pet &Pet::instance()
{
    static Pet inst;
    return inst;
}

void Pet::init()
{
    last_update_tick_ = xTaskGetTickCount();
    stage_ = LifeStage::Egg;
    stage_entered_tick_ = 0;
    just_changed_stage_ = false;
    low_stats_ticks_ = 0;
    streak_ = 0;
    ESP_LOGI(TAG, "Pet initialized: F=%d Ha=%d E=%d He=%d coins=%d stage=%s",
             fullness_, happiness_, energy_, health_, coins_,
             life_stage_name(stage_));
}

int Pet::clamp(int value) const
{
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
}

void Pet::recompute_level()
{
    int new_level = (age_ticks_ / kLevelTicks) + 1;
    if (new_level != level_) {
        level_ = new_level;
        dirty_ = true;
        ESP_LOGI(TAG, "Level up! Lv %d", level_);
    }
}

void Pet::recompute_stage()
{
    recompute_level();

    LifeStage base = stage_for_age_ticks(age_ticks_);

    // Tombstone rule: only Adult (not Egg/Baby/etc) can tombstone, and only
    // after a sustained low-stats period.
    if (base == LifeStage::Adult && stage_ != LifeStage::Tombstone) {
        bool low = (fullness_  < kLowStatThreshold)
                || (happiness_ < kLowStatThreshold)
                || (energy_    < 10);
        if (low) {
            low_stats_ticks_++;
        } else {
            low_stats_ticks_ = 0;
        }
        if (low_stats_ticks_ >= kLowStatTicksForTombstone) {
            base = LifeStage::Tombstone;
        }
    } else {
        // Reset the counter whenever we are not Adult (so going Adult →
        // low stats doesn't immediately tombstone on the first tick).
        low_stats_ticks_ = 0;
    }

    if (base != stage_) {
        stage_ = base;
        stage_entered_tick_ = age_ticks_;
        just_changed_stage_ = true;
        dirty_ = true;
        ESP_LOGW(TAG, "Stage changed to %s (age_ticks=%d)",
                 life_stage_name(stage_), age_ticks_);
    }
}

State Pet::get_state() const
{
    return State{
        .fullness  = fullness_,
        .happiness = happiness_,
        .energy    = energy_,
        .health    = health_,
        .coins     = coins_,
        .level     = level_,
        .age_ticks = age_ticks_,
        .stage     = (int)stage_,
    };
}

void Pet::feed()
{
    fullness_  = clamp(fullness_ + 25);
    energy_    = clamp(energy_ + 5);
    happiness_ = clamp(happiness_ + 5);
    dirty_ = true;
    ESP_LOGI(TAG, "Fed pet");
}

void Pet::feed_with_amount(int amount)
{
    if (amount <= 0) return;
    fullness_  = clamp(fullness_ + amount);
    happiness_ = clamp(happiness_ + 2);
    dirty_ = true;
    ESP_LOGI(TAG, "Ate snack (+%d fullness)", amount);
}

void Pet::drink_energy(int amount)
{
    if (amount <= 0) return;
    energy_ = clamp(energy_ + amount);
    happiness_ = clamp(happiness_ + 1);
    dirty_ = true;
    ESP_LOGI(TAG, "Drank energy (+%d)", amount);
}

void Pet::take_medicine(int amount)
{
    if (amount <= 0) return;
    health_ = clamp(health_ + amount);
    fullness_ = clamp(fullness_ - 2);  // medicine is bitter
    dirty_ = true;
    ESP_LOGI(TAG, "Took medicine (+%d health, -2 fullness)", amount);
}

void Pet::play()
{
    if (sleeping_) {
        wake_up();
        return;
    }
    fullness_  = clamp(fullness_ - 10);
    energy_    = clamp(energy_ - 15);
    happiness_ = clamp(happiness_ + 20);
    health_    = clamp(health_ + 2);
    dirty_ = true;
    ESP_LOGI(TAG, "Played with pet");
}

void Pet::sleep()
{
    sleeping_ = true;
    dirty_ = true;
    ESP_LOGI(TAG, "Pet sleeping");
}

void Pet::wake_up()
{
    sleeping_ = false;
    happiness_ = clamp(happiness_ - 5);
    dirty_ = true;
    ESP_LOGI(TAG, "Pet woke up");
}

void Pet::pet()
{
    happiness_ = clamp(happiness_ + 10);
    health_    = clamp(health_ + 2);
    dirty_ = true;
    ESP_LOGI(TAG, "Pet petted");
}

// v0.6: one-shot work outcome used by the 3 mini-games. The caller passes
// a stat key, an amount (positive reward or negative cost), and a coin
// delta. Empty stat_key = no stat change. Both deltas are clamped/dir
// independently so a game can spend energy while gaining happiness.
void Pet::work_outcome(const char *stat_key, int amount, int coin_delta)
{
    if (stat_key && stat_key[0]) {
        if      (stat_key[0] == 'f'  && stat_key[1] == 0) fullness_  = clamp(fullness_  + amount);
        else if (stat_key[0] == 'h'  && stat_key[1] == 'a') happiness_ = clamp(happiness_ + amount);
        else if (stat_key[0] == 'h'  && stat_key[1] == 'e') health_    = clamp(health_    + amount);
        else if (stat_key[0] == 'e'  && stat_key[1] == 0) energy_    = clamp(energy_    + amount);
    }
    if (coin_delta > 0) add_coins(coin_delta);
    else if (coin_delta < 0) {
        // Spend path: take coins away even if it would go negative — clamp
        // at 0 only if insufficient; otherwise go through normal path so
        // the dirty flag is set.
        coins_ += coin_delta;
        if (coins_ < 0) coins_ = 0;
        dirty_ = true;
    }
    dirty_ = true;  // always mark dirty if anything happened
    ESP_LOGI(TAG, "Work outcome: stat=%s amt=%d coins=%+d",
             stat_key ? stat_key : "", amount, coin_delta);
}

void Pet::add_coins(int delta)
{
    if (delta == 0) return;
    coins_ += delta;
    if (coins_ < 0) coins_ = 0;  // floor at zero
    dirty_ = true;
    ESP_LOGI(TAG, "Coins %+d -> %d", delta, coins_);
}

bool Pet::spend_coins(int amount)
{
    if (amount <= 0) return true;
    if (coins_ < amount) return false;
    coins_ -= amount;
    dirty_ = true;
    ESP_LOGI(TAG, "Spent %d coins -> %d", amount, coins_);
    return true;
}

bool Pet::consume_stage_changed()
{
    bool r = just_changed_stage_;
    just_changed_stage_ = false;
    return r;
}

void Pet::revive_from_tombstone()
{
    // Reset stats to 50, send back to Egg, and clear tombstone counters.
    fullness_  = 50;
    happiness_ = 50;
    energy_    = 50;
    health_    = 50;
    stage_ = LifeStage::Egg;
    stage_entered_tick_ = age_ticks_;
    low_stats_ticks_ = 0;
    just_changed_stage_ = true;
    dirty_ = true;
    ESP_LOGW(TAG, "Tombstone revived → Egg");
}

void Pet::record_open_day(int today_epoch_day)
{
    if (today_epoch_day <= 0) {
        streak_ = 0;
        last_open_day_ = 0;
        return;
    }
    if (last_open_day_ == 0) {
        // First ever open.
        streak_ = 1;
    } else if (today_epoch_day == last_open_day_) {
        // Same day, no change.
    } else if (today_epoch_day == last_open_day_ + 1) {
        // Consecutive day.
        streak_++;
    } else {
        // Gap ≥ 2 days, streak broken.
        streak_ = 1;
    }
    last_open_day_ = today_epoch_day;
    dirty_ = true;
    ESP_LOGI(TAG, "Daily streak updated: today=%d last=%d streak=%d",
             today_epoch_day, last_open_day_, streak_);
}

void Pet::decay_tick()
{
    // fullness drops slowly; sleeping doesn't slow it further (sleep only
    // affects energy direction).
    if (p50()) fullness_ = clamp(fullness_ - 1);

    int happiness_drop = sleeping_ ? 0 : 1;
    if (happiness_drop > 0 && p50()) happiness_ = clamp(happiness_ - happiness_drop);

    // Energy: sleeping regenerates much faster than awake drains.
    if (sleeping_) {
        if (p50()) energy_ = clamp(energy_ + 5);
    } else {
        if (p50()) energy_ = clamp(energy_ - 1);
    }

    // Health drops if neglected, otherwise slowly regenerates.
    bool sick = (fullness_ < 20 || happiness_ < 20 || energy_ < 10);
    if (sick) {
        if (p50()) health_ = clamp(health_ - 1);
    } else if (health_ < 100) {
        if (p50()) health_ = clamp(health_ + 1);
    }

    // Aging and leveling (always; these are not rate-sensitive).
    age_ticks_++;
    recompute_stage();  // v0.6: replaces recompute_level()

    dirty_ = true;
}

void Pet::update()
{
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed_ms = (now - last_update_tick_) * portTICK_PERIOD_MS;

    // Decay every 2 minutes. With 50% skip this yields ~5–7 hour survival
    // from full stats; see the pacing block at the top of this file.
    const uint32_t TICK_MS = 120000;
    while (elapsed_ms >= TICK_MS) {
        decay_tick();
        elapsed_ms -= TICK_MS;
        last_update_tick_ += pdMS_TO_TICKS(TICK_MS);
    }
}

} // namespace pet