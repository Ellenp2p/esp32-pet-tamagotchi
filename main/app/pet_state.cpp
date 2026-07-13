#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"

namespace pet {

static const char *TAG __attribute__((unused)) = "pet_state";

// One level-up every 30 decay ticks = 30 * 10s = 5 minutes of game time.
// Tunable.
static constexpr int kLevelTicks = 30;

// Rate scaling: every decay step happens once every TICK_MS (10s), and each
// numeric decrement has a 50% chance of being skipped. Effective per-second
// rate at TICK_MS=10s and 50% skip:
//   fullness  awake:  -2/10s * 50% = -0.10/s   (70 -> 20 in ~8 min,
//                                                70 -> 0  in ~12 min)
//   fullness  sleep:  -1/10s * 50% = -0.05/s   (sleep slows decay 2x)
//   happiness awake:  -1/10s * 50% = -0.05/s
//   energy    awake:  -1/10s * 50% = -0.05/s
//   energy    sleep:  +5/10s * 50% = +0.25/s   (sleep recovers ~12 min full)
//   health    sick:   -1/10s * 50% = -0.05/s
//   health    heal:   +1/10s * 50% = +0.05/s
//
// Goal: pet only needs meaningful attention (Feed/Play/Sleep) once every
// 30–60 minutes, not every minute. This keeps the "obligation to care"
// sparse enough that the device can be left alone for hours without the
// pet dying, while still feeling alive enough that stats visibly drift
// during a normal session.
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
    // fullness drops over time; sleeping slows it down. 50% skip for halved rate.
    int fullness_drop = sleeping_ ? 1 : 2;
    if (p50()) fullness_ = clamp(fullness_ - fullness_drop);

    int happiness_drop = sleeping_ ? 0 : 1;
    if (happiness_drop > 0 && p50()) happiness_ = clamp(happiness_ - happiness_drop);

    // Energy: sleeping regenerates faster. Halve the delta via 50% chance.
    if (sleeping_) {
        if (p50()) energy_ = clamp(energy_ + 5);
    } else {
        if (p50()) energy_ = clamp(energy_ - 1);
    }

    // Health drops if neglected, otherwise slowly regenerates. Halved rate.
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

    // Decay every 10 seconds (combined with 50% skip in decay_tick this
    // gives a per-second rate roughly 1/20 of the raw -2 base — i.e. on the
    // order of minutes-to-hours for visible stat change).
    const uint32_t TICK_MS = 10000;
    while (elapsed_ms >= TICK_MS) {
        decay_tick();
        elapsed_ms -= TICK_MS;
        last_update_tick_ += pdMS_TO_TICKS(TICK_MS);
    }
}

} // namespace pet