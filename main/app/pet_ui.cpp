#include "pet_ui.h"
#include "pet_state.h"
#include "pet_save.h"
#include "pet_pages.h"
#include "pet_game_whack.h"
#include "pet_game_sequence.h"
#include "pet_game_gacha.h"
#include "pet_frames.h"
#include "pet_idle_events.h"
#include "app/ble_pet.h"
#include "bsp/bsp_qmi8658.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pet {

static const char *TAG = "pet_ui";

// ---------------- Status page widgets (live across the page's lifetime) -----
// Hand-rolled lv_image + lv_anim for full control over frame timing per state.
static lv_obj_t *face_img_ = nullptr;
static lv_anim_t face_anim_buf_;       // backing storage for the anim
static lv_obj_t *status_label_ = nullptr;   // Line 1: "Awake | Lv3 | Sleeping"
static lv_obj_t *stats_label_ = nullptr;   // Line 2: "F:88 Ha:75 E:62 He:90"
static lv_obj_t *coins_label_ = nullptr;    // Line 3: "Coins: 42"
static lv_obj_t *bars_[4] = {nullptr};
static lv_obj_t *btn_sleep_ = nullptr;

// Animation state machine.
static pet_anim_state_t current_anim_ = PET_ANIM_COUNT;  // sentinel: forces init
static bool action_animation_active_ = false;

static const char *BAR_NAMES[4] = {"Fullness", "Happy", "Energy", "Health"};

static const uint32_t kFrameMs[PET_ANIM_COUNT] = {
    200,  // IDLE
    180,  // HAPPY
    160,  // EATING
    400,  // SLEEPING
    180,  // PLAYING
    240,  // SICK
};
static const uint16_t kFrameCount = 9;

static void set_bar_value_with_warn(lv_obj_t *bar, int value)
{
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_color_t color;
    if (value < 10)      color = lv_color_hex(0xE53935);
    else if (value < 25) color = lv_color_hex(0xFB8C00);
    else                 color = lv_color_hex(0x43A047);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
}

// ---------------- Animation control ----------------------------------------
// Decide which animation best reflects the pet's current state. Sleeping
// always wins; if any vital is critically low, we show Sick; otherwise the
// mood tracks happiness + energy.
static pet_anim_state_t resolve_anim_state(const State &s, bool sleeping);

// Per-tick callback: advance the frame index and refresh lv_img src.
// `var` is the lv_img; `value` is the integer "tick" counter (we keep the
// current anim in current_anim_ to know which frame table to index).
static void anim_frame_exec(void *var, int32_t value);

// Animation runner (declared early so anim_ready_cb can call it).
static void switch_to_animation(pet_anim_state_t anim);

static pet_anim_state_t resolve_anim_state(const State &s, bool sleeping)
{
    if (sleeping) return PET_ANIM_SLEEPING;
    if (s.health < 30 || s.fullness < 20 || s.happiness < 20) return PET_ANIM_SICK;
    if (s.happiness > 80 && s.energy > 50) return PET_ANIM_HAPPY;
    return PET_ANIM_IDLE;
}

static void anim_frame_exec(void *var, int32_t value)
{
    lv_obj_t *img = (lv_obj_t *)var;
    const pet_anim_state_t anim = current_anim_;
    if (anim >= PET_ANIM_COUNT) return;
    const int32_t idx = value % (int32_t)kFrameCount;
    if (idx < 0) return;
    lv_image_set_src(img, pet_anim_frames[anim][idx]);
}

static void anim_ready_cb(lv_anim_t *a)
{
    if (action_animation_active_) {
        action_animation_active_ = false;
        const State s = Pet::instance().get_state();
        const pet_anim_state_t next =
            resolve_anim_state(s, Pet::instance().is_sleeping());
        if (next != current_anim_) switch_to_animation(next);
    } else {
        lv_anim_set_repeat_count(a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(a);
    }
}

static void switch_to_animation(pet_anim_state_t anim)
{
    if (!face_img_) return;
    if (anim >= PET_ANIM_COUNT) return;
    current_anim_ = anim;

    lv_anim_init(&face_anim_buf_);
    lv_anim_set_var(&face_anim_buf_, face_img_);
    lv_anim_set_exec_cb(&face_anim_buf_, anim_frame_exec);
    lv_anim_set_values(&face_anim_buf_, 0, (int32_t)kFrameCount);
    lv_anim_set_duration(&face_anim_buf_, kFrameCount * kFrameMs[anim]);
    lv_anim_set_repeat_count(&face_anim_buf_, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_ready_cb(&face_anim_buf_, anim_ready_cb);
    lv_anim_start(&face_anim_buf_);

    // Show frame 0 immediately so the screen is never blank before first tick.
    lv_image_set_src(face_img_, pet_anim_frames[anim][0]);
    lv_obj_invalidate(face_img_);
}

// One-shot action animation: plays N loops then falls back to stat-based.
static lv_anim_t action_anim_buf_;
static void start_action_anim(pet_anim_state_t anim, uint16_t repeat_count)
{
    if (!face_img_) return;
    if (anim >= PET_ANIM_COUNT) return;
    action_animation_active_ = true;
    current_anim_ = anim;

    lv_anim_init(&action_anim_buf_);
    lv_anim_set_var(&action_anim_buf_, face_img_);
    lv_anim_set_exec_cb(&action_anim_buf_, anim_frame_exec);
    lv_anim_set_values(&action_anim_buf_, 0, (int32_t)kFrameCount);
    lv_anim_set_duration(&action_anim_buf_, kFrameCount * kFrameMs[anim]);
    lv_anim_set_repeat_count(&action_anim_buf_, repeat_count);
    lv_anim_set_ready_cb(&action_anim_buf_, anim_ready_cb);
    lv_anim_start(&action_anim_buf_);

    lv_image_set_src(face_img_, pet_anim_frames[anim][0]);
    lv_obj_invalidate(face_img_);
}

// ---------------- Update loop ------------------------------------------------
static void update_ui()
{
    if (!face_img_) return;

    State s = Pet::instance().get_state();
    bool sleeping = Pet::instance().is_sleeping();

    set_bar_value_with_warn(bars_[0], s.fullness);
    set_bar_value_with_warn(bars_[1], s.happiness);
    set_bar_value_with_warn(bars_[2], s.energy);
    set_bar_value_with_warn(bars_[3], s.health);

    if (!action_animation_active_) {
        const pet_anim_state_t target = resolve_anim_state(s, sleeping);
        if (target != current_anim_) switch_to_animation(target);
    }

    // Three short labels stacked vertically instead of one wide row, so we
    // never overflow the 212-px right column. Coins is shown on its own line
    // with a "9999+" cap so the layout doesn't depend on the wallet size.
    char line1[32], line2[64], line3[32];
    snprintf(line1, sizeof(line1), "%s | Lv%d",
             sleeping ? "Sleeping" : "Awake", s.level);
    snprintf(line2, sizeof(line2), "F:%d Ha:%d E:%d He:%d",
             s.fullness, s.happiness, s.energy, s.health);
    int coins_disp = s.coins > 9999 ? 9999 : s.coins;
    snprintf(line3, sizeof(line3), "Coins: %d%s",
             coins_disp, s.coins > 9999 ? "+" : "");
    lv_label_set_text(status_label_, line1);
    lv_label_set_text(stats_label_, line2);
    lv_label_set_text(coins_label_, line3);

    lv_label_set_text(lv_obj_get_child(btn_sleep_, 0), sleeping ? "Wake" : "Sleep");
}

static void on_update_timer(lv_timer_t *timer)
{
    update_ui();
}

// ---------------- Action button callbacks -----------------------------------
static void btn_feed_cb(lv_event_t *e)
{
    Pet::instance().feed();
    start_action_anim(PET_ANIM_EATING, 2);  // 2 × 9 × 160ms ≈ 2.9s
}
static void btn_play_cb(lv_event_t *e)
{
    Pet::instance().play();
    start_action_anim(PET_ANIM_PLAYING, 3);  // 3 × 9 × 180ms ≈ 4.9s
}
static void btn_sleep_cb(lv_event_t *e)
{
    if (Pet::instance().is_sleeping()) Pet::instance().wake_up();
    else                              Pet::instance().sleep();
}
static void btn_pet_cb(lv_event_t *e)   { Pet::instance().pet(); }

// ---------------- Page builders ---------------------------------------------
// Status page: face + status line + 4 bars + 4 action buttons. All inside a
// 320x208 content container at the top of the screen.
static lv_obj_t *build_page_status(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 208);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    // Pet sprite — 96x96 native, no scaling.
    face_img_ = lv_image_create(root);
    lv_obj_set_size(face_img_, 96, 96);
    lv_obj_align(face_img_, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_image_set_src(face_img_, pet_anim_frames[PET_ANIM_IDLE][0]);
    lv_obj_invalidate(face_img_);

    status_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_white(), 0);
    lv_label_set_text(status_label_, "Initializing...");
    lv_obj_align(status_label_, LV_ALIGN_TOP_LEFT, 108, 4);

    // 4 stats on the next line (F / Ha / E / He).
    stats_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(stats_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(stats_label_, lv_color_white(), 0);
    lv_label_set_text(stats_label_, "...");
    lv_obj_align(stats_label_, LV_ALIGN_TOP_LEFT, 108, 20);

    // Coins on its own line so a big number never clips out of the column.
    coins_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(coins_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(coins_label_, lv_color_hex(0xFFD54F), 0);  // amber
    lv_label_set_text(coins_label_, "Coins: 0");
    lv_obj_align(coins_label_, LV_ALIGN_TOP_LEFT, 108, 36);

    // 4 bars stacked tightly on the right side, below status label.
    for (int i = 0; i < 4; i++) {
        lv_obj_t *container = lv_obj_create(root);
        lv_obj_set_size(container, 208, 22);
        lv_obj_align(container, LV_ALIGN_TOP_LEFT, 108, 60 + i * 24);
        lv_obj_set_style_pad_all(container, 2, 0);
        lv_obj_set_style_bg_color(container, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(container, 0, 0);

        lv_obj_t *name = lv_label_create(container);
        lv_label_set_text(name, BAR_NAMES[i]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 4, 0);

        bars_[i] = lv_bar_create(container);
        lv_obj_set_size(bars_[i], 120, 10);
        lv_obj_align(bars_[i], LV_ALIGN_RIGHT_MID, -4, 0);
        lv_bar_set_range(bars_[i], 0, 100);
        lv_bar_set_value(bars_[i], 50, LV_ANIM_OFF);
    }

    // Action buttons row at bottom (full width).
    auto make_btn = [&](const char *text, lv_align_t align, int x, lv_event_cb_t cb) {
        lv_obj_t *btn = lv_button_create(root);
        lv_obj_set_size(btn, 70, 28);
        lv_obj_align(btn, align, x, -8);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_center(label);
        return btn;
    };

    make_btn("Feed",  LV_ALIGN_BOTTOM_MID, -117, btn_feed_cb);
    make_btn("Play",  LV_ALIGN_BOTTOM_MID, -39,  btn_play_cb);
    btn_sleep_ = make_btn("Sleep", LV_ALIGN_BOTTOM_MID, 39, btn_sleep_cb);
    make_btn("Pet",   LV_ALIGN_BOTTOM_MID, 117,  btn_pet_cb);

    // Make sure the bars reflect the current state right after build.
    update_ui();
    return root;
}

static void destroy_page_status(lv_obj_t *root)
{
    // Stop any pending anim before deleting the widget it points at.
    lv_anim_del(face_img_, nullptr);
    face_img_ = nullptr;
    status_label_ = nullptr;
    btn_sleep_ = nullptr;
    for (int i = 0; i < 4; i++) bars_[i] = nullptr;
    action_animation_active_ = false;
    if (root) lv_obj_del(root);
}

static lv_obj_t *build_page_placeholder(lv_obj_t *parent, const char *title, const char *hint)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 208);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *t = lv_label_create(root);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *h = lv_label_create(root);
    lv_obj_set_style_text_font(h, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(0xBBBBBB), 0);
    lv_label_set_text(h, hint);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 70);
    return root;
}

// Games page shared state (file-scope). CardCb is allocated per picker card and
// freed when the card is deleted. Both are referenced by free-function event
// handlers below; they live outside build_page_games() so the handlers can see
// them without implicit capture.
struct GamesCtx {
    lv_obj_t *root = nullptr;
    lv_obj_t *picker = nullptr;
    lv_obj_t *game_root = nullptr;
    lv_obj_t *back_btn = nullptr;
    void (*active_destroy)(lv_obj_t *) = nullptr;
};
struct CardCb {
    GamesCtx *c;
    lv_obj_t *(*build_fn)(lv_obj_t *);
    void (*destroy_fn)(lv_obj_t *);
};

static void on_card_clicked(lv_event_t *e)
{
    CardCb *cb = (CardCb *)lv_event_get_user_data(e);
    if (!cb || !cb->c) return;
    if (cb->c->active_destroy && cb->c->game_root) {
        cb->c->active_destroy(cb->c->game_root);
        cb->c->game_root = nullptr;
    }
    if (cb->c->picker) lv_obj_add_flag(cb->c->picker, LV_OBJ_FLAG_HIDDEN);
    cb->c->active_destroy = cb->destroy_fn;
    cb->c->game_root = cb->build_fn(cb->c->root);
    if (cb->c->back_btn) {
        lv_obj_clear_flag(cb->c->back_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(cb->c->back_btn);
    }
}

static void on_card_freed(lv_event_t *e)
{
    CardCb *cb = (CardCb *)lv_event_get_user_data(e);
    delete cb;
}

static void on_back_clicked(lv_event_t *e)
{
    GamesCtx *c = (GamesCtx *)lv_event_get_user_data(e);
    if (!c) return;
    if (c->active_destroy && c->game_root) {
        c->active_destroy(c->game_root);
        c->game_root = nullptr;
    }
    c->active_destroy = nullptr;
    if (c->back_btn) lv_obj_add_flag(c->back_btn, LV_OBJ_FLAG_HIDDEN);
    if (c->picker) lv_obj_clear_flag(c->picker, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *build_page_games(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 208);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    auto *gctx = new GamesCtx();
    gctx->root = root;
    lv_obj_set_user_data(root, gctx);
    lv_obj_add_event_cb(root, [](lv_event_t *e) {
        GamesCtx *c = (GamesCtx *)lv_obj_get_user_data(
            (lv_obj_t *)lv_event_get_user_data(e));
        if (!c) return;
        if (c->active_destroy && c->game_root) c->active_destroy(c->game_root);
        delete c;
    }, LV_EVENT_DELETE, root);

    gctx->picker = lv_obj_create(root);
    lv_obj_set_size(gctx->picker, 320, 208);
    lv_obj_set_pos(gctx->picker, 0, 0);
    lv_obj_set_style_bg_color(gctx->picker, lv_color_black(), 0);
    lv_obj_set_style_border_width(gctx->picker, 0, 0);
    lv_obj_set_style_pad_all(gctx->picker, 0, 0);

    lv_obj_t *title = lv_label_create(gctx->picker);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Pick a game");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    int lvl = Pet::instance().get_state().level;

    auto make_card = [&](int col, const char *label, uint32_t color,
                         bool enabled,
                         lv_obj_t *(*build_fn)(lv_obj_t *),
                         void (*destroy_fn)(lv_obj_t *)) {
        lv_obj_t *card = lv_button_create(gctx->picker);
        lv_obj_set_size(card, 95, 140);
        lv_obj_set_pos(card, 12 + col * 102, 32);
        lv_obj_set_style_bg_color(card, lv_color_hex(enabled ? color : 0x555555), 0);

        lv_obj_t *name = lv_label_create(card);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_label_set_text(name, label);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 8);

        if (!enabled) {
            lv_obj_t *lock = lv_label_create(card);
            lv_obj_set_style_text_font(lock, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lock, lv_color_hex(0xDDDDDD), 0);
            lv_label_set_text(lock, "Lv2+");
            lv_obj_align(lock, LV_ALIGN_BOTTOM_MID, 0, -8);
            return;
        }

        auto *cb = new CardCb { gctx, build_fn, destroy_fn };
        lv_obj_add_event_cb(card, on_card_clicked, LV_EVENT_CLICKED, cb);
        lv_obj_add_event_cb(card, on_card_freed, LV_EVENT_DELETE, cb);
    };

    make_card(0, "Whack",    0x1976D2, true,
              pet::game_whack::build,    pet::game_whack::destroy);
    make_card(1, "Sequence", 0x388E3C, lvl >= 2,
              pet::game_sequence::build, pet::game_sequence::destroy);
    make_card(2, "Gacha",    0xC62828, true,
              pet::game_gacha::build,    pet::game_gacha::destroy);

    gctx->back_btn = lv_button_create(root);
    lv_obj_set_size(gctx->back_btn, 56, 22);
    lv_obj_set_pos(gctx->back_btn, 2, 2);
    lv_obj_set_style_bg_color(gctx->back_btn, lv_color_hex(0x37474F), 0);
    lv_obj_t *bl = lv_label_create(gctx->back_btn);
    lv_label_set_text(bl, "< Back");
    lv_obj_center(bl);
    lv_obj_add_flag(gctx->back_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(gctx->back_btn, on_back_clicked, LV_EVENT_CLICKED, gctx);

    return root;
}

static void destroy_page_games(lv_obj_t *root)
{
    if (!root) return;
    // The LV_EVENT_DELETE handler on `root` calls `c->active_destroy` for
    // the active game (which deletes its lv_timer_t*) and frees GamesCtx.
    // lv_obj_del triggers that handler.
    lv_obj_del(root);
}

static lv_obj_t *build_page_shop(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 208);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *title = lv_label_create(root);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Shop");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    struct Item {
        const char *name;
        const char *effect;  // shown on the card, e.g. "F+25  E+5"
        int price;
        lv_color_t tint;
        void (*apply)(int amount);  // Pet method to call with `amount`
        int amount;                // delta to pass to apply()
    };
    static const Item items[6] = {
        {"Snack",    "F+10",  8,  {}, [](int a){ Pet::instance().feed_with_amount(a); },   10},
        {"Meal",     "F+25 E+5",  20, {}, [](int a){ Pet::instance().feed(); }, 0},
        {"Feast",    "F+40",  35, {}, [](int a){ Pet::instance().feed_with_amount(a); },   40},
        {"Coffee",   "E+30",  15, {}, [](int a){ Pet::instance().drink_energy(a); },      30},
        {"Energy+",  "E+60",  28, {}, [](int a){ Pet::instance().drink_energy(a); },      60},
        {"Medicine", "He+50", 25, {}, [](int a){ Pet::instance().take_medicine(a); },     50},
    };

    // 3 cols × 2 rows of 100x80 cards (320 - 6*pad = ~300 / 3 = 100 each).
    const int card_w = 100, card_h = 80;
    const int x0 = 6, y0 = 28, gx = 6, gy = 6;
    for (int i = 0; i < 6; i++) {
        int col = i % 3;
        int row = i / 3;
        lv_obj_t *card = lv_button_create(root);
        lv_obj_set_size(card, card_w, card_h);
        lv_obj_set_pos(card, x0 + col * (card_w + gx), y0 + row * (card_h + gy));
        lv_obj_set_style_bg_color(card, lv_color_hex(0x37474F), 0);

        lv_obj_t *name = lv_label_create(card);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_label_set_text(name, items[i].name);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 6);

        lv_obj_t *effect = lv_label_create(card);
        lv_obj_set_style_text_font(effect, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(effect, lv_color_hex(0xBBBBBB), 0);
        lv_label_set_text(effect, items[i].effect);
        lv_obj_align(effect, LV_ALIGN_CENTER, 0, 6);

        lv_obj_t *price = lv_label_create(card);
        lv_obj_set_style_text_font(price, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(price, lv_color_hex(0xFFD54F), 0);  // amber for coins
        char buf[24];
        snprintf(buf, sizeof(buf), "%d coins", items[i].price);
        lv_label_set_text(price, buf);
        lv_obj_align(price, LV_ALIGN_BOTTOM_MID, 0, -6);

        lv_obj_add_event_cb(card, [](lv_event_t *e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            const Item *it = &items[idx];
            if (Pet::instance().spend_coins(it->price)) {
                it->apply(it->amount);
                ESP_LOGI(TAG, "Bought %s", it->name);
            } else {
                ESP_LOGW(TAG, "Not enough coins for %s", it->name);
            }
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    return root;
}

static lv_obj_t *build_page_about(lv_obj_t *parent)
{
    return build_page_placeholder(parent, "About",
        "ESP32-S3 Pet v0.4.0\nLVGL 9.4 + NimBLE\nFS:BSD Pet Project");
}

// ---------------- Boot --------------------------------------------------------
static void build_ui()
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

    pet::pages::register_page(pet::pages::Page::Status, build_page_status, destroy_page_status);
    pet::pages::register_page(pet::pages::Page::Games,  build_page_games,  destroy_page_games);
    pet::pages::register_page(pet::pages::Page::Shop,   build_page_shop,   nullptr);
    pet::pages::register_page(pet::pages::Page::About,  build_page_about,  nullptr);

    pet::pages::build_tabs(screen);
    pet::pages::switch_page(pet::pages::Page::Status);

    lv_timer_create(on_update_timer, 500, nullptr);
}

static void pet_task(void *arg)
{
    bsp::QMI8658_Data imu_data;
    int notify_counter = 0;
    int save_counter = 0;
    pet::idle_events::init();

    while (true) {
        Pet::instance().update();
        pet::idle_events::tick(Pet::instance().get_state().age_ticks);

        if (Pet::instance().is_dirty()) {
            if (++save_counter >= 50) {
                pet::save::save_if_dirty(Pet::instance(), false);
                save_counter = 0;
            }
        } else {
            save_counter = 0;
        }

        if (bsp::QMI8658::instance().read(&imu_data) == ESP_OK) {
            if (bsp::QMI8658::instance().detect_shake(imu_data)) {
                ESP_LOGI(TAG, "Shake detected!");
                if (!Pet::instance().is_sleeping()) {
                    Pet::instance().play();
                } else {
                    Pet::instance().wake_up();
                }
            }
        }

        if (++notify_counter >= 20) {
            pet_ble::notify_state();
            notify_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t start_ui()
{
    if (lvgl_port_lock(0)) {
        build_ui();
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock for UI build");
        return ESP_FAIL;
    }

    Pet::instance().init();
    pet::save::init();
    pet::save::load(Pet::instance());

    xTaskCreate(pet_task, "pet_task", 4096, nullptr, 5, nullptr);

    ESP_LOGI(TAG, "Pet UI started");
    return ESP_OK;
}

} // namespace pet