#include "pet_game_gacha.h"
#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

namespace pet {
namespace game_gacha {

static const char *TAG __attribute__((unused)) = "gacha";

static constexpr int kRarities = 5;
static constexpr int kPerRarity = 6;  // 5 * 6 = 30 cards total
static constexpr int kAlbumSlots = kRarities * kPerRarity;
static constexpr int kPullCost = 10;

// 5 rarities; cumulative weights for picking.
static const int kWeights[kRarities] = {60, 85, 95, 99, 100};

// Per-card display info. Icon is short ASCII art (LVGL cannot render emoji).
struct CardDef {
    const char *name;
    const char *icon;     // short ASCII art, max 3 chars
    uint32_t    color;    // background hex
    int         coin_reward;
};

static const CardDef kCards[kRarities][kPerRarity] = {
    // Common
    { {"Bread",   "br", 0xFFB300, 5},
      {"Apple",   "ap", 0xF4511E, 5},
      {"Fish",    "fi", 0x42A5F5, 5},
      {"Leaf",    "lf", 0x66BB6A, 5},
      {"Cup",     "cu", 0xAB47BC, 5},
      {"Star",    "st", 0xFFA726, 5} },
    // Uncommon
    { {"Cake",    "ck", 0xEC407A, 15},
      {"Shine",   "sh", 0xFFEE58, 15},
      {"Coin",    "co", 0xFFCA28, 15},
      {"Gem",     "gm", 0x26A69A, 15},
      {"Hat",     "ht", 0x8D6E63, 15},
      {"Bow",     "bw", 0xEF5350, 15} },
    // Rare
    { {"Crown",   "cr", 0xFFB300, 50},
      {"Bell",    "bl", 0xD4E157, 50},
      {"Mask",    "mk", 0x7E57C2, 50},
      {"Lyre",    "lr", 0x5C6BC0, 50},
      {"Wing",    "wg", 0x29B6F6, 50},
      {"Heart",   "hr", 0xEF5350, 50} },
    // Epic
    { {"Phoenix", "ph", 0xFF6F00, 200},
      {"Unicorn", "un", 0xE1BEE7, 200},
      {"Dragon",  "dr", 0xC62828, 200},
      {"Kraken",  "kr", 0x1565C0, 200},
      {"Comet",   "co", 0x00B8D4, 200},
      {"Aurora",  "au", 0x00BFA5, 200} },
    // Legendary
    { {"Galaxies","ga", 0xFFD600, 1000},
      {"Time",    "ti", 0xF57F17, 1000},
      {"Nebula",  "nb", 0x4527A0, 1000},
      {"Eternal", "et", 0x1A237E, 1000},
      {"Origin",  "og", 0x4A148C, 1000},
      {"Void",    "vd", 0x000000, 1000} },
};

static const char *kRarityName[kRarities] = {
    "Common", "Uncommon", "Rare", "Epic", "Legendary",
};

// NVS persistence (album only).
static const char *kNs = "gacha_album";
static uint8_t s_owned[kAlbumSlots] = {0};   // 1 bit per card, packed into bytes
static bool s_album_loaded = false;

static void album_load()
{
    if (s_album_loaded) return;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
    size_t sz = sizeof(s_owned);
    if (nvs_get_blob(h, "owned", s_owned, &sz) != ESP_OK) {
        memset(s_owned, 0, sizeof(s_owned));
    }
    nvs_close(h);
    s_album_loaded = true;
}

static void album_save()
{
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "owned", s_owned, sizeof(s_owned));
    nvs_commit(h);
    nvs_close(h);
}

static int rarity_pick()
{
    int r = esp_random() % 100;
    for (int i = 0; i < kRarities; i++) {
        if (r < kWeights[i]) return i;
    }
    return 0;
}

int pull_one()
{
    album_load();
    int rarity = rarity_pick();
    int idx = esp_random() % kPerRarity;
    int slot = rarity * kPerRarity + idx;
    s_owned[slot] = 1;
    album_save();
    const CardDef &c = kCards[rarity][idx];
    Pet::instance().add_coins(c.coin_reward);
    ESP_LOGI(TAG, "Pulled: %s (%s) -> +%d coins", c.name, kRarityName[rarity], c.coin_reward);
    return rarity;
}

// ---------------- UI state ---------------------------------------------------
struct Ctx {
    lv_obj_t *root = nullptr;
    lv_obj_t *card_label = nullptr;
    lv_obj_t *card_icon = nullptr;
    lv_obj_t *card_bg = nullptr;
    lv_obj_t *info_label = nullptr;
    lv_obj_t *pull_btn = nullptr;
    lv_obj_t *album_btn = nullptr;
    lv_timer_t *anim_timer = nullptr;
    int anim_ticks = 0;
    int anim_last_rarity = 0;
    int anim_last_idx = 0;
    bool anim_showing_result = false;
    bool anim_in_progress = false;
};

static Ctx s_ctx;

static void set_card_display(const CardDef &c, const char *rarity)
{
    lv_obj_set_style_bg_color(s_ctx.card_bg, lv_color_hex(c.color), 0);
    lv_label_set_text(s_ctx.card_icon, c.icon);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s\n[%s]\n+%d coins", c.name, rarity, c.coin_reward);
    lv_label_set_text(s_ctx.card_label, buf);
}

static void anim_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_ctx.anim_in_progress) return;

    s_ctx.anim_ticks++;
    if (s_ctx.anim_ticks < 12) {
        // Fast scroll: 12 frames at ~25ms = 300ms.
        int r = esp_random() % kRarities;
        int i = esp_random() % kPerRarity;
        const CardDef &c = kCards[r][i];
        char rname[16];
        snprintf(rname, sizeof(rname), "%s", kRarityName[r]);
        set_card_display(c, rname);
    } else {
        // Final result.
        int rarity = s_ctx.anim_last_rarity;
        int idx    = s_ctx.anim_last_idx;
        const CardDef &c = kCards[rarity][idx];
        set_card_display(c, kRarityName[rarity]);
        s_ctx.anim_in_progress = false;
        s_ctx.anim_showing_result = true;
        lv_obj_clear_state(s_ctx.pull_btn, LV_STATE_DISABLED);
    }
}

static void pull_cb(lv_event_t *e)
{
    (void)e;
    if (s_ctx.anim_in_progress) return;
    if (!Pet::instance().spend_coins(kPullCost)) {
        lv_label_set_text(s_ctx.info_label, "Need 10 coins");
        return;
    }
    int rarity = rarity_pick();
    int idx = esp_random() % kPerRarity;
    int slot = rarity * kPerRarity + idx;
    s_owned[slot] = 1;
    album_save();
    const CardDef &c = kCards[rarity][idx];
    Pet::instance().add_coins(c.coin_reward);

    s_ctx.anim_last_rarity = rarity;
    s_ctx.anim_last_idx = idx;
    s_ctx.anim_ticks = 0;
    s_ctx.anim_in_progress = true;
    lv_obj_add_state(s_ctx.pull_btn, LV_STATE_DISABLED);

    char buf[48];
    snprintf(buf, sizeof(buf), "Pulling...");
    lv_label_set_text(s_ctx.info_label, buf);
}

static void album_cb(lv_event_t *e)
{
    (void)e;
    album_load();
    int owned_count = 0;
    for (int i = 0; i < kAlbumSlots; i++) if (s_owned[i]) owned_count++;
    char buf[64];
    snprintf(buf, sizeof(buf), "Album: %d / %d collected", owned_count, kAlbumSlots);
    lv_label_set_text(s_ctx.info_label, buf);
}

lv_obj_t *build(lv_obj_t *parent)
{
    s_ctx = Ctx{};
    album_load();

    s_ctx.root = lv_obj_create(parent);
    lv_obj_set_size(s_ctx.root, 320, 208);
    lv_obj_set_style_bg_color(s_ctx.root, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_ctx.root, 0, 0);
    lv_obj_set_style_pad_all(s_ctx.root, 0, 0);

    // Title + coins line
    lv_obj_t *title = lv_label_create(s_ctx.root);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Gacha");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    // Card display panel (large card with icon + name + reward)
    s_ctx.card_bg = lv_obj_create(s_ctx.root);
    lv_obj_set_size(s_ctx.card_bg, 140, 110);
    lv_obj_align(s_ctx.card_bg, LV_ALIGN_TOP_LEFT, 20, 30);
    lv_obj_set_style_bg_color(s_ctx.card_bg, lv_color_hex(0x37474F), 0);
    lv_obj_set_style_border_width(s_ctx.card_bg, 0, 0);
    lv_obj_set_style_radius(s_ctx.card_bg, 12, 0);

    s_ctx.card_icon = lv_label_create(s_ctx.card_bg);
    lv_obj_set_style_text_font(s_ctx.card_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ctx.card_icon, lv_color_white(), 0);
    lv_label_set_text(s_ctx.card_icon, "?");
    lv_obj_align(s_ctx.card_icon, LV_ALIGN_TOP_MID, 0, 6);

    s_ctx.card_label = lv_label_create(s_ctx.card_bg);
    lv_obj_set_style_text_font(s_ctx.card_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ctx.card_label, lv_color_white(), 0);
    lv_label_set_text(s_ctx.card_label, "Tap PULL");
    lv_obj_align(s_ctx.card_label, LV_ALIGN_BOTTOM_MID, 0, -6);

    // Right column: info + buttons
    s_ctx.info_label = lv_label_create(s_ctx.root);
    lv_obj_set_style_text_font(s_ctx.info_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ctx.info_label, lv_color_hex(0xFFD54F), 0);
    lv_label_set_text(s_ctx.info_label, "10 coins / pull");
    lv_obj_align(s_ctx.info_label, LV_ALIGN_TOP_LEFT, 180, 40);

    s_ctx.pull_btn = lv_btn_create(s_ctx.root);
    lv_obj_set_size(s_ctx.pull_btn, 120, 50);
    lv_obj_align(s_ctx.pull_btn, LV_ALIGN_TOP_LEFT, 180, 70);
    lv_obj_set_style_bg_color(s_ctx.pull_btn, lv_color_hex(0xD32F2F), 0);
    lv_obj_t *pl = lv_label_create(s_ctx.pull_btn);
    lv_label_set_text(pl, "PULL\n(10c)");
    lv_obj_center(pl);
    lv_obj_add_event_cb(s_ctx.pull_btn, pull_cb, LV_EVENT_CLICKED, nullptr);

    s_ctx.album_btn = lv_btn_create(s_ctx.root);
    lv_obj_set_size(s_ctx.album_btn, 120, 36);
    lv_obj_align(s_ctx.album_btn, LV_ALIGN_TOP_LEFT, 180, 140);
    lv_obj_set_style_bg_color(s_ctx.album_btn, lv_color_hex(0x1976D2), 0);
    lv_obj_t *al = lv_label_create(s_ctx.album_btn);
    lv_label_set_text(al, "Album");
    lv_obj_center(al);
    lv_obj_add_event_cb(s_ctx.album_btn, album_cb, LV_EVENT_CLICKED, nullptr);

    // Anim timer: 25ms cadence
    s_ctx.anim_timer = lv_timer_create(anim_tick_cb, 25, nullptr);

    return s_ctx.root;
}

void destroy(lv_obj_t *root)
{
    if (s_ctx.anim_timer) {
        lv_timer_del(s_ctx.anim_timer);
        s_ctx.anim_timer = nullptr;
    }
    s_ctx = Ctx{};
    if (root) lv_obj_del(root);
}

}  // namespace game_gacha
}  // namespace pet