#include "pet_state.h"
#include "esp_log.h"

namespace pet {

static const char *TAG = "pet_state";

// One level-up every 5 minutes of game time. Tunable.
static constexpr int kLevelTicks = 300;

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
    // fullness drops over time; sleeping slows it down.
    fullness_  = clamp(fullness_ - (sleeping_ ? 1 : 2));
    happiness_ = clamp(happiness_ - (sleeping_ ? 0 : 1));

    if (sleeping_) {
        energy_ = clamp(energy_ + 5);
    } else {
        energy_ = clamp(energy_ - 1);
    }

    // Health drops if neglected, otherwise slowly regenerates.
    if (fullness_ < 20 || happiness_ < 20 || energy_ < 10) {
        health_ = clamp(health_ - 1);
    } else if (health_ < 100) {
        health_ = clamp(health_ + 1);
    }

    // Aging and leveling.
    age_ticks_++;
    recompute_level();

    dirty_ = true;
}

void Pet::update()
{
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed_ms = (now - last_update_tick_) * portTICK_PERIOD_MS;

    const uint32_t TICK_MS = 1000; // decay every second
    while (elapsed_ms >= TICK_MS) {
        decay_tick();
        elapsed_ms -= TICK_MS;
        last_update_tick_ += pdMS_TO_TICKS(TICK_MS);
    }
}

} // namespace pet