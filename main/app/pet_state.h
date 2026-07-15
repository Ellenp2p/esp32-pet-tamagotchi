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
    int stage;      // LifeStage cast to int (0..5)
};

// v0.6: Tamagotchi-style 6-stage lifecycle. stage_ is derived from
// age_ticks_ + a tombstone rule (Adult + sustained low stats). UI reads
// pet.stage() after each decay_tick to display the current stage name.
enum class LifeStage : int {
    Egg = 0,        // 0-3 min, only feed/pet/sleep available
    Baby = 1,       // 3-30 min, basic 4 actions
    Child = 2,      // 30-60 min, unlocks Sequence Tap
    Teen = 3,       // 60-90 min, unlocks Whack + Gacha
    Adult = 4,      // 90+ min, Shop fully open
    Tombstone = 5,  // Adult + all stats < 20 for 50+ ticks (~100 min)
    Count
};
const char *life_stage_name(LifeStage s);

class Pet {
public:
    static Pet &instance();

    void init();

    // Get current state
    State get_state() const;

    // Actions
    void feed();           // +25 fullness, +5 energy, +5 happiness
    void feed_with_amount(int amount);  // shop-bought snack: amount=fullness delta
    void drink_energy(int amount);     // shop-bought energy drink: amount=energy delta
    void take_medicine(int amount);    // shop-bought medicine: amount=health delta
    void play();
    void sleep();
    void wake_up();
    void pet();            // care / petting

    // v0.6: one-shot work outcome. The mini-games call this when a round
    // ends. stat_key is one of: "f" (fullness), "ha" (happiness), "e"
    // (energy), "he" (health); empty string means no stat change.
    // amount may be negative (cost) or positive (reward). coin_delta is
    // applied via add_coins (which floors at 0). The stat delta is
    // clamped to 0..100.
    void work_outcome(const char *stat_key, int amount, int coin_delta);

    // Currency
    void add_coins(int delta);
    bool spend_coins(int amount);  // returns false if insufficient

    // Update state based on elapsed time; called periodically
    void update();

    bool is_sleeping() const { return sleeping_; }
    LifeStage stage() const { return stage_; }
    int streak() const { return streak_; }

    // Mark the saved snapshot as dirty so the next pet_task tick flushes it.
    void mark_dirty() { dirty_ = true; }
    bool is_dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    // v0.6: called by pet_task after update() to discover if the
    // LifeStage just transitioned; the UI pops a "Stage UP!" toast if
    // true. Clears the flag on read.
    bool consume_stage_changed();

    // v0.6: revive a tombstoned pet. Resets stats to 50, returns to
    // Egg stage with stage_entered_tick_ reset. Used by the tombstone
    // UI button.
    void revive_from_tombstone();

    // v0.6: Daily Streak. Called by pet_meta::check_streak() at boot.
    // Increments the streak if the user opens the pet on a new day,
    // resets if more than 1 day has passed, no-op if same day. last_open
    // is the epoch-day of the user's last open (0 = never). After this
    // call, get_streak() reflects the new value.
    void record_open_day(int today_epoch_day);

    // Restore a complete snapshot from persistent storage. Used by pet_save.
    // v0.6 adds three fields: stage, stage_entered_tick, last_open_day.
    // The streak counter is recomputed from last_open_day vs the new
    // caller's today; the snapshot itself stores yesterday's last_open
    // so the +1/+0 logic still works after a reboot.
    void apply_snapshot(int fullness, int happiness, int energy, int health,
                        int coins, int level, int age_ticks, bool sleeping,
                        int stage, int stage_entered_tick, int last_open_day) {
        fullness_  = fullness;
        happiness_ = happiness;
        energy_    = energy;
        health_    = health;
        coins_     = coins;
        level_     = level;
        age_ticks_ = age_ticks;
        sleeping_  = sleeping;
        stage_     = (LifeStage)stage;
        stage_entered_tick_ = stage_entered_tick;
        last_open_day_ = last_open_day;
        streak_    = 0;  // recomputed on next record_open_day()
        just_changed_stage_ = false;
        dirty_     = false;  // freshly restored, no need to save immediately
    }

    // Getters used by pet_save to write the v2 snapshot.
    int stage_entered_tick() const { return stage_entered_tick_; }
    int last_open_day() const { return last_open_day_; }

private:
    Pet() = default;

    int clamp(int value) const;
    void decay_tick();
    void recompute_level();
    void recompute_stage();  // v0.6: replaces per-decay role of recompute_level

    int fullness_  = 70;
    int happiness_ = 70;
    int energy_    = 70;
    int health_    = 90;
    bool sleeping_ = false;

    int coins_     = 0;
    int level_     = 1;
    int age_ticks_ = 0;

    // v0.6: lifecycle + Daily Streak state
    LifeStage stage_ = LifeStage::Egg;
    int stage_entered_tick_ = 0;
    int last_open_day_ = 0;   // 0 = never opened; otherwise epoch day
    int streak_    = 0;
    bool just_changed_stage_ = false;

    // Internal: low-stats tick counter for tombstone rule.
    int low_stats_ticks_ = 0;

    bool dirty_ = false;

    TickType_t last_update_tick_ = 0;
};

} // namespace pet