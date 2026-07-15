#include "pet_game_sequence.h"
#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"
#include <algorithm>

namespace pet {
namespace game_sequence {

static const char *TAG __attribute__((unused)) = "sequence";

static constexpr int kTiles = 9;
static constexpr int kReward = 30;

struct Ctx {
    lv_obj_t *root = nullptr;
    lv_obj_t *tiles[kTiles] = {nullptr};
    lv_obj_t *info_label = nullptr;
    int order[kTiles] = {0};
    int next_expected = 1;
};

static Ctx s_ctx;

static void shuffle(int *arr, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = esp_random() % (i + 1);
        int t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
}

static void restart_round()
{
    for (int i = 0; i < kTiles; i++) s_ctx.order[i] = i + 1;
    shuffle(s_ctx.order, kTiles);
    s_ctx.next_expected = 1;
    for (int i = 0; i < kTiles; i++) {
        if (s_ctx.tiles[i]) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", s_ctx.order[i]);
            lv_obj_set_style_bg_color(s_ctx.tiles[i], lv_color_hex(0x37474F), 0);
            lv_label_set_text(lv_obj_get_child(s_ctx.tiles[i], 0), buf);
        }
    }
    if (s_ctx.info_label) lv_label_set_text(s_ctx.info_label, "Tap 1, 2, 3...");
}

static void tile_cb(lv_event_t *e)
{
    int tile_idx = (int)(intptr_t)lv_event_get_user_data(e);
    int value = s_ctx.order[tile_idx];
    if (value != s_ctx.next_expected) {
        // Wrong order, flash red briefly.
        lv_obj_set_style_bg_color(s_ctx.tiles[tile_idx], lv_color_hex(0xE53935), 0);
        return;
    }
    // Correct!
    lv_obj_set_style_bg_color(s_ctx.tiles[tile_idx], lv_color_hex(0x43A047), 0);  // green
    s_ctx.next_expected++;
    if (s_ctx.next_expected > kTiles) {
        // Win! v0.6: stat cost. Sequence Tap is mental — costs energy,
        // gives happiness back.
        Pet::instance().work_outcome("e",  -15, 0);
        Pet::instance().work_outcome("ha", +10, 0);
        Pet::instance().add_coins(kReward);
        char buf[32];
        snprintf(buf, sizeof(buf), "Cleared! +%d", kReward);
        lv_label_set_text(s_ctx.info_label, buf);
        // Auto-restart after a short delay (caller can ignore next tap).
        restart_round();
    } else {
        char buf[24];
        snprintf(buf, sizeof(buf), "Next: %d", s_ctx.next_expected);
        lv_label_set_text(s_ctx.info_label, buf);
    }
}

lv_obj_t *build(lv_obj_t *parent)
{
    s_ctx = Ctx{};

    // Show "locked" overlay if level < 2.
    int lvl = Pet::instance().get_state().level;
    if (lvl < 2) {
        s_ctx.root = lv_obj_create(parent);
        lv_obj_set_size(s_ctx.root, 320, 208);
        lv_obj_set_style_bg_color(s_ctx.root, lv_color_black(), 0);
        lv_obj_set_style_border_width(s_ctx.root, 0, 0);

        lv_obj_t *t = lv_label_create(s_ctx.root);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0x888888), 0);
        lv_label_set_text(t, "Locked");
        lv_obj_align(t, LV_ALIGN_CENTER, 0, -10);

        lv_obj_t *h = lv_label_create(s_ctx.root);
        lv_obj_set_style_text_font(h, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0xBBBBBB), 0);
        lv_label_set_text(h, "Reach Lv 2 to unlock\nSequence Tap");
        lv_obj_align(h, LV_ALIGN_CENTER, 0, 20);
        return s_ctx.root;
    }

    s_ctx.root = lv_obj_create(parent);
    lv_obj_set_size(s_ctx.root, 320, 208);
    lv_obj_set_style_bg_color(s_ctx.root, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_ctx.root, 0, 0);
    lv_obj_set_style_pad_all(s_ctx.root, 0, 0);

    s_ctx.info_label = lv_label_create(s_ctx.root);
    lv_obj_set_style_text_font(s_ctx.info_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ctx.info_label, lv_color_white(), 0);
    lv_obj_align(s_ctx.info_label, LV_ALIGN_TOP_MID, 0, 6);

    // 3x3 grid of 90x90 tiles starting at y=40.
    for (int i = 0; i < kTiles; i++) {
        int col = i % 3;
        int row = i / 3;
        lv_obj_t *tile = lv_button_create(s_ctx.root);
        lv_obj_set_size(tile, 90, 50);
        lv_obj_set_pos(tile, 20 + col * 95, 40 + row * 55);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x37474F), 0);

        lv_obj_t *label = lv_label_create(tile);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_center(label);

        lv_obj_add_event_cb(tile, tile_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        s_ctx.tiles[i] = tile;
    }

    restart_round();
    return s_ctx.root;
}

void destroy(lv_obj_t *root)
{
    s_ctx = Ctx{};
    if (root) lv_obj_del(root);
}

}  // namespace game_sequence
}  // namespace pet