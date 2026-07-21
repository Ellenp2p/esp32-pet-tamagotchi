#include "pet_game_whack.h"
#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"

namespace pet {

static const char *TAG __attribute__((unused)) = "whack";

WhackGame &WhackGame::instance() noexcept
{
    static WhackGame s;
    return s;
}

void WhackGame::set_info(const char *text)
{
    if (ctx_.info_label) lv_label_set_text(ctx_.info_label, text);
}

void WhackGame::set_hint(const char *text)
{
    if (ctx_.hint_label) lv_label_set_text(ctx_.hint_label, text);
}

void WhackGame::reset_holes()
{
    for (int i = 0; i < kHoles; i++) {
        if (ctx_.holes[i]) {
            lv_obj_set_style_bg_color(ctx_.holes[i], lv_color_hex(0x37474F), 0);
            lv_obj_set_style_shadow_width(ctx_.holes[i], 0, 0);
        }
    }
}

void WhackGame::next_round()
{
    switch (ctx_.state) {
    case State::Show: {
        reset_holes();
        ctx_.state = State::Gap;
        char buf[40];
        snprintf(buf, sizeof(buf), "Miss! -%d", kPenalty);
        set_hint(buf);
        Pet::instance().add_coins(-kPenalty);
        ctx_.score = (ctx_.score > 0) ? ctx_.score - 1 : 0;
        lv_timer_set_period(ctx_.timer, kGapMs);
        break;
    }
    case State::Gap: {
        ctx_.round_idx++;
        if (ctx_.round_idx >= kRounds) {
            ctx_.state = State::GameOver;
            char buf[64];
            snprintf(buf, sizeof(buf), "Game over! Score:%d/%d", ctx_.score, kRounds);
            set_info(buf);
            set_hint("Press Start");
            Pet::instance().work_outcome("ha", 5, 0);
            Pet::instance().work_outcome("",   0, ctx_.score * 5);
            if (ctx_.start_btn) lv_obj_clear_state(ctx_.start_btn, LV_STATE_DISABLED);
            lv_timer_pause(ctx_.timer);
            return;
        }
        ctx_.current_hole = esp_random() % kHoles;
        if (ctx_.holes[ctx_.current_hole]) {
            lv_obj_set_style_bg_color(ctx_.holes[ctx_.current_hole],
                                      lv_color_hex(0xF4511E), 0);
            lv_obj_set_style_shadow_width(ctx_.holes[ctx_.current_hole], 12, 0);
            lv_obj_set_style_shadow_color(ctx_.holes[ctx_.current_hole],
                                          lv_color_hex(0xFF6E40), 0);
        }
        ctx_.state = State::Show;
        set_hint("Tap!");
        lv_timer_set_period(ctx_.timer, kShowMs);
        break;
    }
    default:
        break;
    }
}

void WhackGame::on_timer(lv_timer_t *t)
{
    static_cast<WhackGame *>(lv_timer_get_user_data(t))->next_round();
}

void WhackGame::on_hole_click(lv_event_t *e)
{
    auto &self = instance();
    if (self.ctx_.state != State::Show) return;
    int hit = (int)(intptr_t)lv_event_get_user_data(e);
    if (hit != self.ctx_.current_hole) {
        self.set_hint("Wrong hole!");
        Pet::instance().add_coins(-kPenalty);
        return;
    }
    self.reset_holes();
    self.ctx_.state = State::Gap;
    self.ctx_.score++;
    char buf[40];
    snprintf(buf, sizeof(buf), "Hit! +%d", kReward);
    self.set_hint(buf);
    Pet::instance().work_outcome("e", -2, 0);
    Pet::instance().add_coins(kReward);
    lv_timer_set_period(self.ctx_.timer, kGapMs);
}

void WhackGame::on_start(lv_event_t *e)
{
    (void)e;
    auto &self = instance();
    self.ctx_.score = 0;
    self.ctx_.round_idx = 0;
    self.ctx_.state = State::Gap;
    if (self.ctx_.start_btn) lv_obj_add_state(self.ctx_.start_btn, LV_STATE_DISABLED);
    char buf[40];
    snprintf(buf, sizeof(buf), "Score: 0 / %d", kRounds);
    self.set_info(buf);
    self.set_hint("Get ready...");
    lv_timer_resume(self.ctx_.timer);
    lv_timer_set_period(self.ctx_.timer, 100);
    self.ctx_.current_hole = -1;
}

lv_obj_t *WhackGame::build(lv_obj_t *parent)
{
    auto &self = instance();
    self.ctx_ = Ctx{};

    self.ctx_.root = lv_obj_create(parent);
    lv_obj_set_size(self.ctx_.root, 320, 208);
    lv_obj_set_style_bg_color(self.ctx_.root, lv_color_black(), 0);
    lv_obj_set_style_border_width(self.ctx_.root, 0, 0);
    lv_obj_set_style_pad_all(self.ctx_.root, 0, 0);

    self.ctx_.info_label = lv_label_create(self.ctx_.root);
    lv_obj_set_style_text_font(self.ctx_.info_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(self.ctx_.info_label, lv_color_white(), 0);
    lv_label_set_text(self.ctx_.info_label, "Whack-a-Mole");
    lv_obj_align(self.ctx_.info_label, LV_ALIGN_TOP_MID, 0, 6);

    self.ctx_.hint_label = lv_label_create(self.ctx_.root);
    lv_obj_set_style_text_font(self.ctx_.hint_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(self.ctx_.hint_label, lv_color_hex(0xFFD54F), 0);
    lv_label_set_text(self.ctx_.hint_label, "Press Start");
    lv_obj_align(self.ctx_.hint_label, LV_ALIGN_TOP_MID, 0, 26);

    for (int i = 0; i < kHoles; i++) {
        lv_obj_t *hole = lv_button_create(self.ctx_.root);
        lv_obj_set_size(hole, 90, 90);
        lv_obj_set_pos(hole, 10 + i * 100, 60);
        lv_obj_set_style_bg_color(hole, lv_color_hex(0x37474F), 0);
        lv_obj_set_style_radius(hole, 45, 0);
        lv_obj_add_event_cb(hole, on_hole_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        self.ctx_.holes[i] = hole;
    }

    self.ctx_.start_btn = lv_button_create(self.ctx_.root);
    lv_obj_set_size(self.ctx_.start_btn, 100, 32);
    lv_obj_align(self.ctx_.start_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t *start_label = lv_label_create(self.ctx_.start_btn);
    lv_label_set_text(start_label, "Start");
    lv_obj_center(start_label);
    lv_obj_add_event_cb(self.ctx_.start_btn, on_start, LV_EVENT_CLICKED, nullptr);

    self.ctx_.timer = lv_timer_create(on_timer, 50, &self);
    lv_timer_pause(self.ctx_.timer);

    return self.ctx_.root;
}

void WhackGame::destroy(lv_obj_t *root)
{
    auto &self = instance();
    if (self.ctx_.timer) {
        lv_timer_del(self.ctx_.timer);
        self.ctx_.timer = nullptr;
    }
    self.ctx_ = Ctx{};
    if (root) lv_obj_del(root);
}

} // namespace pet
