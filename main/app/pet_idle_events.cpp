#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pet {
namespace idle_events {

static const char *TAG = "idle";

// Random interval between idle events, in ticks (100ms each).
// 60s..120s => 600..1200 ticks.
static int s_next_event_tick = 0;
static int s_age_at_last_event = 0;

void init()
{
    s_next_event_tick = 600 + (esp_random() % 600);
    s_age_at_last_event = 0;
}

// Called from pet_task every 100ms with the current age_ticks (game seconds).
void tick(int current_age_ticks)
{
    if (s_age_at_last_event == 0) {
        s_age_at_last_event = current_age_ticks;
        return;
    }
    int delta = current_age_ticks - s_age_at_last_event;
    if (delta >= s_next_event_tick) {
        // Pick a small random reward: 1..5 coins.
        int reward = 1 + (esp_random() % 5);
        Pet::instance().add_coins(reward);
        ESP_LOGI(TAG, "Idle event: pet found %d coins", reward);
        s_age_at_last_event = current_age_ticks;
        s_next_event_tick = 600 + (esp_random() % 600);  // 60..120s
    }
}

}  // namespace idle_events
}  // namespace pet