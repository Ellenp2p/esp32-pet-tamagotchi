#include "pet_game_gacha.h"
#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <cstring>

namespace pet {

static const char *TAG __attribute__((unused)) = "gacha";

// ---- static data -----------------------------------------------------------

const GachaGame::CardDef GachaGame::kCards[kRarities][kPerRarity] = {
    { {"Bread",   "br", 0xFFB300, 5}, {"Apple",   "ap", 0xF4511E, 5},
      {"Fish",    "fi", 0x42A5F5, 5}, {"Leaf",    "lf", 0x66BB6A, 5},
      {"Cup",     "cu", 0xAB47BC, 5}, {"Star",    "st", 0xFFA726, 5} },
    { {"Cake",    "ck", 0xEC407A, 15}, {"Shine",   "sh", 0xFFEE58, 15},
      {"Coin",    "co", 0xFFCA28, 15}, {"Gem",     "gm", 0x26A69A, 15},
      {"Hat",     "ht", 0x8D6E63, 15}, {"Bow",     "bw", 0xEF5350, 15} },
    { {"Crown",   "cr", 0xFFB300, 50}, {"Bell",    "bl", 0xD4E157, 50},
      {"Mask",    "mk", 0x7E57C2, 50}, {"Lyre",    "lr", 0x5C6BC0, 50},
      {"Wing",    "wg", 0x29B6F6, 50}, {"Heart",   "hr", 0xEF5350, 50} },
    { {"Phoenix", "ph", 0xFF6F00, 200}, {"Unicorn", "un", 0xE1BEE7, 200},
      {"Dragon",  "dr", 0xC62828, 200}, {"Kraken",  "kr", 0x1565C0, 200},
      {"Comet",   "co", 0x00B8D4, 200}, {"Aurora",  "au", 0x00BFA5, 200} },
    { {"Galaxies","ga", 0xFFD600, 1000}, {"Time",    "ti", 0xF57F17, 1000},
      {"Nebula",  "nb", 0x4527A0, 1000}, {"Eternal", "et", 0x1A237E, 1000},
      {"Origin",  "og", 0x4A148C, 1000}, {"Void",    "vd", 0x000000, 1000} },
};

const char *GachaGame::kRarityName[kRarities] = {
    "Common", "Uncommon", "Rare", "Epic", "Legendary",
};

const int GachaGame::kWeights[kRarities] = {60, 85, 95, 99, 100};

// ---- singleton -------------------------------------------------------------

GachaGame &GachaGame::instance() noexcept
{
    static GachaGame s;
    return s;
}

// ---- persistence -----------------------------------------------------------

void GachaGame::album_load()
{
    if (album_loaded_) return;
    nvs_handle_t h;
    if (nvs_open("gacha_album", NVS_READWRITE, &h) != ESP_OK) return;
    size_t sz = sizeof(album_);
    if (nvs_get_blob(h, "owned", album_, &sz) != ESP_OK) {
        memset(album_, 0, sizeof(album_));
    }
    nvs_close(h);
    album_loaded_ = true;
}

void GachaGame::album_save()
{
    nvs_handle_t h;
    if (nvs_open("gacha_album", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "owned", album_, sizeof(album_));
    nvs_commit(h);
    nvs_close(h);
}

// ---- pull logic ------------------------------------------------------------

int GachaGame::rarity_pick()
{
    int r = esp_random() % 100;
    for (int i = 0; i < kRarities; i++) {
        if (r < kWeights[i]) return i;
    }
    return 0;
}

int GachaGame::pull_one()
{
    auto &self = instance();
    self.album_load();
    int rarity = self.rarity_pick();
    int idx = esp_random() % kPerRarity;
    int slot = rarity * kPerRarity + idx;
    self.album_[slot] = 1;
    self.album_save();
    const CardDef &c = kCards[rarity][idx];
    Pet::instance().add_coins(c.coin_reward);
    ESP_LOGI(TAG, "Pulled: %s (%s) -> +%d coins", c.name, kRarityName[rarity], c.coin_reward);
    return rarity;
}

// ---- UI helpers ------------------------------------------------------------

void GachaGame::set_card_display(const CardDef &c, const char *rarity)
{
    lv_obj_set_style_bg_color(ctx_.card_bg, lv_color_hex(c.color), 0);
    lv_label_set_text(ctx_.card_icon, c.icon);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s\n[%s]\n+%d coins", c.name, rarity, c.coin_reward);
    lv_label_set_text(ctx_.card_label, buf);
}

void GachaGame::anim_tick()
{
    if (!ctx_.anim_in_progress) return;
    ctx_.anim_ticks++;
    if (ctx_.anim_ticks < 12) {
        int r = esp_random() % kRarities;
        int i = esp_random() % kPerRarity;
        char rname[16];
        snprintf(rname, sizeof(rname), "%s", kRarityName[r]);
        set_card_display(kCards[r][i], rname);
    } else {
        const CardDef &c = kCards[ctx_.anim_last_rarity][ctx_.anim_last_idx];
        set_card_display(c, kRarityName[ctx_.anim_last_rarity]);
        ctx_.anim_in_progress = false;
        ctx_.anim_showing_result = true;
        lv_obj_clear_state(ctx_.pull_btn, LV_STATE_DISABLED);
    }
}

void GachaGame::on_anim_timer(lv_timer_t *t)
{
    static_cast<GachaGame *>(lv_timer_get_user_data(t))->anim_tick();
}

void GachaGame::on_pull(lv_event_t *e)
{
    (void)e;
    auto &self = instance();
    if (self.ctx_.anim_in_progress) return;
    if (!Pet::instance().spend_coins(kPullCost)) {
        lv_label_set_text(self.ctx_.info_label, "Need 10 coins");
        return;
    }
    int rarity = self.rarity_pick();
    int idx = esp_random() % kPerRarity;
    int slot = rarity * kPerRarity + idx;
    self.album_[slot] = 1;
    self.album_save();
    const CardDef &c = kCards[rarity][idx];
    Pet::instance().add_coins(c.coin_reward);

    self.ctx_.anim_last_rarity = rarity;
    self.ctx_.anim_last_idx = idx;
    self.ctx_.anim_ticks = 0;
    self.ctx_.anim_in_progress = true;
    lv_obj_add_state(self.ctx_.pull_btn, LV_STATE_DISABLED);

    char buf[48];
    snprintf(buf, sizeof(buf), "Pulling...");
    lv_label_set_text(self.ctx_.info_label, buf);
}

void GachaGame::on_album(lv_event_t *e)
{
    (void)e;
    auto &self = instance();
    self.album_load();
    int owned_count = 0;
    for (int i = 0; i < kAlbumSlots; i++) if (self.album_[i]) owned_count++;
    char buf[64];
    snprintf(buf, sizeof(buf), "Album: %d / %d collected", owned_count, kAlbumSlots);
    lv_label_set_text(self.ctx_.info_label, buf);
}

// ---- build / destroy -------------------------------------------------------

lv_obj_t *GachaGame::build(lv_obj_t *parent)
{
    auto &self = instance();
    self.ctx_ = Ctx{};
    self.album_load();

    self.ctx_.root = lv_obj_create(parent);
    lv_obj_set_size(self.ctx_.root, 320, 208);
    lv_obj_set_style_bg_color(self.ctx_.root, lv_color_black(), 0);
    lv_obj_set_style_border_width(self.ctx_.root, 0, 0);
    lv_obj_set_style_pad_all(self.ctx_.root, 0, 0);

    lv_obj_t *title = lv_label_create(self.ctx_.root);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Gacha");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    self.ctx_.card_bg = lv_obj_create(self.ctx_.root);
    lv_obj_set_size(self.ctx_.card_bg, 140, 110);
    lv_obj_align(self.ctx_.card_bg, LV_ALIGN_TOP_LEFT, 20, 30);
    lv_obj_set_style_bg_color(self.ctx_.card_bg, lv_color_hex(0x37474F), 0);
    lv_obj_set_style_border_width(self.ctx_.card_bg, 0, 0);
    lv_obj_set_style_radius(self.ctx_.card_bg, 12, 0);

    self.ctx_.card_icon = lv_label_create(self.ctx_.card_bg);
    lv_obj_set_style_text_font(self.ctx_.card_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(self.ctx_.card_icon, lv_color_white(), 0);
    lv_label_set_text(self.ctx_.card_icon, "?");
    lv_obj_align(self.ctx_.card_icon, LV_ALIGN_TOP_MID, 0, 6);

    self.ctx_.card_label = lv_label_create(self.ctx_.card_bg);
    lv_obj_set_style_text_font(self.ctx_.card_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(self.ctx_.card_label, lv_color_white(), 0);
    lv_label_set_text(self.ctx_.card_label, "Tap PULL");
    lv_obj_align(self.ctx_.card_label, LV_ALIGN_BOTTOM_MID, 0, -6);

    self.ctx_.info_label = lv_label_create(self.ctx_.root);
    lv_obj_set_style_text_font(self.ctx_.info_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(self.ctx_.info_label, lv_color_hex(0xFFD54F), 0);
    lv_label_set_text(self.ctx_.info_label, "10 coins / pull");
    lv_obj_align(self.ctx_.info_label, LV_ALIGN_TOP_LEFT, 180, 40);

    self.ctx_.pull_btn = lv_button_create(self.ctx_.root);
    lv_obj_set_size(self.ctx_.pull_btn, 120, 50);
    lv_obj_align(self.ctx_.pull_btn, LV_ALIGN_TOP_LEFT, 180, 70);
    lv_obj_set_style_bg_color(self.ctx_.pull_btn, lv_color_hex(0xD32F2F), 0);
    lv_obj_t *pl = lv_label_create(self.ctx_.pull_btn);
    lv_label_set_text(pl, "PULL\n(10c)");
    lv_obj_center(pl);
    lv_obj_add_event_cb(self.ctx_.pull_btn, on_pull, LV_EVENT_CLICKED, nullptr);

    self.ctx_.album_btn = lv_button_create(self.ctx_.root);
    lv_obj_set_size(self.ctx_.album_btn, 120, 36);
    lv_obj_align(self.ctx_.album_btn, LV_ALIGN_TOP_LEFT, 180, 140);
    lv_obj_set_style_bg_color(self.ctx_.album_btn, lv_color_hex(0x1976D2), 0);
    lv_obj_t *al = lv_label_create(self.ctx_.album_btn);
    lv_label_set_text(al, "Album");
    lv_obj_center(al);
    lv_obj_add_event_cb(self.ctx_.album_btn, on_album, LV_EVENT_CLICKED, nullptr);

    self.ctx_.anim_timer = lv_timer_create(on_anim_timer, 25, &self);

    return self.ctx_.root;
}

void GachaGame::destroy(lv_obj_t *root)
{
    auto &self = instance();
    if (self.ctx_.anim_timer) {
        lv_timer_del(self.ctx_.anim_timer);
        self.ctx_.anim_timer = nullptr;
    }
    self.ctx_ = Ctx{};
    if (root) lv_obj_del(root);
}

} // namespace pet
