#pragma once

#include "esp_err.h"
#include "pet_state.h"
#include "pet_pages.h"
#include "pet_frames.h"
#include "app/wifi_manager.h"
#include "app/screen_power.h"
#include "lvgl.h"

namespace pet {

class PetUi {
public:
    static PetUi &instance() noexcept;

    static esp_err_t start_ui();

    // BuildFn / DestroyFn compatible page builders
    static lv_obj_t *build_page_status(lv_obj_t *parent);
    static void      destroy_page_status(lv_obj_t *root);
    static lv_obj_t *build_page_games(lv_obj_t *parent);
    static void      destroy_page_games(lv_obj_t *root);
    static lv_obj_t *build_page_shop(lv_obj_t *parent);
    static lv_obj_t *build_page_settings(lv_obj_t *parent);
    static void      destroy_page_settings(lv_obj_t *root);

private:
    PetUi() = default;

    // Status page widgets
    lv_obj_t *face_img_          = nullptr;
    lv_anim_t face_anim_buf_     = {};
    lv_obj_t *status_label_      = nullptr;
    lv_obj_t *stats_label_       = nullptr;
    lv_obj_t *coins_label_       = nullptr;
    lv_obj_t *wifi_label_        = nullptr;
    lv_obj_t *bars_[4]           = {};
    lv_obj_t *btn_sleep_         = nullptr;

    // Animation state
    pet_anim_state_t current_anim_       = PET_ANIM_COUNT;
    bool             action_anim_active_ = false;
    lv_anim_t        action_anim_buf_    = {};

    // Settings page state
    struct SettingsCtx {
        lv_obj_t *status_label   = nullptr;
        lv_obj_t *list           = nullptr;
        lv_obj_t *lock_btn       = nullptr;
        lv_obj_t *to_off_btn     = nullptr;
        lv_obj_t *to_2min_btn    = nullptr;
        lv_obj_t *to_5min_btn    = nullptr;
        lv_obj_t *to_label       = nullptr;
        lv_obj_t *pass_popup     = nullptr;
        lv_obj_t *pass_ta        = nullptr;
        lv_obj_t *pass_kb        = nullptr;
        char      pass_ssid[33]  = {};
    };
    SettingsCtx settings_{};

    lv_timer_t            *settings_poll_ = nullptr;
    app::wifi_conn_state   last_state_    = static_cast<app::wifi_conn_state>(-1);

    // UI helpers
    static void set_bar_value_with_warn(lv_obj_t *bar, int value);
    static pet_anim_state_t resolve_anim_state(const State &s, bool sleeping);
    static void anim_frame_exec(void *var, int32_t value);
    static void anim_ready_cb(lv_anim_t *a);
    static void switch_to_animation(pet_anim_state_t anim);
    static void start_action_anim(pet_anim_state_t anim, uint16_t repeat_count);
    static void update_ui();
    static void build_ui();

    // Settings helpers
    static void refresh_settings_status();
    static void refresh_timeout_label();
    static void rebuild_ap_list();
    static void close_pass_popup();

    // LVGL callbacks
    static void on_update_timer(lv_timer_t *timer);
    static void btn_feed_cb(lv_event_t *e);
    static void btn_play_cb(lv_event_t *e);
    static void btn_sleep_cb(lv_event_t *e);
    static void btn_pet_cb(lv_event_t *e);
    static void on_card_clicked(lv_event_t *e);
    static void on_card_freed(lv_event_t *e);
    static void on_back_clicked(lv_event_t *e);
    static void settings_poll_cb(lv_timer_t *t);
    static void on_rescan_clicked(lv_event_t *e);
    static void on_disconnect_clicked(lv_event_t *e);
    static void on_forget_clicked(lv_event_t *e);
    static void on_lock_clicked(lv_event_t *e);
    static void on_to_off_clicked(lv_event_t *e);
    static void on_to_2min_clicked(lv_event_t *e);
    static void on_to_5min_clicked(lv_event_t *e);
    static void on_connect_clicked(lv_event_t *e);
    static void on_ap_clicked(lv_event_t *e);
};

} // namespace pet
