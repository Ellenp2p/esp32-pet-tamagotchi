#include "pet_ui.h"
#include "pet_state.h"
#include "pet_save.h"
#include "pet_meta.h"
#include "pet_pages.h"
#include "pet_game_whack.h"
#include "pet_game_sequence.h"
#include "pet_game_gacha.h"
#include "pet_frames.h"
#include "pet_idle_events.h"
#include "app/ble_pet.h"
#include "app/wifi_manager.h"
#include "app/pet_ai_usage.h"
#include "app/screen_power.h"
#include "app/ui_main_task.h"
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
static lv_obj_t *wifi_label_ = nullptr;     // Top-right WiFi status indicator
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
    char line1[48], line2[64], line3[32];
    // v0.6: status line shows the pet's life stage, not just "Awake".
    // Tombstone is shown as "RIP" with the stage elapsed count.
    const char *stg_name = pet::life_stage_name(Pet::instance().stage());
    snprintf(line1, sizeof(line1), "%s | Lv%d", stg_name, s.level);
    snprintf(line2, sizeof(line2), "F:%d Ha:%d E:%d He:%d",
             s.fullness, s.happiness, s.energy, s.health);
    int coins_disp = s.coins > 9999 ? 9999 : s.coins;
    snprintf(line3, sizeof(line3), "Coins: %d%s",
             coins_disp, s.coins > 9999 ? "+" : "");
    lv_label_set_text(status_label_, line1);
    lv_label_set_text(stats_label_, line2);
    lv_label_set_text(coins_label_, line3);

    // v0.6.6: top-right WiFi indicator. Color cues:
    //   CONNECTED   -> green  "WiFi:SSID"
    //   CONNECTING  -> amber  "WiFi:..."
    //   SCANNING    -> amber  "WiFi:scan"
    //   FAILED      -> red    "WiFi:!"
    //   IDLE/DISCON -> grey   "WiFi:--"
    if (wifi_label_) {
        app::wifi_status ws;
        app::wifi_manager_get_status(&ws);
        char wbuf[40];
        const char *color_hex = "90A4AE";  // dim grey
        switch (ws.state) {
            case app::WIFI_CONN_CONNECTED:
                snprintf(wbuf, sizeof(wbuf), "WiFi:%s", ws.ssid);
                color_hex = "66BB6A";  // green
                break;
            case app::WIFI_CONN_CONNECTING:
                snprintf(wbuf, sizeof(wbuf), "WiFi:...");
                color_hex = "FFD54F";  // amber
                break;
            case app::WIFI_CONN_SCANNING:
                snprintf(wbuf, sizeof(wbuf), "WiFi:scan");
                color_hex = "FFD54F";
                break;
            case app::WIFI_CONN_FAILED:
                snprintf(wbuf, sizeof(wbuf), "WiFi:!");
                color_hex = "EF5350";  // red
                break;
            case app::WIFI_CONN_DISCONNECTED:
            case app::WIFI_CONN_IDLE:
            default:
                snprintf(wbuf, sizeof(wbuf), "WiFi:--");
                color_hex = "90A4AE";  // grey
                break;
        }
        lv_label_set_text(wifi_label_, wbuf);
        lv_obj_set_style_text_color(wifi_label_,
                                    lv_color_hex(strtoul(color_hex, nullptr, 16)),
                                    0);
    }

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

    // Top-right WiFi indicator — small badge showing connection state.
    // Updated by pet_task from wifi_manager_get_status().
    wifi_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(wifi_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(wifi_label_, lv_color_hex(0x90A4AE), 0);  // dim grey
    lv_label_set_text(wifi_label_, "[WiFi: --]");
    lv_obj_align(wifi_label_, LV_ALIGN_TOP_RIGHT, -2, 4);

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
    wifi_label_ = nullptr;
    btn_sleep_ = nullptr;
    for (int i = 0; i < 4; i++) bars_[i] = nullptr;
    action_animation_active_ = false;
    if (root) lv_obj_del(root);
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

    pet::LifeStage stg = Pet::instance().stage();

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

    // v0.6: gate games by LifeStage. Whack is always available (kids can
    // play). Sequence unlocks at Child (~30 min). Gacha at Teen (~60 min).
    // Tombstone disables all games.
    bool tomb = (stg == pet::LifeStage::Tombstone);
    make_card(0, "Whack",    0x1976D2, !tomb,
              pet::game_whack::build,    pet::game_whack::destroy);
    make_card(1, "Sequence", 0x388E3C, !tomb && (int)stg >= (int)pet::LifeStage::Child,
              pet::game_sequence::build, pet::game_sequence::destroy);
    make_card(2, "Gacha",    0xC62828, !tomb && (int)stg >= (int)pet::LifeStage::Teen,
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

// v0.6.6: WiFi settings page. Layout (320x208):
//   y=0..32   status bar  (SSID / state / IP)
//   y=36..56  "Rescan" + "Forget" + "Disconnect" buttons
//   y=60..168 AP list  (each item: SSID, lock icon, RSSI, "*" if connected)
//   y=170..200 Lock row + auto-off toggles
struct SettingsCtx {
    lv_obj_t *status_label    = nullptr;
    lv_obj_t *list            = nullptr;
    lv_obj_t *lock_btn        = nullptr;
    lv_obj_t *to_off_btn      = nullptr;
    lv_obj_t *to_2min_btn     = nullptr;
    lv_obj_t *to_5min_btn     = nullptr;
    lv_obj_t *to_label        = nullptr;
    // Password popup state — created on demand when an AP is selected.
    lv_obj_t *pass_popup      = nullptr;
    lv_obj_t *pass_ta         = nullptr;
    lv_obj_t *pass_kb         = nullptr;
    char      pass_ssid[33]   = {};
};
static SettingsCtx s_settings;

// Forward decls for the per-AP list-button handler.
static void on_ap_clicked(lv_event_t *e);
static void on_rescan_clicked(lv_event_t *e);
static void on_disconnect_clicked(lv_event_t *e);
static void on_forget_clicked(lv_event_t *e);
static void refresh_settings_status();
// v0.7: Lock + auto-off toggle handlers
static void on_lock_clicked(lv_event_t *e);
static void on_to_off_clicked(lv_event_t *e);
static void on_to_2min_clicked(lv_event_t *e);
static void on_to_5min_clicked(lv_event_t *e);
static void refresh_timeout_label();

// One-shot lvgl timer that polls wifi_manager state and refreshes the UI.
static lv_timer_t *s_settings_poll = nullptr;
static int s_last_state = -1;
static void rebuild_ap_list();

static void settings_poll_cb(lv_timer_t *t)
{
    (void)t;
    refresh_settings_status();
    app::wifi_status st;
    app::wifi_manager_get_status(&st);
    // v0.6.6 fix: rebuild the AP list whenever SCAN_DONE transitions us
    // out of SCANNING. The list is built once at page entry; without this
    // the user is stuck on "(no scan results)" even though s_scan_count>0.
    if (s_last_state == app::WIFI_CONN_SCANNING &&
        st.state != app::WIFI_CONN_SCANNING) {
        rebuild_ap_list();
    }
    s_last_state = st.state;
}

static void refresh_settings_status()
{
    if (!s_settings.status_label) return;
    app::wifi_status st;
    app::wifi_manager_get_status(&st);
    const char *state_str = "?";
    switch (st.state) {
        case app::WIFI_CONN_IDLE:        state_str = "Idle";        break;
        case app::WIFI_CONN_SCANNING:    state_str = "Scanning";    break;
        case app::WIFI_CONN_CONNECTING:  state_str = "Connecting";  break;
        case app::WIFI_CONN_CONNECTED:   state_str = "Connected";   break;
        case app::WIFI_CONN_FAILED:      state_str = "Failed";      break;
        case app::WIFI_CONN_DISCONNECTED: state_str = "Disconnected"; break;
    }
    char buf[96];
    if (st.ssid[0] == 0) {
        snprintf(buf, sizeof(buf), "WiFi: %s", state_str);
    } else {
        snprintf(buf, sizeof(buf), "WiFi: %s  %s  %s",
                 st.ssid, state_str, st.ip);
    }
    lv_label_set_text(s_settings.status_label, buf);
}

static void rebuild_ap_list()
{
    if (!s_settings.list) return;
    lv_obj_clean(s_settings.list);
    int n = app::wifi_manager_scan_count();
    const wifi_ap_record_t *aps = app::wifi_manager_scan_results();
    app::wifi_status st;
    app::wifi_manager_get_status(&st);
    for (int i = 0; i < n; i++) {
        // Skip hidden / empty SSIDs.
        if (aps[i].ssid[0] == 0) continue;
        char label[64];
        bool connected = (strncmp((char *)aps[i].ssid, st.ssid,
                                  sizeof(aps[i].ssid)) == 0);
        // Lock char: "" (open) or "#" (WPA).
        const char *lock = (aps[i].authmode == WIFI_AUTH_OPEN) ? "" : "#";
        // v0.6.6 visual: convert RSSI dBm into a 0..4 bar ASCII gauge.
        // All chars are basic ASCII so they are guaranteed in the
        // montserrat_12 subset that the list buttons use. Each filled
        // bar is one `|` plus a space separator, empty bars are three
        // spaces so each bar slot is 3 columns wide; total gauge width
        // is always 12 columns regardless of strength.
        //   >= -55  ->  4 bars "|||          "  (excellent)
        //   >= -67  ->  3 bars  "|||          -" reduce to " |||       "
        //   >= -75  ->  2 bars  "  ||        "
        //   >= -82  ->  1 bar   "   |        "
        //   <  -82  ->  0 bars  "            "
        int rssi = aps[i].rssi;
        int bars;
        if      (rssi >= -55) bars = 4;
        else if (rssi >= -67) bars = 3;
        else if (rssi >= -75) bars = 2;
        else if (rssi >= -82) bars = 1;
        else                  bars = 0;
        char bar_chars[13];
        // Each bar = "|  " (3 chars wide); 4 slots = 12 chars total.
        for (int b = 0; b < 4; b++) {
            bar_chars[b * 3 + 0] = (b < bars) ? '|' : ' ';
            bar_chars[b * 3 + 1] = ' ';
            bar_chars[b * 3 + 2] = ' ';
        }
        bar_chars[12] = 0;
        snprintf(label, sizeof(label), "%s%-20.20s%s%s",
                 connected ? "*" : " ",
                 (char *)aps[i].ssid,
                 bar_chars,
                 lock);
        lv_obj_t *btn = lv_list_add_button(s_settings.list, NULL, label);
        // Allocate a heap copy of the SSID so the click handler can read it.
        // Free in the LV_EVENT_DELETE callback below.
        char *ssid_copy = (char *)lv_malloc(sizeof(aps[i].ssid));
        if (!ssid_copy) return;
        strncpy(ssid_copy, (char *)aps[i].ssid, sizeof(aps[i].ssid) - 1);
        ssid_copy[sizeof(aps[i].ssid) - 1] = 0;
        lv_obj_set_user_data(btn, ssid_copy);
        lv_obj_add_event_cb(btn, on_ap_clicked, LV_EVENT_CLICKED, ssid_copy);
        // Free the copy on delete so we don't leak.
        lv_obj_add_event_cb(btn, [](lv_event_t *ev) {
            lv_obj_t *target = (lv_obj_t *)lv_event_get_current_target(ev);
            char *p = (char *)lv_obj_get_user_data(target);
            if (p) {
                lv_free(p);
                lv_obj_set_user_data(target, nullptr);
            }
        }, LV_EVENT_DELETE, nullptr);
    }
    if (n == 0) {
        lv_list_add_text(s_settings.list, "(no scan results)");
    }
}

static void on_rescan_clicked(lv_event_t *e)
{
    (void)e;
    app::wifi_manager_scan_start();
}

static void on_disconnect_clicked(lv_event_t *e)
{
    (void)e;
    app::wifi_manager_disconnect();
    refresh_settings_status();
}

static void on_forget_clicked(lv_event_t *e)
{
    (void)e;
    app::wifi_manager_forget();
    refresh_settings_status();
}

// v0.7: Lock now / auto-off toggle handlers.
static void on_lock_clicked(lv_event_t *e)
{
    (void)e;
    pet::ScreenPower::instance().lock_now();
}

static void on_to_off_clicked(lv_event_t *e)
{
    (void)e;
    pet::ScreenPower::instance().set_timeout(pet::ScreenTimeout::Off);
    refresh_timeout_label();
}

static void on_to_2min_clicked(lv_event_t *e)
{
    (void)e;
    pet::ScreenPower::instance().set_timeout(pet::ScreenTimeout::Min2);
    refresh_timeout_label();
}

static void on_to_5min_clicked(lv_event_t *e)
{
    (void)e;
    pet::ScreenPower::instance().set_timeout(pet::ScreenTimeout::Min5);
    refresh_timeout_label();
}

static void refresh_timeout_label()
{
    if (!s_settings.to_label) return;
    pet::ScreenTimeout t = pet::ScreenPower::instance().timeout();
    const char *txt = "?";
    switch (t) {
        case pet::ScreenTimeout::Off:  txt = "Off";    break;
        case pet::ScreenTimeout::Min2: txt = "2 min";  break;
        case pet::ScreenTimeout::Min5: txt = "5 min";  break;
    }
    lv_label_set_text(s_settings.to_label, txt);
}

static void close_pass_popup()
{
    if (s_settings.pass_kb) {
        lv_obj_del(s_settings.pass_kb);
        s_settings.pass_kb = nullptr;
    }
    if (s_settings.pass_popup) {
        lv_obj_del(s_settings.pass_popup);
        s_settings.pass_popup = nullptr;
        s_settings.pass_ta    = nullptr;
    }
}

// "Connect" button inside the password popup.
static void on_connect_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI("pet_ui", "Connect CLICK, ta=%p", (void *)s_settings.pass_ta);
    if (!s_settings.pass_ta) {
        ESP_LOGW("pet_ui", "Connect: ta is null");
        close_pass_popup();
        return;
    }
    const char *pass = lv_textarea_get_text(s_settings.pass_ta);
    if (!pass) pass = "";
    ESP_LOGI("pet_ui", "Connect: ssid='%s' pass_len=%d",
             s_settings.pass_ssid, (int)strlen(pass));
    if (s_settings.pass_ssid[0] == 0) {
        ESP_LOGW("pet_ui", "Connect: ssid empty");
        close_pass_popup();
        return;
    }
    esp_err_t rc = app::wifi_manager_connect(s_settings.pass_ssid, pass);
    ESP_LOGI("pet_ui", "wifi_manager_connect rc=%s",
             esp_err_to_name(rc));
    close_pass_popup();
    refresh_settings_status();
}

static void on_ap_clicked(lv_event_t *e)
{
    char *ssid = (char *)lv_event_get_user_data(e);
    if (!ssid) return;
    strncpy(s_settings.pass_ssid, ssid, sizeof(s_settings.pass_ssid) - 1);
    s_settings.pass_ssid[sizeof(s_settings.pass_ssid) - 1] = 0;

    // Build a modal-style overlay containing a password field + Connect.
    lv_obj_t *popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(popup, 280, 130);
    lv_obj_set_pos(popup, 20, 38);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x263238), 0);
    lv_obj_set_style_border_color(popup, lv_color_hex(0x1976D2), 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_radius(popup, 8, 0);
    lv_obj_set_style_pad_all(popup, 8, 0);

    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text_fmt(title, "Password: %s", ssid);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_t *ta = lv_textarea_create(popup);
    lv_obj_set_size(ta, 230, 32);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, -16, 30);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "password");

    // v0.6.6: pre-fill the textarea with the cached NVS password if the
    // user is editing the previously-saved SSID. Other APs leave the
    // field blank.
    char saved_ssid[33] = {};
    char saved_pass[64] = {};
    if (app::wifi_manager_get_saved_credentials(saved_ssid, sizeof(saved_ssid),
                                                saved_pass, sizeof(saved_pass))
        && strcmp(saved_ssid, ssid) == 0
        && saved_pass[0] != 0) {
        lv_textarea_set_text(ta, saved_pass);
    }

    // v0.6.6: Show/Hide button next to the password field. Toggle the
    // textarea's password_mode flag.
    lv_obj_t *show_btn = lv_button_create(popup);
    lv_obj_set_size(show_btn, 32, 32);
    lv_obj_align(show_btn, LV_ALIGN_TOP_RIGHT, -2, 30);
    lv_obj_t *show_lbl = lv_label_create(show_btn);
    lv_label_set_text(show_lbl, "*");
    lv_obj_center(show_lbl);
    lv_obj_add_event_cb(show_btn, [](lv_event_t *ev) {
        lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(ev);
        lv_obj_t *label = (lv_obj_t *)lv_obj_get_child(btn, 0);
        if (!s_settings.pass_ta || !label) return;
        bool now_hidden = !lv_textarea_get_password_mode(s_settings.pass_ta);
        lv_textarea_set_password_mode(s_settings.pass_ta, now_hidden);
        lv_label_set_text(label, now_hidden ? "*" : "o");
    }, LV_EVENT_CLICKED, nullptr);

    // Open the keyboard so the user can type immediately.
    lv_obj_t *kb = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_bg_opa(kb, LV_OPA_90, 0);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_size(kb, 320, 110);
    // v0.6.6 fix: tapping the keyboard's "OK" (✓) button only emits
    // LV_EVENT_READY — the keyboard widget does not delete itself.
    // Without this handler the keyboard stays on screen after the user
    // confirms, blocking everything else. Delete it here; the popup
    // stays open so the user can still tap Cancel/Connect.
    lv_obj_add_event_cb(kb, [](lv_event_t *ev) {
        lv_obj_t *kbd = (lv_obj_t *)lv_event_get_current_target(ev);
        if (lv_obj_is_valid(kbd)) {
            lv_obj_del(kbd);
        }
        if (s_settings.pass_kb == kbd) {
            s_settings.pass_kb = nullptr;
        }
    }, LV_EVENT_READY, nullptr);
    lv_obj_t *cancel_btn = lv_button_create(popup);
    lv_obj_set_size(cancel_btn, 100, 30);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *ev) {
        (void)ev;
        close_pass_popup();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);

    lv_obj_t *connect_btn = lv_button_create(popup);
    lv_obj_set_size(connect_btn, 100, 30);
    lv_obj_align(connect_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -4);
    lv_obj_add_event_cb(connect_btn, on_connect_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *connect_lbl = lv_label_create(connect_btn);
    lv_label_set_text(connect_lbl, "Connect");
    lv_obj_center(connect_lbl);

    s_settings.pass_popup = popup;
    s_settings.pass_ta    = ta;
    s_settings.pass_kb    = kb;
}

static lv_obj_t *build_page_settings(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 208);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    // Status bar.
    s_settings.status_label = lv_label_create(root);
    lv_obj_set_style_text_font(s_settings.status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_settings.status_label, lv_color_white(), 0);
    lv_obj_align(s_settings.status_label, LV_ALIGN_TOP_LEFT, 4, 2);
    refresh_settings_status();

    // Button row.
    auto make_btn = [&](const char *text, int x, lv_event_cb_t cb) {
        lv_obj_t *b = lv_button_create(root);
        lv_obj_set_size(b, 70, 26);
        lv_obj_set_pos(b, x, 22);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x37474F), 0);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(b);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_center(lbl);
        return b;
    };
    make_btn("Rescan",      4,  on_rescan_clicked);
    make_btn("Disconnect", 78,  on_disconnect_clicked);
    make_btn("Forget",     152, on_forget_clicked);

    // AP list — shrunk from 152 to 108 px tall to leave room for the
    // Lock + auto-off row at the bottom.
    s_settings.list = lv_list_create(root);
    lv_obj_set_size(s_settings.list, 312, 108);
    lv_obj_set_pos(s_settings.list, 4, 52);
    lv_obj_set_style_bg_color(s_settings.list, lv_color_hex(0x101010), 0);
    lv_obj_set_style_pad_all(s_settings.list, 0, 0);
    lv_obj_set_style_border_width(s_settings.list, 0, 0);
    rebuild_ap_list();

    // v0.7: Lock + auto-off toggle row (y=164..198).
    // Layout:
    //   x=4      "Lock" button
    //   x=80     "Auto-off:" label
    //   x=140    "Off" toggle, x=180 "2m", x=220 "5m"
    //   x=260    current selection text
    s_settings.lock_btn = lv_button_create(root);
    lv_obj_set_size(s_settings.lock_btn, 70, 24);
    lv_obj_set_pos(s_settings.lock_btn, 4, 170);
    lv_obj_set_style_bg_color(s_settings.lock_btn, lv_color_hex(0xC62828), 0);
    lv_obj_add_event_cb(s_settings.lock_btn, on_lock_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lock_lbl = lv_label_create(s_settings.lock_btn);
    lv_label_set_text(lock_lbl, "Lock");
    lv_obj_set_style_text_font(lock_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lock_lbl);

    lv_obj_t *to_hdr = lv_label_create(root);
    lv_obj_set_style_text_font(to_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(to_hdr, lv_color_hex(0x90A4AE), 0);
    lv_label_set_text(to_hdr, "Auto-off:");
    lv_obj_set_pos(to_hdr, 84, 176);

    auto make_to_btn = [&](const char *txt, int x, lv_event_cb_t cb) {
        lv_obj_t *b = lv_button_create(root);
        lv_obj_set_size(b, 36, 24);
        lv_obj_set_pos(b, x, 170);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x37474F), 0);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(b);
        lv_label_set_text(lbl, txt);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        return b;
    };
    s_settings.to_off_btn  = make_to_btn("Off", 156, on_to_off_clicked);
    s_settings.to_2min_btn = make_to_btn("2m",  194, on_to_2min_clicked);
    s_settings.to_5min_btn = make_to_btn("5m",  232, on_to_5min_clicked);

    s_settings.to_label = lv_label_create(root);
    lv_obj_set_style_text_font(s_settings.to_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_settings.to_label, lv_color_white(), 0);
    lv_obj_set_pos(s_settings.to_label, 274, 176);
    refresh_timeout_label();

    // Poll the status every 500 ms so the user sees CONNECTING →
    // CONNECTED transitions without a manual refresh.
    s_settings_poll = lv_timer_create(settings_poll_cb, 500, nullptr);

    return root;
}

static void destroy_page_settings(lv_obj_t *root)
{
    (void)root;
    if (s_settings_poll) {
        lv_timer_del(s_settings_poll);
        s_settings_poll = nullptr;
    }
    close_pass_popup();
    s_settings.status_label = nullptr;
    s_settings.list         = nullptr;
    s_settings.lock_btn     = nullptr;
    s_settings.to_off_btn   = nullptr;
    s_settings.to_2min_btn  = nullptr;
    s_settings.to_5min_btn  = nullptr;
    s_settings.to_label     = nullptr;
}

// ---------------- Boot --------------------------------------------------------
static void build_ui()
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

    pet::pages::register_page(pet::pages::Page::Status,   build_page_status,   destroy_page_status);
    pet::pages::register_page(pet::pages::Page::Games,    build_page_games,    destroy_page_games);
    pet::pages::register_page(pet::pages::Page::Shop,     build_page_shop,     nullptr);
    pet::pages::register_page(pet::pages::Page::Settings, build_page_settings, destroy_page_settings);
    // v0.6.7: AI Usage tab — no-op when no keys are configured.
    pet::ai_usage::register_page_handlers();

    pet::pages::build_tabs(screen);
    pet::pages::switch_page(pet::pages::Page::Status);

    lv_timer_create(on_update_timer, 500, nullptr);
}

esp_err_t start_ui()
{
    // v0.6.6: bring up the Wi-Fi subsystem as early as possible so the
    // command queue exists by the time the Settings page appears. The
    // worker task initialises esp_netif / esp_wifi in STA mode and (if
    // NVS has saved credentials) kicks off the auto-connect on its own
    // thread. We deliberately call it before building the LVGL UI so
    // the first scan/connect doesn't stall the boot sequence.
    app::wifi_manager_init();

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

    // v0.6: check daily streak once the system time is stable. If NTP
    // hasn't synced yet (no WiFi, or first boot), today_epoch_day() returns
    // 0 and meta::record_open_day_and_reward() short-circuits.
    int streak = pet::meta::record_open_day_and_reward(
        pet::meta::today_epoch_day());
    if (streak >= 1) {
        ESP_LOGI(TAG, "Daily streak active: %d days, +%d coins",
                 streak, streak * 10);
    }

    PetMainTask::instance().start();

    ESP_LOGI(TAG, "Pet UI started");
    return ESP_OK;
}

} // namespace pet