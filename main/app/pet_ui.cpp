#include "pet_ui.h"
#include "pet_state.h"
#include "pet_save.h"
#include "pet_pages.h"
#include "pet_game_whack.h"
#include "pet_game_sequence.h"
#include "pet_game_gacha.h"
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
static lv_obj_t *face_label_ = nullptr;
static lv_obj_t *status_label_ = nullptr;
static lv_obj_t *bars_[4] = {nullptr};
static lv_obj_t *btn_sleep_ = nullptr;

static const char *BAR_NAMES[4] = {"Fullness", "Happy", "Energy", "Health"};

// Color thresholds for status bars. <25 = orange, <10 = red, else green.
static void set_bar_value_with_warn(lv_obj_t *bar, int value)
{
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_color_t color;
    if (value < 10)      color = lv_color_hex(0xE53935); // red
    else if (value < 25) color = lv_color_hex(0xFB8C00); // orange
    else                 color = lv_color_hex(0x43A047); // green
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
}

static const char *get_face_text(const State &s, bool sleeping)
{
    if (sleeping) return "(-_-)zzZ";
    if (s.health < 30) return "(x_x)";
    if (s.fullness < 20) return "(>_<)";
    if (s.happiness > 80 && s.energy > 50) return "(^o^)";
    if (s.happiness < 30) return "(T_T)";
    if (s.energy < 30) return "(-.-)";
    return "(^_^)";
}

// ---------------- Update loop ------------------------------------------------
static void update_ui()
{
    if (!face_label_) return;  // status page not built yet

    State s = Pet::instance().get_state();
    bool sleeping = Pet::instance().is_sleeping();

    set_bar_value_with_warn(bars_[0], s.fullness);
    set_bar_value_with_warn(bars_[1], s.happiness);
    set_bar_value_with_warn(bars_[2], s.energy);
    set_bar_value_with_warn(bars_[3], s.health);

    lv_label_set_text(face_label_, get_face_text(s, sleeping));

    char status[96];
    snprintf(status, sizeof(status), "%s | Lv%d | F:%d Ha:%d E:%d He:%d | Coins:%d",
             sleeping ? "Sleeping" : "Awake",
             s.level, s.fullness, s.happiness, s.energy, s.health, s.coins);
    lv_label_set_text(status_label_, status);

    lv_label_set_text(lv_obj_get_child(btn_sleep_, 0), sleeping ? "Wake" : "Sleep");
}

static void on_update_timer(lv_timer_t *timer)
{
    update_ui();
}

// ---------------- Action button callbacks -----------------------------------
static void btn_feed_cb(lv_event_t *e)  { Pet::instance().feed(); }
static void btn_play_cb(lv_event_t *e)  { Pet::instance().play(); }
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

    face_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(face_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(face_label_, lv_color_white(), 0);
    lv_label_set_text(face_label_, "(^_^)");
    lv_obj_align(face_label_, LV_ALIGN_TOP_MID, 0, 4);

    status_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_white(), 0);
    lv_label_set_text(status_label_, "Initializing...");
    lv_obj_align(status_label_, LV_ALIGN_TOP_MID, 0, 32);

    // 4 bars stacked tightly to leave room for action buttons at bottom.
    for (int i = 0; i < 4; i++) {
        lv_obj_t *container = lv_obj_create(root);
        lv_obj_set_size(container, 300, 18);
        lv_obj_align(container, LV_ALIGN_TOP_LEFT, 10, 56 + i * 20);
        lv_obj_set_style_pad_all(container, 2, 0);
        lv_obj_set_style_bg_color(container, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(container, 0, 0);

        lv_obj_t *name = lv_label_create(container);
        lv_label_set_text(name, BAR_NAMES[i]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 4, 0);

        bars_[i] = lv_bar_create(container);
        lv_obj_set_size(bars_[i], 200, 10);
        lv_obj_align(bars_[i], LV_ALIGN_RIGHT_MID, -4, 0);
        lv_bar_set_range(bars_[i], 0, 100);
        lv_bar_set_value(bars_[i], 50, LV_ANIM_OFF);
    }

    // Action buttons row at bottom of the status page.
    auto make_btn = [&](const char *text, lv_align_t align, int x, lv_event_cb_t cb) {
        lv_obj_t *btn = lv_btn_create(root);
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
    face_label_ = nullptr;
    status_label_ = nullptr;
    btn_sleep_ = nullptr;
    for (int i = 0; i < 4; i++) bars_[i] = nullptr;
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
    // The DELETE event carries the user_data we registered on the card button,
    // which is the CardCb* itself.
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
    // Games page has two states:
    //   - picker: 3 game cards filling the 320x208 content area.
    //   - play:   one game occupies the full area; a "Back" button in the
    //             top-left corner returns to the picker.

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

    // ---- Picker (3 large cards, full-screen) ----
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
        lv_obj_t *card = lv_btn_create(gctx->picker);
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

    // ---- Back overlay (hidden until play state) ----
    gctx->back_btn = lv_btn_create(root);
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
    // The DELETE handler on root frees the GamesCtx. Nothing to do here.
    (void)root;
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
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    // 4 items, 2 rows of 2 cards. Each card shows name + price.
    struct Item { const char *name; int amount; int price; };
    static const Item items[4] = {
        {"Snack",   10, 10},
        {"Meal",    25, 22},
        {"Treat",   15, 18},
        {"Feast",   50, 40},
    };
    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        lv_obj_t *card = lv_btn_create(root);
        lv_obj_set_size(card, 140, 70);
        lv_obj_set_pos(card, 15 + col * 150, 36 + row * 80);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x37474F), 0);

        lv_obj_t *name = lv_label_create(card);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_label_set_text(name, items[i].name);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 6);

        lv_obj_t *stat = lv_label_create(card);
        lv_obj_set_style_text_font(stat, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(stat, lv_color_hex(0xBBBBBB), 0);
        char buf[32];
        snprintf(buf, sizeof(buf), "F+%d  %d coins", items[i].amount, items[i].price);
        lv_label_set_text(stat, buf);
        lv_obj_align(stat, LV_ALIGN_BOTTOM_MID, 0, -8);

        // Use user_data to identify which item this card buys.
        lv_obj_add_event_cb(card, [](lv_event_t *e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            const Item *it = &items[idx];
            if (Pet::instance().spend_coins(it->price)) {
                Pet::instance().feed_with_amount(it->amount);
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
        "ESP32-S3 Pet v0.2.0\nLVGL 8.3.0 + NimBLE\nFS:BSD Pet Project");
}

// ---------------- Boot --------------------------------------------------------
static void build_ui()
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

    // Register page builders.
    pet::pages::register_page(pet::pages::Page::Status, build_page_status, destroy_page_status);
    pet::pages::register_page(pet::pages::Page::Games,  build_page_games,  destroy_page_games);
    pet::pages::register_page(pet::pages::Page::Shop,   build_page_shop,   nullptr);
    pet::pages::register_page(pet::pages::Page::About,  build_page_about,  nullptr);

    // Tab bar + initial page.
    pet::pages::build_tabs(screen);
    pet::pages::switch_page(pet::pages::Page::Status);

    // Update UI timer in LVGL task context.
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
            if (++save_counter >= 50) {  // 50 * 100ms = 5s
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