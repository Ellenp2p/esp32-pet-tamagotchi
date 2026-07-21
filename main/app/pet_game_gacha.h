#pragma once

#include "lvgl.h"
#include <cstdint>

namespace pet {

class GachaGame {
public:
    static GachaGame &instance() noexcept;

    static lv_obj_t *build(lv_obj_t *parent);
    static void      destroy(lv_obj_t *root);
    static int       pull_one();

private:
    GachaGame() = default;

    static constexpr int kRarities   = 5;
    static constexpr int kPerRarity  = 6;
    static constexpr int kAlbumSlots = kRarities * kPerRarity;
    static constexpr int kPullCost   = 10;

    struct CardDef {
        const char *name;
        const char *icon;
        uint32_t    color;
        int         coin_reward;
    };

    struct Ctx {
        lv_obj_t *root     = nullptr;
        lv_obj_t *card_label = nullptr;
        lv_obj_t *card_icon  = nullptr;
        lv_obj_t *card_bg    = nullptr;
        lv_obj_t *info_label = nullptr;
        lv_obj_t *pull_btn   = nullptr;
        lv_obj_t *album_btn  = nullptr;
        lv_timer_t *anim_timer  = nullptr;
        int anim_ticks       = 0;
        int anim_last_rarity = 0;
        int anim_last_idx    = 0;
        bool anim_showing_result = false;
        bool anim_in_progress    = false;
    };

    static const CardDef kCards[kRarities][kPerRarity];
    static const char *kRarityName[kRarities];
    static const int   kWeights[kRarities];

    Ctx    ctx_{};
    uint8_t album_[kAlbumSlots]{};
    bool    album_loaded_ = false;

    void album_load();
    void album_save();
    int  rarity_pick();
    void set_card_display(const CardDef &c, const char *rarity);
    void anim_tick();
    static void on_anim_timer(lv_timer_t *t);
    static void on_pull(lv_event_t *e);
    static void on_album(lv_event_t *e);
};

} // namespace pet
