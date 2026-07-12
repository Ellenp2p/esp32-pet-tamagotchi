#include "kos_app_pet.h"
#include "kos_app_registry.h"
#include "app/pet_state.h"
#include "app/pet_save.h"
#include "app/pet_idle_events.h"
#include "bsp/bsp_qmi8658.h"
#include "lvgl.h"
#include "esp_log.h"

namespace kos {

static const char *TAG = "app_pet";
static AppManifest s_manifest = {"pet", "Pet", "0.3.0", 0x43A047, 256};

const AppManifest &AppPet::manifest() { return s_manifest; }

static const char *kBarNames[4] = {"Fullness", "Happy", "Energy", "Health"};

static const char *face_text(const pet::State &s, bool sleeping)
{
    if (sleeping) return "(-_-)zzZ";
    if (s.health < 30) return "(x_x)";
    if (s.fullness < 20) return "(>_<)";
    if (s.happiness > 80 && s.energy > 50) return "(^o^)";
    if (s.happiness < 30) return "(T_T)";
    if (s.energy < 30) return "(-.-)";
    return "(^_^)";
}

AppPet::~AppPet() {}

void AppPet::set_bar_value_with_warn(lv_obj_t *bar, int value)
{
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_color_t color;
    if (value < 10)      color = lv_color_hex(0xE53935);
    else if (value < 25) color = lv_color_hex(0xFB8C00);
    else                 color = lv_color_hex(0x43A047);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
}

void AppPet::refresh_ui()
{
    if (!face_label_) return;
    pet::State s = pet::Pet::instance().get_state();
    bool sleeping = pet::Pet::instance().is_sleeping();

    set_bar_value_with_warn(bars_[0], s.fullness);
    set_bar_value_with_warn(bars_[1], s.happiness);
    set_bar_value_with_warn(bars_[2], s.energy);
    set_bar_value_with_warn(bars_[3], s.health);

    lv_label_set_text(face_label_, face_text(s, sleeping));
    char buf[96];
    snprintf(buf, sizeof(buf),
             "%s | Lv%d | F:%d Ha:%d E:%d He:%d | Coins:%d",
             sleeping ? "Sleep" : "Awake", s.level,
             s.fullness, s.happiness, s.energy, s.health, s.coins);
    lv_label_set_text(status_label_, buf);
    if (btn_sleep_) {
        lv_obj_t *lbl = lv_obj_get_child(btn_sleep_, 0);
        if (lbl) lv_label_set_text(lbl, sleeping ? "Wake" : "Sleep");
    }
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text,
                          lv_event_cb_t cb, lv_align_t a, int x)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 70, 28);
    lv_obj_align(b, a, x, -8);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return b;
}

static void on_feed(lv_event_t *e)  { pet::Pet::instance().feed(); }
static void on_play(lv_event_t *e)  { pet::Pet::instance().play(); }
static void on_sleep(lv_event_t *e)
{
    if (pet::Pet::instance().is_sleeping()) pet::Pet::instance().wake_up();
    else                                    pet::Pet::instance().sleep();
}
static void on_pet(lv_event_t *e)   { pet::Pet::instance().pet(); }

void AppPet::on_start(AppContext &ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "Pet App: on_start");

    // 1-time init for state persistence + idle events. Idempotent.
    pet::save::init();
    pet::save::load(pet::Pet::instance());
    pet::idle_events::init();
    pet::Pet::instance().init();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_clean(scr);

    face_label_ = lv_label_create(scr);
    lv_obj_set_style_text_font(face_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(face_label_, lv_color_white(), 0);
    lv_label_set_text(face_label_, "(^_^)");
    lv_obj_align(face_label_, LV_ALIGN_TOP_MID, 0, 4);

    status_label_ = lv_label_create(scr);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_white(), 0);
    lv_label_set_text(status_label_, "Initializing...");
    lv_obj_align(status_label_, LV_ALIGN_TOP_MID, 0, 32);

    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = lv_obj_create(scr);
        lv_obj_set_size(c, 300, 18);
        lv_obj_align(c, LV_ALIGN_TOP_LEFT, 10, 56 + i * 20);
        lv_obj_set_style_pad_all(c, 2, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(c, 0, 0);

        lv_obj_t *n = lv_label_create(c);
        lv_label_set_text(n, kBarNames[i]);
        lv_obj_set_style_text_font(n, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(n, lv_color_white(), 0);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 4, 0);

        bars_[i] = lv_bar_create(c);
        lv_obj_set_size(bars_[i], 200, 10);
        lv_obj_align(bars_[i], LV_ALIGN_RIGHT_MID, -4, 0);
        lv_bar_set_range(bars_[i], 0, 100);
        lv_bar_set_value(bars_[i], 50, LV_ANIM_OFF);
    }

    make_btn(scr, "Feed",  on_feed,  LV_ALIGN_BOTTOM_MID, -117);
    make_btn(scr, "Play",  on_play,  LV_ALIGN_BOTTOM_MID, -39);
    btn_sleep_ = make_btn(scr, "Sleep", on_sleep, LV_ALIGN_BOTTOM_MID, 39);
    make_btn(scr, "Pet",   on_pet,   LV_ALIGN_BOTTOM_MID, 117);

    // Home button: top-left corner so the user can return to Launcher.
    lv_obj_t *home = lv_btn_create(scr);
    lv_obj_set_size(home, 36, 22);
    lv_obj_align(home, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_set_style_bg_color(home, lv_color_hex(0x37474F), 0);
    lv_obj_t *hl = lv_label_create(home);
    lv_label_set_text(hl, "Home");
    lv_obj_center(hl);
    lv_obj_add_event_cb(home, [](lv_event_t *e) {
        (void)e;
        registry::launch("launcher");
    }, LV_EVENT_CLICKED, nullptr);

    refresh_ui();
    started_ = true;
}

void AppPet::on_pause() {}

void AppPet::on_stop()
{
    ESP_LOGI(TAG, "Pet App: on_stop");
    face_label_ = nullptr;
    status_label_ = nullptr;
    btn_sleep_ = nullptr;
    for (int i = 0; i < 4; i++) bars_[i] = nullptr;
    started_ = false;
    lv_obj_clean(lv_scr_act());
}

void AppPet::on_tick(uint32_t now_ms)
{
    if (!started_) return;
    (void)now_ms;

    pet::Pet::instance().update();
    pet::idle_events::tick(pet::Pet::instance().get_state().age_ticks);
    if (pet::Pet::instance().is_dirty()) {
        pet::save::save_if_dirty(pet::Pet::instance(), false);
    }

    // IMU shake → play (or wake if sleeping).
    bsp::QMI8658_Data d;
    if (bsp::QMI8658::instance().read(&d) == ESP_OK) {
        if (bsp::QMI8658::instance().detect_shake(d)) {
            if (!pet::Pet::instance().is_sleeping()) {
                pet::Pet::instance().play();
            } else {
                pet::Pet::instance().wake_up();
            }
        }
    }

    refresh_ui();
}

}  // namespace kos

// 不能用 KOS_APP_DEFINE 宏(Paste + namespace kos 嵌套有 bug)—— 直接手写
// 与宏等价的代码即可。
namespace kos {
static AppPet _kos_app_inst_kPetInst;
const registry::internals::Entry _kos_app_entry_kPetInst = {
    "pet", static_cast<App *>(&_kos_app_inst_kPetInst),
};
registry::internals::StaticRegistrar
    _kos_app_regar_kPetInst(_kos_app_entry_kPetInst);
}  // namespace kos
