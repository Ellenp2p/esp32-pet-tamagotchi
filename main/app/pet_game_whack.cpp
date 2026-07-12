#include "pet_game_whack.h"
#include "pet_state.h"
#include "esp_log.h"
#include "esp_random.h"

namespace pet {
namespace game_whack {

static const char *TAG __attribute__((unused)) = "whack";

static constexpr int kHoles = 3;
static constexpr int kRounds = 5;
static constexpr int kShowMs = 1500;     // mole stays up this long
static constexpr int kGapMs = 600;       // gap between moles
static constexpr int kReward = 5;
static constexpr int kPenalty = 1;

enum class State { Idle, Show, Gap, GameOver };

struct Ctx {
    lv_obj_t *root = nullptr;
    lv_obj_t *holes[kHoles] = {nullptr};
    lv_obj_t *info_label = nullptr;      // "Score: 0 / 5" line
    lv_obj_t *hint_label = nullptr;      // "Tap!" hint
    lv_obj_t *start_btn = nullptr;
    lv_timer_t *timer = nullptr;
    State state = State::Idle;
    int current_hole = -1;
    int round_idx = 0;
    int score = 0;
};

static Ctx s_ctx;

static void set_info(const char *text)
{
    if (s_ctx.info_label) lv_label_set_text(s_ctx.info_label, text);
}

static void set_hint(const char *text)
{
    if (s_ctx.hint_label) lv_label_set_text(s_ctx.hint_label, text);
}

static void reset_holes()
{
    for (int i = 0; i < kHoles; i++) {
        if (s_ctx.holes[i]) {
            lv_obj_set_style_bg_color(s_ctx.holes[i], lv_color_hex(0x37474F), 0);
            lv_obj_set_style_shadow_width(s_ctx.holes[i], 0, 0);
        }
    }
}

static void next_round(lv_timer_t *t)
{
    (void)t;
    switch (s_ctx.state) {
    case State::Show: {
        // Time's up, player missed this mole.
        reset_holes();
        s_ctx.state = State::Gap;
        char buf[40];
        snprintf(buf, sizeof(buf), "Miss! -%d", kPenalty);
        set_hint(buf);
        Pet::instance().add_coins(-kPenalty);
        s_ctx.score = (s_ctx.score > 0) ? s_ctx.score - 1 : 0;
        lv_timer_set_period(s_ctx.timer, kGapMs);
        break;
    }
    case State::Gap: {
        s_ctx.round_idx++;
        if (s_ctx.round_idx >= kRounds) {
            s_ctx.state = State::GameOver;
            char buf[64];
            snprintf(buf, sizeof(buf), "Game over! Score:%d/%d", s_ctx.score, kRounds);
            set_info(buf);
            set_hint("Press Start");
            if (s_ctx.start_btn) lv_obj_clear_state(s_ctx.start_btn, LV_STATE_DISABLED);
            lv_timer_pause(s_ctx.timer);
            return;
        }
        s_ctx.current_hole = esp_random() % kHoles;
        if (s_ctx.holes[s_ctx.current_hole]) {
            lv_obj_set_style_bg_color(s_ctx.holes[s_ctx.current_hole],
                                      lv_color_hex(0xF4511E), 0);  // orange mole
            lv_obj_set_style_shadow_width(s_ctx.holes[s_ctx.current_hole], 12, 0);
            lv_obj_set_style_shadow_color(s_ctx.holes[s_ctx.current_hole],
                                          lv_color_hex(0xFF6E40), 0);
        }
        s_ctx.state = State::Show;
        set_hint("Tap!");
        lv_timer_set_period(s_ctx.timer, kShowMs);
        break;
    }
    default:
        break;
    }
}

static void hole_click_cb(lv_event_t *e)
{
    if (s_ctx.state != State::Show) return;
    int hit = (int)(intptr_t)lv_event_get_user_data(e);
    if (hit != s_ctx.current_hole) {
        // Wrong hole. Minor penalty.
        set_hint("Wrong hole!");
        Pet::instance().add_coins(-kPenalty);
        return;
    }
    // Hit!
    reset_holes();
    s_ctx.state = State::Gap;
    s_ctx.score++;
    char buf[40];
    snprintf(buf, sizeof(buf), "Hit! +%d", kReward);
    set_hint(buf);
    Pet::instance().add_coins(kReward);
    lv_timer_set_period(s_ctx.timer, kGapMs);
}

static void start_cb(lv_event_t *e)
{
    (void)e;
    s_ctx.score = 0;
    s_ctx.round_idx = 0;
    s_ctx.state = State::Gap;  // first tick will promote to Show
    if (s_ctx.start_btn) lv_obj_add_state(s_ctx.start_btn, LV_STATE_DISABLED);
    char buf[40];
    snprintf(buf, sizeof(buf), "Score: 0 / %d", kRounds);
    set_info(buf);
    set_hint("Get ready...");
    lv_timer_resume(s_ctx.timer);
    lv_timer_set_period(s_ctx.timer, 100);  // brief delay then first mole
    // Pre-populate current_hole so the Gap->Show transition works.
    s_ctx.current_hole = -1;
}

lv_obj_t *build(lv_obj_t *parent)
{
    s_ctx = Ctx{};

    s_ctx.root = lv_obj_create(parent);
    lv_obj_set_size(s_ctx.root, 320, 208);
    lv_obj_set_style_bg_color(s_ctx.root, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_ctx.root, 0, 0);
    lv_obj_set_style_pad_all(s_ctx.root, 0, 0);

    s_ctx.info_label = lv_label_create(s_ctx.root);
    lv_obj_set_style_text_font(s_ctx.info_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ctx.info_label, lv_color_white(), 0);
    lv_label_set_text(s_ctx.info_label, "Whack-a-Mole");
    lv_obj_align(s_ctx.info_label, LV_ALIGN_TOP_MID, 0, 6);

    s_ctx.hint_label = lv_label_create(s_ctx.root);
    lv_obj_set_style_text_font(s_ctx.hint_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ctx.hint_label, lv_color_hex(0xFFD54F), 0);
    lv_label_set_text(s_ctx.hint_label, "Press Start");
    lv_obj_align(s_ctx.hint_label, LV_ALIGN_TOP_MID, 0, 26);

    // 3 holes, 90x90 each, evenly spaced.
    for (int i = 0; i < kHoles; i++) {
        lv_obj_t *hole = lv_btn_create(s_ctx.root);
        lv_obj_set_size(hole, 90, 90);
        lv_obj_set_pos(hole, 10 + i * 100, 60);
        lv_obj_set_style_bg_color(hole, lv_color_hex(0x37474F), 0);
        lv_obj_set_style_radius(hole, 45, 0);
        lv_obj_add_event_cb(hole, hole_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        s_ctx.holes[i] = hole;
    }

    s_ctx.start_btn = lv_btn_create(s_ctx.root);
    lv_obj_set_size(s_ctx.start_btn, 100, 32);
    lv_obj_align(s_ctx.start_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t *start_label = lv_label_create(s_ctx.start_btn);
    lv_label_set_text(start_label, "Start");
    lv_obj_center(start_label);
    lv_obj_add_event_cb(s_ctx.start_btn, start_cb, LV_EVENT_CLICKED, nullptr);

    // 50ms idle period; ticks only when state != Idle.
    s_ctx.timer = lv_timer_create(next_round, 50, nullptr);
    lv_timer_pause(s_ctx.timer);

    return s_ctx.root;
}

void destroy(lv_obj_t *root)
{
    if (s_ctx.timer) {
        lv_timer_del(s_ctx.timer);
        s_ctx.timer = nullptr;
    }
    s_ctx = Ctx{};
    if (root) lv_obj_del(root);
}

}  // namespace game_whack
}  // namespace pet