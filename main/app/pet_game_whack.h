#pragma once

#include "lvgl.h"

namespace pet {

class WhackGame {
public:
    static WhackGame &instance() noexcept;

    static lv_obj_t *build(lv_obj_t *parent);
    static void      destroy(lv_obj_t *root);

private:
    WhackGame() = default;

    static constexpr int kHoles = 3;
    static constexpr int kRounds = 5;
    static constexpr int kShowMs = 1500;
    static constexpr int kGapMs = 600;
    static constexpr int kReward = 5;
    static constexpr int kPenalty = 1;

    enum class State { Idle, Show, Gap, GameOver };

    struct Ctx {
        lv_obj_t *root = nullptr;
        lv_obj_t *holes[kHoles] = {nullptr};
        lv_obj_t *info_label = nullptr;
        lv_obj_t *hint_label = nullptr;
        lv_obj_t *start_btn = nullptr;
        lv_timer_t *timer = nullptr;
        State state = State::Idle;
        int current_hole = -1;
        int round_idx = 0;
        int score = 0;
    };

    Ctx ctx_{};

    void set_info(const char *text);
    void set_hint(const char *text);
    void reset_holes();
    void next_round();
    static void on_timer(lv_timer_t *t);
    static void on_hole_click(lv_event_t *e);
    static void on_start(lv_event_t *e);
};

} // namespace pet
