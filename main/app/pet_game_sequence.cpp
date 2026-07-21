#include "pet_game_sequence.h"
#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"

namespace pet {

static const char *TAG __attribute__((unused)) = "sequence";

SequenceGame &SequenceGame::instance() noexcept
{
    static SequenceGame s;
    return s;
}

void SequenceGame::shuffle(int *arr, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = esp_random() % (i + 1);
        int t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
}

void SequenceGame::restart_round()
{
    for (int i = 0; i < kTiles; i++) ctx_.order[i] = i + 1;
    shuffle(ctx_.order, kTiles);
    ctx_.next_expected = 1;
    for (int i = 0; i < kTiles; i++) {
        if (ctx_.tiles[i]) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", ctx_.order[i]);
            lv_obj_set_style_bg_color(ctx_.tiles[i], lv_color_hex(0x37474F), 0);
            lv_label_set_text(lv_obj_get_child(ctx_.tiles[i], 0), buf);
        }
    }
    if (ctx_.info_label) lv_label_set_text(ctx_.info_label, "Tap 1, 2, 3...");
}

void SequenceGame::on_tile_click(lv_event_t *e)
{
    auto &self = instance();
    int tile_idx = (int)(intptr_t)lv_event_get_user_data(e);
    int value = self.ctx_.order[tile_idx];
    if (value != self.ctx_.next_expected) {
        lv_obj_set_style_bg_color(self.ctx_.tiles[tile_idx], lv_color_hex(0xE53935), 0);
        return;
    }
    lv_obj_set_style_bg_color(self.ctx_.tiles[tile_idx], lv_color_hex(0x43A047), 0);
    self.ctx_.next_expected++;
    if (self.ctx_.next_expected > kTiles) {
        Pet::instance().work_outcome("e",  -15, 0);
        Pet::instance().work_outcome("ha", +10, 0);
        Pet::instance().add_coins(kReward);
        char buf[32];
        snprintf(buf, sizeof(buf), "Cleared! +%d", kReward);
        lv_label_set_text(self.ctx_.info_label, buf);
        self.restart_round();
    } else {
        char buf[24];
        snprintf(buf, sizeof(buf), "Next: %d", self.ctx_.next_expected);
        lv_label_set_text(self.ctx_.info_label, buf);
    }
}

lv_obj_t *SequenceGame::build(lv_obj_t *parent)
{
    auto &self = instance();
    self.ctx_ = Ctx{};

    int lvl = Pet::instance().get_state().level;
    if (lvl < 2) {
        self.ctx_.root = lv_obj_create(parent);
        lv_obj_set_size(self.ctx_.root, 320, 208);
        lv_obj_set_style_bg_color(self.ctx_.root, lv_color_black(), 0);
        lv_obj_set_style_border_width(self.ctx_.root, 0, 0);

        lv_obj_t *t = lv_label_create(self.ctx_.root);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0x888888), 0);
        lv_label_set_text(t, "Locked");
        lv_obj_align(t, LV_ALIGN_CENTER, 0, -10);

        lv_obj_t *h = lv_label_create(self.ctx_.root);
        lv_obj_set_style_text_font(h, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0xBBBBBB), 0);
        lv_label_set_text(h, "Reach Lv 2 to unlock\nSequence Tap");
        lv_obj_align(h, LV_ALIGN_CENTER, 0, 20);
        return self.ctx_.root;
    }

    self.ctx_.root = lv_obj_create(parent);
    lv_obj_set_size(self.ctx_.root, 320, 208);
    lv_obj_set_style_bg_color(self.ctx_.root, lv_color_black(), 0);
    lv_obj_set_style_border_width(self.ctx_.root, 0, 0);
    lv_obj_set_style_pad_all(self.ctx_.root, 0, 0);

    self.ctx_.info_label = lv_label_create(self.ctx_.root);
    lv_obj_set_style_text_font(self.ctx_.info_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(self.ctx_.info_label, lv_color_white(), 0);
    lv_obj_align(self.ctx_.info_label, LV_ALIGN_TOP_MID, 0, 6);

    for (int i = 0; i < kTiles; i++) {
        int col = i % 3;
        int row = i / 3;
        lv_obj_t *tile = lv_button_create(self.ctx_.root);
        lv_obj_set_size(tile, 90, 50);
        lv_obj_set_pos(tile, 20 + col * 95, 40 + row * 55);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x37474F), 0);

        lv_obj_t *label = lv_label_create(tile);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_center(label);

        lv_obj_add_event_cb(tile, on_tile_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        self.ctx_.tiles[i] = tile;
    }

    self.restart_round();
    return self.ctx_.root;
}

void SequenceGame::destroy(lv_obj_t *root)
{
    instance().ctx_ = Ctx{};
    if (root) lv_obj_del(root);
}

} // namespace pet
