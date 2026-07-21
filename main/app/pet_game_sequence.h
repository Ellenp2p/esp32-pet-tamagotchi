#pragma once

#include "lvgl.h"

namespace pet {

class SequenceGame {
public:
    static SequenceGame &instance() noexcept;

    static lv_obj_t *build(lv_obj_t *parent);
    static void      destroy(lv_obj_t *root);

private:
    SequenceGame() = default;

    static constexpr int kTiles  = 9;
    static constexpr int kReward = 30;

    struct Ctx {
        lv_obj_t *root = nullptr;
        lv_obj_t *tiles[kTiles] = {nullptr};
        lv_obj_t *info_label = nullptr;
        int order[kTiles] = {0};
        int next_expected = 1;
    };

    Ctx ctx_{};

    void shuffle(int *arr, int n);
    void restart_round();
    static void on_tile_click(lv_event_t *e);
};

} // namespace pet
