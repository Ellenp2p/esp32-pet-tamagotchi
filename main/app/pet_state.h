#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pet {

// All four stats use the same convention: 0 = bad, 100 = good.
struct State {
    int fullness;   // 0 = starving, 100 = full
    int happiness;  // 0 = sad, 100 = happy
    int energy;     // 0 = exhausted, 100 = energetic
    int health;     // 0 = sick, 100 = healthy
    int coins;      // currency (biscuits), can grow unbounded
    int level;      // 1..N, derived from age_ticks
    int age_ticks;  // number of decay_tick() calls; one per second of game time
};

class Pet {
public:
    static Pet &instance();

    void init();

    // Get current state
    State get_state() const;

    // Actions
    void feed();           // +25 fullness, +5 energy, +5 happiness
    void feed_with_amount(int amount);  // shop-bought snack: amount=fullness delta
    void play();
    void sleep();
    void wake_up();
    void pet();            // care / petting

    // Currency
    void add_coins(int delta);
    bool spend_coins(int amount);  // returns false if insufficient

    // Update state based on elapsed time; called periodically
    void update();

    bool is_sleeping() const { return sleeping_; }

    // Mark the saved snapshot as dirty so the next pet_task tick flushes it.
    void mark_dirty() { dirty_ = true; }
    bool is_dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    // Restore a complete snapshot from persistent storage. Used by pet_save.
    void apply_snapshot(int fullness, int happiness, int energy, int health,
                        int coins, int level, int age_ticks, bool sleeping) {
        fullness_  = fullness;
        happiness_ = happiness;
        energy_    = energy;
        health_    = health;
        coins_     = coins;
        level_     = level;
        age_ticks_ = age_ticks;
        sleeping_  = sleeping;
        dirty_     = false;  // freshly restored, no need to save immediately
    }

private:
    Pet() = default;

    int clamp(int value) const;
    void decay_tick();
    void recompute_level();

    int fullness_  = 70;
    int happiness_ = 70;
    int energy_    = 70;
    int health_    = 90;
    bool sleeping_ = false;

    int coins_     = 0;
    int level_     = 1;
    int age_ticks_ = 0;

    bool dirty_ = false;

    TickType_t last_update_tick_ = 0;
};

} // namespace pet