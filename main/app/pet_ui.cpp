#include "pet_ui.h"
#include "pet_save.h"
#include "pet_meta.h"
#include "pet_game_whack.h"
#include "pet_game_sequence.h"
#include "pet_game_gacha.h"
#include "pet_frames.h"
#include "pet_idle_events.h"
#include "app/ble_pet.h"
#include "app/pet_ai_usage.h"
#include "app/ui_main_task.h"
#include "bsp/bsp_qmi8658.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pet {

static const char *TAG = "pet_ui";

static const char *BAR_NAMES[4] = {"Fullness", "Happy", "Energy", "Health"};

static const uint32_t kFrameMs[PET_ANIM_COUNT] = {
    200, 180, 160, 400, 180, 240,
};
static const uint16_t kFrameCount = 9;

// Games page shared types (kept at file scope — stack-friendly structs).
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

// ---- singleton -------------------------------------------------------------

PetUi &PetUi::instance() noexcept
{
    static PetUi s;
    return s;
}

// ---- helpers ---------------------------------------------------------------

void PetUi::set_bar_value_with_warn(lv_obj_t *bar, int value)
{
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_color_t color;
    if (value < 10)      color = lv_color_hex(0xE53935);
    else if (value < 25) color = lv_color_hex(0xFB8C00);
    else                 color = lv_color_hex(0x43A047);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
}

pet_anim_state_t PetUi::resolve_anim_state(const State &s, bool sleeping)
{
    if (sleeping) return PET_ANIM_SLEEPING;
    if (s.health < 30 || s.fullness < 20 || s.happiness < 20) return PET_ANIM_SICK;
    if (s.happiness > 80 && s.energy > 50) return PET_ANIM_HAPPY;
    return PET_ANIM_IDLE;
}

void PetUi::anim_frame_exec(void *var, int32_t value)
{
    auto &self = instance();
    lv_obj_t *img = (lv_obj_t *)var;
    const pet_anim_state_t anim = self.current_anim_;
    if (anim >= PET_ANIM_COUNT) return;
    const int32_t idx = value % (int32_t)kFrameCount;
    if (idx < 0) return;
    lv_image_set_src(img, pet_anim_frames[anim][idx]);
}

void PetUi::anim_ready_cb(lv_anim_t *a)
{
    auto &self = instance();
    if (self.action_anim_active_) {
        self.action_anim_active_ = false;
        const State s = Pet::instance().get_state();
        const pet_anim_state_t next =
            resolve_anim_state(s, Pet::instance().is_sleeping());
        if (next != self.current_anim_) switch_to_animation(next);
    } else {
        lv_anim_set_repeat_count(a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(a);
    }
}

void PetUi::switch_to_animation(pet_anim_state_t anim)
{
    auto &self = instance();
    if (!self.face_img_) return;
    if (anim >= PET_ANIM_COUNT) return;
    self.current_anim_ = anim;

    lv_anim_init(&self.face_anim_buf_);
    lv_anim_set_var(&self.face_anim_buf_, self.face_img_);
    lv_anim_set_exec_cb(&self.face_anim_buf_, anim_frame_exec);
    lv_anim_set_values(&self.face_anim_buf_, 0, (int32_t)kFrameCount);
    lv_anim_set_duration(&self.face_anim_buf_, kFrameCount * kFrameMs[anim]);
    lv_anim_set_repeat_count(&self.face_anim_buf_, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_ready_cb(&self.face_anim_buf_, anim_ready_cb);
    lv_anim_start(&self.face_anim_buf_);

    lv_image_set_src(self.face_img_, pet_anim_frames[anim][0]);
    lv_obj_invalidate(self.face_img_);
}

void PetUi::start_action_anim(pet_anim_state_t anim, uint16_t repeat_count)
{
    auto &self = instance();
    if (!self.face_img_) return;
    if (anim >= PET_ANIM_COUNT) return;
    self.action_anim_active_ = true;
    self.current_anim_ = anim;

    lv_anim_init(&self.action_anim_buf_);
    lv_anim_set_var(&self.action_anim_buf_, self.face_img_);
    lv_anim_set_exec_cb(&self.action_anim_buf_, anim_frame_exec);
    lv_anim_set_values(&self.action_anim_buf_, 0, (int32_t)kFrameCount);
    lv_anim_set_duration(&self.action_anim_buf_, kFrameCount * kFrameMs[anim]);
    lv_anim_set_repeat_count(&self.action_anim_buf_, repeat_count);
    lv_anim_set_ready_cb(&self.action_anim_buf_, anim_ready_cb);
    lv_anim_start(&self.action_anim_buf_);

    lv_image_set_src(self.face_img_, pet_anim_frames[anim][0]);
    lv_obj_invalidate(self.face_img_);
}

// ---- Update loop -----------------------------------------------------------

void PetUi::update_ui()
{
    auto &self = instance();
    if (!self.face_img_) return;

    State s = Pet::instance().get_state();
    bool sleeping = Pet::instance().is_sleeping();

    set_bar_value_with_warn(self.bars_[0], s.fullness);
    set_bar_value_with_warn(self.bars_[1], s.happiness);
    set_bar_value_with_warn(self.bars_[2], s.energy);
    set_bar_value_with_warn(self.bars_[3], s.health);

    if (!self.action_anim_active_) {
        const pet_anim_state_t target = resolve_anim_state(s, sleeping);
        if (target != self.current_anim_) switch_to_animation(target);
    }

    char line1[48], line2[64], line3[32];
    const char *stg_name = pet::life_stage_name(Pet::instance().stage());
    snprintf(line1, sizeof(line1), "%s | Lv%d", stg_name, s.level);
    snprintf(line2, sizeof(line2), "F:%d Ha:%d E:%d He:%d",
             s.fullness, s.happiness, s.energy, s.health);
    int coins_disp = s.coins > 9999 ? 9999 : s.coins;
    snprintf(line3, sizeof(line3), "Coins: %d%s",
             coins_disp, s.coins > 9999 ? "+" : "");
    lv_label_set_text(self.status_label_, line1);
    lv_label_set_text(self.stats_label_, line2);
    lv_label_set_text(self.coins_label_, line3);

    if (self.wifi_label_) {
        app::wifi_status ws;
        app::WifiManager::instance().get_status(ws);
        char wbuf[40];
        const char *color_hex = "90A4AE";
        switch (ws.state) {
            case app::wifi_conn_state::Connected:
                snprintf(wbuf, sizeof(wbuf), "WiFi:%s", ws.ssid);
                color_hex = "66BB6A";
                break;
            case app::wifi_conn_state::Connecting:
                snprintf(wbuf, sizeof(wbuf), "WiFi:...");
                color_hex = "FFD54F";
                break;
            case app::wifi_conn_state::Scanning:
                snprintf(wbuf, sizeof(wbuf), "WiFi:scan");
                color_hex = "FFD54F";
                break;
            case app::wifi_conn_state::Failed:
                snprintf(wbuf, sizeof(wbuf), "WiFi:!");
                color_hex = "EF5350";
                break;
            case app::wifi_conn_state::Disconnected:
            case app::wifi_conn_state::Idle:
            default:
                snprintf(wbuf, sizeof(wbuf), "WiFi:--");
                color_hex = "90A4AE";
                break;
        }
        lv_label_set_text(self.wifi_label_, wbuf);
        lv_obj_set_style_text_color(self.wifi_label_,
                                    lv_color_hex(strtoul(color_hex, nullptr, 16)),
                                    0);
    }

    lv_label_set_text(lv_obj_get_child(self.btn_sleep_, 0), sleeping ? "Wake" : "Sleep");
}

void PetUi::on_update_timer(lv_timer_t *timer)
{
    (void)timer;
    update_ui();
}

// ---- Action button callbacks -----------------------------------------------

void PetUi::btn_feed_cb(lv_event_t *e)
{
    (void)e;
    Pet::instance().feed();
    start_action_anim(PET_ANIM_EATING, 2);
}

void PetUi::btn_play_cb(lv_event_t *e)
{
    (void)e;
    Pet::instance().play();
    start_action_anim(PET_ANIM_PLAYING, 3);
}

void PetUi::btn_sleep_cb(lv_event_t *e)
{
    (void)e;
    if (Pet::instance().is_sleeping()) Pet::instance().wake_up();
    else                               Pet::instance().sleep();
}

void PetUi::btn_pet_cb(lv_event_t *e)
{
    (void)e;
    Pet::instance().pet();
}

// ---- Page: Status ----------------------------------------------------------

lv_obj_t *PetUi::build_page_status(lv_obj_t *parent)
{
    auto &self = instance();

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 208);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    self.face_img_ = lv_image_create(root);
    lv_obj_set_size(self.face_img_, 96, 96);
    lv_obj_align(self.face_img_, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_image_set_src(self.face_img_, pet_anim_frames[PET_ANIM_IDLE][0]);
    lv_obj_invalidate(self.face_img_);

    self.status_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(self.status_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(self.status_label_, lv_color_white(), 0);
    lv_label_set_text(self.status_label_, "Initializing...");
    lv_obj_align(self.status_label_, LV_ALIGN_TOP_LEFT, 108, 4);

    self.wifi_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(self.wifi_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(self.wifi_label_, lv_color_hex(0x90A4AE), 0);
    lv_label_set_text(self.wifi_label_, "[WiFi: --]");
    lv_obj_align(self.wifi_label_, LV_ALIGN_TOP_RIGHT, -2, 4);

    self.stats_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(self.stats_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(self.stats_label_, lv_color_white(), 0);
    lv_label_set_text(self.stats_label_, "...");
    lv_obj_align(self.stats_label_, LV_ALIGN_TOP_LEFT, 108, 20);

    self.coins_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(self.coins_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(self.coins_label_, lv_color_hex(0xFFD54F), 0);
    lv_label_set_text(self.coins_label_, "Coins: 0");
    lv_obj_align(self.coins_label_, LV_ALIGN_TOP_LEFT, 108, 36);

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

        self.bars_[i] = lv_bar_create(container);
        lv_obj_set_size(self.bars_[i], 120, 10);
        lv_obj_align(self.bars_[i], LV_ALIGN_RIGHT_MID, -4, 0);
        lv_bar_set_range(self.bars_[i], 0, 100);
        lv_bar_set_value(self.bars_[i], 50, LV_ANIM_OFF);
    }

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
    self.btn_sleep_ = make_btn("Sleep", LV_ALIGN_BOTTOM_MID, 39, btn_sleep_cb);
    make_btn("Pet",   LV_ALIGN_BOTTOM_MID, 117,  btn_pet_cb);

    update_ui();
    return root;
}

void PetUi::destroy_page_status(lv_obj_t *root)
{
    auto &self = instance();
    lv_anim_del(self.face_img_, nullptr);
    self.face_img_ = nullptr;
    self.status_label_ = nullptr;
    self.wifi_label_ = nullptr;
    self.btn_sleep_ = nullptr;
    for (int i = 0; i < 4; i++) self.bars_[i] = nullptr;
    self.action_anim_active_ = false;
    if (root) lv_obj_del(root);
}

// ---- Page: Games -----------------------------------------------------------

void PetUi::on_card_clicked(lv_event_t *e)
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

void PetUi::on_card_freed(lv_event_t *e)
{
    CardCb *cb = (CardCb *)lv_event_get_user_data(e);
    delete cb;
}

void PetUi::on_back_clicked(lv_event_t *e)
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

lv_obj_t *PetUi::build_page_games(lv_obj_t *parent)
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

    bool tomb = (stg == pet::LifeStage::Tombstone);
    make_card(0, "Whack",    0x1976D2, !tomb,
              WhackGame::build,    WhackGame::destroy);
    make_card(1, "Sequence", 0x388E3C, !tomb && (int)stg >= (int)pet::LifeStage::Child,
              SequenceGame::build, SequenceGame::destroy);
    make_card(2, "Gacha",    0xC62828, !tomb && (int)stg >= (int)pet::LifeStage::Teen,
              GachaGame::build,    GachaGame::destroy);

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

void PetUi::destroy_page_games(lv_obj_t *root)
{
    if (!root) return;
    lv_obj_del(root);
}

// ---- Page: Shop ------------------------------------------------------------

lv_obj_t *PetUi::build_page_shop(lv_obj_t *parent)
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
        const char *effect;
        int price;
        lv_color_t tint;
        void (*apply)(int amount);
        int amount;
    };
    static const Item items[6] = {
        {"Snack",    "F+10",  8,  {}, [](int a){ Pet::instance().feed_with_amount(a); },   10},
        {"Meal",     "F+25 E+5",  20, {}, [](int a){ Pet::instance().feed(); }, 0},
        {"Feast",    "F+40",  35, {}, [](int a){ Pet::instance().feed_with_amount(a); },   40},
        {"Coffee",   "E+30",  15, {}, [](int a){ Pet::instance().drink_energy(a); },      30},
        {"Energy+",  "E+60",  28, {}, [](int a){ Pet::instance().drink_energy(a); },      60},
        {"Medicine", "He+50", 25, {}, [](int a){ Pet::instance().take_medicine(a); },     50},
    };

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
        lv_obj_set_style_text_color(price, lv_color_hex(0xFFD54F), 0);
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

// ---- Page: Settings --------------------------------------------------------

void PetUi::refresh_settings_status()
{
    auto &self = instance();
    if (!self.settings_.status_label) return;
    app::wifi_status st;
    app::WifiManager::instance().get_status(st);
    const char *state_str = "?";
    switch (st.state) {
        case app::wifi_conn_state::Idle:        state_str = "Idle";        break;
        case app::wifi_conn_state::Scanning:    state_str = "Scanning";    break;
        case app::wifi_conn_state::Connecting:  state_str = "Connecting";  break;
        case app::wifi_conn_state::Connected:   state_str = "Connected";   break;
        case app::wifi_conn_state::Failed:      state_str = "Failed";      break;
        case app::wifi_conn_state::Disconnected: state_str = "Disconnected"; break;
    }
    char buf[96];
    if (st.ssid[0] == 0) {
        snprintf(buf, sizeof(buf), "WiFi: %s", state_str);
    } else {
        snprintf(buf, sizeof(buf), "WiFi: %s  %s  %s",
                 st.ssid, state_str, st.ip);
    }
    lv_label_set_text(self.settings_.status_label, buf);
}

void PetUi::refresh_timeout_label()
{
    auto &self = instance();
    if (!self.settings_.to_label) return;
    pet::ScreenTimeout t = pet::ScreenPower::instance().timeout();
    const char *txt = "?";
    switch (t) {
        case pet::ScreenTimeout::Off:  txt = "Off";    break;
        case pet::ScreenTimeout::Min2: txt = "2 min";  break;
        case pet::ScreenTimeout::Min5: txt = "5 min";  break;
    }
    lv_label_set_text(self.settings_.to_label, txt);
}

void PetUi::rebuild_ap_list()
{
    auto &self = instance();
    if (!self.settings_.list) return;
    lv_obj_clean(self.settings_.list);
    int n = app::WifiManager::instance().scan_count();
    const wifi_ap_record_t *aps = app::WifiManager::instance().scan_results();
    app::wifi_status st;
    app::WifiManager::instance().get_status(st);
    for (int i = 0; i < n; i++) {
        if (aps[i].ssid[0] == 0) continue;
        char label[64];
        bool connected = (strncmp((char *)aps[i].ssid, st.ssid,
                                  sizeof(aps[i].ssid)) == 0);
        const char *lock = (aps[i].authmode == WIFI_AUTH_OPEN) ? "" : "#";
        int rssi = aps[i].rssi;
        int bars;
        if      (rssi >= -55) bars = 4;
        else if (rssi >= -67) bars = 3;
        else if (rssi >= -75) bars = 2;
        else if (rssi >= -82) bars = 1;
        else                  bars = 0;
        char bar_chars[13];
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
        lv_obj_t *btn = lv_list_add_button(self.settings_.list, NULL, label);
        char *ssid_copy = (char *)lv_malloc(sizeof(aps[i].ssid));
        if (!ssid_copy) return;
        strncpy(ssid_copy, (char *)aps[i].ssid, sizeof(aps[i].ssid) - 1);
        ssid_copy[sizeof(aps[i].ssid) - 1] = 0;
        lv_obj_set_user_data(btn, ssid_copy);
        lv_obj_add_event_cb(btn, on_ap_clicked, LV_EVENT_CLICKED, ssid_copy);
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
        lv_list_add_text(self.settings_.list, "(no scan results)");
    }
}

void PetUi::settings_poll_cb(lv_timer_t *t)
{
    (void)t;
    auto &self = instance();
    refresh_settings_status();
    app::wifi_status st;
    app::WifiManager::instance().get_status(st);
    if (self.last_state_ == app::wifi_conn_state::Scanning &&
        st.state != app::wifi_conn_state::Scanning) {
        rebuild_ap_list();
    }
    self.last_state_ = st.state;
}

void PetUi::on_rescan_clicked(lv_event_t *e)
{
    (void)e;
    app::WifiManager::instance().scan_start();
}

void PetUi::on_disconnect_clicked(lv_event_t *e)
{
    (void)e;
    app::WifiManager::instance().disconnect();
    refresh_settings_status();
}

void PetUi::on_forget_clicked(lv_event_t *e)
{
    (void)e;
    app::WifiManager::instance().forget();
    refresh_settings_status();
}

void PetUi::on_lock_clicked(lv_event_t *e)
{
    (void)e;
    pet::ScreenPower::instance().lock_now();
}

void PetUi::on_to_off_clicked(lv_event_t *e)
{
    (void)e;
    pet::ScreenPower::instance().set_timeout(pet::ScreenTimeout::Off);
    refresh_timeout_label();
}

void PetUi::on_to_2min_clicked(lv_event_t *e)
{
    (void)e;
    pet::ScreenPower::instance().set_timeout(pet::ScreenTimeout::Min2);
    refresh_timeout_label();
}

void PetUi::on_to_5min_clicked(lv_event_t *e)
{
    (void)e;
    pet::ScreenPower::instance().set_timeout(pet::ScreenTimeout::Min5);
    refresh_timeout_label();
}

void PetUi::close_pass_popup()
{
    auto &self = instance();
    if (self.settings_.pass_kb) {
        lv_obj_del(self.settings_.pass_kb);
        self.settings_.pass_kb = nullptr;
    }
    if (self.settings_.pass_popup) {
        lv_obj_del(self.settings_.pass_popup);
        self.settings_.pass_popup = nullptr;
        self.settings_.pass_ta    = nullptr;
    }
}

void PetUi::on_connect_clicked(lv_event_t *e)
{
    (void)e;
    auto &self = instance();
    if (!self.settings_.pass_ta) {
        ESP_LOGW("pet_ui", "Connect: ta is null");
        close_pass_popup();
        return;
    }
    const char *pass = lv_textarea_get_text(self.settings_.pass_ta);
    if (!pass) pass = "";
    if (self.settings_.pass_ssid[0] == 0) {
        close_pass_popup();
        return;
    }
    app::WifiManager::instance().connect(self.settings_.pass_ssid, pass);
    close_pass_popup();
    refresh_settings_status();
}

void PetUi::on_ap_clicked(lv_event_t *e)
{
    auto &self = instance();
    char *ssid = (char *)lv_event_get_user_data(e);
    if (!ssid) return;
    strncpy(self.settings_.pass_ssid, ssid, sizeof(self.settings_.pass_ssid) - 1);
    self.settings_.pass_ssid[sizeof(self.settings_.pass_ssid) - 1] = 0;

    lv_obj_t *popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(popup, 280, 130);
    lv_obj_set_pos(popup, 20, 38);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x263238), 0);
    lv_obj_set_style_border_color(popup, lv_color_hex(0x1976D2), 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_radius(popup, 8, 0);
    lv_obj_set_style_pad_all(popup, 8, 0);

    lv_obj_t *t = lv_label_create(popup);
    lv_label_set_text_fmt(t, "Password: %s", ssid);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_t *ta = lv_textarea_create(popup);
    lv_obj_set_size(ta, 230, 32);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, -16, 30);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "password");

    char saved_ssid[33] = {};
    char saved_pass[64] = {};
    if (app::WifiManager::instance().get_saved_credentials(saved_ssid, sizeof(saved_ssid),
                                                saved_pass, sizeof(saved_pass))
        && strcmp(saved_ssid, ssid) == 0
        && saved_pass[0] != 0) {
        lv_textarea_set_text(ta, saved_pass);
    }

    lv_obj_t *show_btn = lv_button_create(popup);
    lv_obj_set_size(show_btn, 32, 32);
    lv_obj_align(show_btn, LV_ALIGN_TOP_RIGHT, -2, 30);
    lv_obj_t *show_lbl = lv_label_create(show_btn);
    lv_label_set_text(show_lbl, "*");
    lv_obj_center(show_lbl);
    lv_obj_add_event_cb(show_btn, [](lv_event_t *ev) {
        lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(ev);
        lv_obj_t *label = (lv_obj_t *)lv_obj_get_child(btn, 0);
        auto &s = instance();
        if (!s.settings_.pass_ta || !label) return;
        bool now_hidden = !lv_textarea_get_password_mode(s.settings_.pass_ta);
        lv_textarea_set_password_mode(s.settings_.pass_ta, now_hidden);
        lv_label_set_text(label, now_hidden ? "*" : "o");
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *kb = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_bg_opa(kb, LV_OPA_90, 0);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_size(kb, 320, 110);
    lv_obj_add_event_cb(kb, [](lv_event_t *ev) {
        lv_obj_t *kbd = (lv_obj_t *)lv_event_get_current_target(ev);
        auto &s = instance();
        if (lv_obj_is_valid(kbd)) lv_obj_del(kbd);
        if (s.settings_.pass_kb == kbd) s.settings_.pass_kb = nullptr;
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

    self.settings_.pass_popup = popup;
    self.settings_.pass_ta    = ta;
    self.settings_.pass_kb    = kb;
}

lv_obj_t *PetUi::build_page_settings(lv_obj_t *parent)
{
    auto &self = instance();

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 208);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    self.settings_.status_label = lv_label_create(root);
    lv_obj_set_style_text_font(self.settings_.status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(self.settings_.status_label, lv_color_white(), 0);
    lv_obj_align(self.settings_.status_label, LV_ALIGN_TOP_LEFT, 4, 2);
    refresh_settings_status();

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

    self.settings_.list = lv_list_create(root);
    lv_obj_set_size(self.settings_.list, 312, 108);
    lv_obj_set_pos(self.settings_.list, 4, 52);
    lv_obj_set_style_bg_color(self.settings_.list, lv_color_hex(0x101010), 0);
    lv_obj_set_style_pad_all(self.settings_.list, 0, 0);
    lv_obj_set_style_border_width(self.settings_.list, 0, 0);
    rebuild_ap_list();

    self.settings_.lock_btn = lv_button_create(root);
    lv_obj_set_size(self.settings_.lock_btn, 70, 24);
    lv_obj_set_pos(self.settings_.lock_btn, 4, 170);
    lv_obj_set_style_bg_color(self.settings_.lock_btn, lv_color_hex(0xC62828), 0);
    lv_obj_add_event_cb(self.settings_.lock_btn, on_lock_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lock_lbl = lv_label_create(self.settings_.lock_btn);
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
    self.settings_.to_off_btn  = make_to_btn("Off", 156, on_to_off_clicked);
    self.settings_.to_2min_btn = make_to_btn("2m",  194, on_to_2min_clicked);
    self.settings_.to_5min_btn = make_to_btn("5m",  232, on_to_5min_clicked);

    self.settings_.to_label = lv_label_create(root);
    lv_obj_set_style_text_font(self.settings_.to_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(self.settings_.to_label, lv_color_white(), 0);
    lv_obj_set_pos(self.settings_.to_label, 274, 176);
    refresh_timeout_label();

    self.settings_poll_ = lv_timer_create(settings_poll_cb, 500, nullptr);

    return root;
}

void PetUi::destroy_page_settings(lv_obj_t *root)
{
    (void)root;
    auto &self = instance();
    if (self.settings_poll_) {
        lv_timer_del(self.settings_poll_);
        self.settings_poll_ = nullptr;
    }
    close_pass_popup();
    self.settings_.status_label = nullptr;
    self.settings_.list         = nullptr;
    self.settings_.lock_btn     = nullptr;
    self.settings_.to_off_btn   = nullptr;
    self.settings_.to_2min_btn  = nullptr;
    self.settings_.to_5min_btn  = nullptr;
    self.settings_.to_label     = nullptr;
}

// ---- Boot ------------------------------------------------------------------

void PetUi::build_ui()
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

    PetPages::instance().register_page(PetPages::Page::Status,   build_page_status,   destroy_page_status);
    PetPages::instance().register_page(PetPages::Page::Games,    build_page_games,    destroy_page_games);
    PetPages::instance().register_page(PetPages::Page::Shop,     build_page_shop,     nullptr);
    PetPages::instance().register_page(PetPages::Page::Settings, build_page_settings, destroy_page_settings);
    pet::ai_usage::AiUsagePage::instance().register_handlers();

    PetPages::instance().build_tabs(screen);
    PetPages::instance().switch_page(PetPages::Page::Status);

    lv_timer_create(on_update_timer, 500, nullptr);
}

esp_err_t PetUi::start_ui()
{
    app::WifiManager::instance().init();

    if (lvgl_port_lock(0)) {
        build_ui();
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock for UI build");
        return ESP_FAIL;
    }

    Pet::instance().init();
    PetSave::instance().init();
    PetSave::instance().load(Pet::instance());

    int streak = PetMeta::instance().record_open_day_and_reward(
        PetMeta::instance().today_epoch_day());
    if (streak >= 1) {
        ESP_LOGI(TAG, "Daily streak active: %d days, +%d coins",
                 streak, streak * 10);
    }

    PetMainTask::instance().start();

    ESP_LOGI(TAG, "Pet UI started");
    return ESP_OK;
}

} // namespace pet
