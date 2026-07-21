#include "pet_idle_events.h"
#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"

namespace pet {

static const char *TAG = "idle";

PetIdleEvents &PetIdleEvents::instance() noexcept
{
    static PetIdleEvents s;
    return s;
}

void PetIdleEvents::init() noexcept
{
    next_event_tick_ = 3 + (esp_random() % 6);
    age_at_last_event_ = 0;
}

void PetIdleEvents::tick(int current_age_ticks) noexcept
{
    if (age_at_last_event_ == 0) {
        age_at_last_event_ = current_age_ticks;
        return;
    }
    int delta = current_age_ticks - age_at_last_event_;
    if (delta >= next_event_tick_) {
        int reward = 1 + (esp_random() % 5);
        Pet::instance().add_coins(reward);
        ESP_LOGI(TAG, "Idle event: pet found %d coins", reward);
        age_at_last_event_ = current_age_ticks;
        next_event_tick_ = 3 + (esp_random() % 6);
    }
}

} // namespace pet
