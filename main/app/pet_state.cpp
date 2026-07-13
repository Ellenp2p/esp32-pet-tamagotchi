#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"

namespace pet {

static const char *TAG __attribute__((unused)) = "pet_state";

// One level-up every 30 decay ticks = 30 * 120s = 60 minutes of game time.
// Tunable.
static constexpr int kLevelTicks = 30;

// ============================================================================
// Pacing model — all stats use a single 2-minute tick with 50% skip.
//
//   TICK_MS = 120_000 ms (2 minutes per tick)
//   per-tick Δ = -1 (or +1 for regen)  · 50% skip → average ±0.5 per tick
//
// Effective rates (per minute):
//   fullness   awake -1*50%/2min = -0.25/min   → 100→0 in ~400 min (~6.7 h)
//   fullness   sleep -1*50%/2min = -0.25/min   (same — sleep also slows via the
//                                              ↓im_sick threshold, see below)
//   happiness  awake -1*50%/2min = -0.25/min   (≈ 400 min full decay)
//   energy     awake -1*50%/2min = -0.25/min
//   energy     sleep +5*50%/2min = +1.25/min   (full recovery ≈ 80 min)
//   health     sick   -1*50%/2min = -0.25/min
//   health     heal   +1*50%/2min = +0.25/min  (full recovery ≈ 400 min)
//
// Bench targets:
//   - Pet left alone from full stats can survive ~5 hours before any stat
//     drops to the "sick" threshold (20% for fullness/happiness, 10% for
//     energy). It reaches 0% only after ≈6–7 h.
//   - Sleep roughly doubles recovery speed (energy regen is 5x faster
//     while asleep).
//   - Once sick, health drains at the same gentle 0.25/min rate, so the
//     pet doesn't collapse instantly — but it WILL die if ignored for
//     12+ hours total.
//   - 1 Feed (+25 fullness) restores 100 min of "full" state. 1 Play
//     gives +20 happiness but -10 fullness, -15 energy (a real cost).
// ============================================================================
static inline bool p50() { return (esp_random() & 1) == 0; }

Pet &Pet::instance()
{
    static Pet inst;
    return inst;
}

void Pet::init()
{
    last_update_tick_ = xTaskGetTickCount();
    ESP_LOGI(TAG, "Pet initialized: fullness=%d happiness=%d energy=%d health=%d coins=%d",
             fullness_, happiness_, energy_, health_, coins_);
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
    recompute_level();

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