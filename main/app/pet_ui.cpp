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

static lv_obj_t *build_page_games(lv_obj_t *parent)
{
    // File-scope state for which mini-game is currently mounted inside the Games page.
    // Survives across Games-page rebuilds so the host container can clean up.
    static lv_obj_t *s_game_host = nullptr;
    static lv_obj_t *s_active_root = nullptr;
    static void (*s_active_destroy)(lv_obj_t *) = nullptr;

    auto mount_game = [&](lv_obj_t *(*builder)(lv_obj_t *), void (*destroy)(lv_obj_t *)) {
        if (s_active_destroy && s_active_root) s_active_destroy(s_active_root);
        s_active_destroy = destroy;
        s_active_root = builder(s_game_host);
    };

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 208);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *title = lv_label_create(root);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Games");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *whack_btn = lv_btn_create(root);
    lv_obj_set_size(whack_btn, 100, 36);
    lv_obj_set_pos(whack_btn, 10, 28);
    lv_obj_set_style_bg_color(whack_btn, lv_color_hex(0x1976D2), 0);
    lv_obj_t *wb = lv_label_create(whack_btn);
    lv_label_set_text(wb, "Whack");
    lv_obj_center(wb);

    int lvl = Pet::instance().get_state().level;
    lv_obj_t *seq_btn = lv_btn_create(root);
    lv_obj_set_size(seq_btn, 100, 36);
    lv_obj_set_pos(seq_btn, 115, 28);
    lv_obj_set_style_bg_color(seq_btn,
        lvl >= 2 ? lv_color_hex(0x388E3C) : lv_color_hex(0x555555), 0);
    lv_obj_t *sb = lv_label_create(seq_btn);
    lv_label_set_text(sb, lvl >= 2 ? "Sequence" : "Sequence (Lv2)");
    lv_obj_center(sb);

    // Gacha button (right column of the top row).
    lv_obj_t *gacha_btn = lv_btn_create(root);
    lv_obj_set_size(gacha_btn, 95, 36);
    lv_obj_set_pos(gacha_btn, 220, 28);
    lv_obj_set_style_bg_color(gacha_btn, lv_color_hex(0xC62828), 0);
    lv_obj_t *gb = lv_label_create(gacha_btn);
    lv_label_set_text(gb, "Gacha");
    lv_obj_center(gb);

    s_game_host = lv_obj_create(root);
    lv_obj_set_size(s_game_host, 320, 138);
    lv_obj_set_pos(s_game_host, 0, 68);
    lv_obj_set_style_bg_color(s_game_host, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_game_host, 0, 0);
    lv_obj_set_style_pad_all(s_game_host, 0, 0);

    // The lambda captures `mount_game` by reference; since mount_game only
    // touches file-scope statics this is safe.
    lv_obj_add_event_cb(whack_btn, [](lv_event_t *e) {
        extern lv_obj_t *(*get_mount_fn())(lv_obj_t *);
        (void)e;
        if (s_active_destroy && s_active_root) s_active_destroy(s_active_root);
        s_active_destroy = pet::game_whack::destroy;
        s_active_root = pet::game_whack::build(s_game_host);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(seq_btn, [](lv_event_t *e) {
        (void)e;
        if (Pet::instance().get_state().level < 2) return;
        if (s_active_destroy && s_active_root) s_active_destroy(s_active_root);
        s_active_destroy = pet::game_sequence::destroy;
        s_active_root = pet::game_sequence::build(s_game_host);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(gacha_btn, [](lv_event_t *e) {
        (void)e;
        if (s_active_destroy && s_active_root) s_active_destroy(s_active_root);
        s_active_destroy = pet::game_gacha::destroy;
        s_active_root = pet::game_gacha::build(s_game_host);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(root, [](lv_event_t *e) {
        (void)e;
        if (s_active_destroy && s_active_root) s_active_destroy(s_active_root);
        s_active_destroy = nullptr;
        s_active_root = nullptr;
        s_game_host = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    return root;
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
    pet::pages::register_page(pet::pages::Page::Games,  build_page_games,  nullptr);
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