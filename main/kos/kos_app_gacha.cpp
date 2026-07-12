#include "kos_app_gacha.h"
#include "app/pet_game_gacha.h"
#include "kos_app_registry.h"
#include "lvgl.h"
#include "esp_log.h"

namespace kos {

static const char *TAG __attribute__((unused)) = "app_gacha";
static AppManifest s_manifest = {"gacha", "Gacha", "0.3.0", 0xC62828, 64};

const AppManifest &AppGacha::manifest() { return s_manifest; }

AppGacha::~AppGacha() {}

void AppGacha::on_start(AppContext &ctx)
{
    (void)ctx;
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    gacha_root_ = pet::game_gacha::build(lv_scr_act());

    // Home button on top.
    lv_obj_t *home = lv_btn_create(lv_scr_act());
    lv_obj_set_size(home, 36, 22);
    lv_obj_align(home, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_set_style_bg_color(home, lv_color_hex(0x37474F), 0);
    lv_obj_t *lbl = lv_label_create(home);
    lv_label_set_text(lbl, "Home");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(home, [](lv_event_t *e) {
        (void)e;
        registry::launch("launcher");
    }, LV_EVENT_CLICKED, nullptr);
}

void AppGacha::on_stop()
{
    if (gacha_root_) {
        pet::game_gacha::destroy(gacha_root_);
        gacha_root_ = nullptr;
    }
    lv_obj_clean(lv_scr_act());
}

}  // namespace kos

namespace kos {
static AppGacha _kos_app_inst_kGachaInst;
const registry::internals::Entry _kos_app_entry_kGachaInst = {
    "gacha", static_cast<App *>(&_kos_app_inst_kGachaInst),
};
registry::internals::StaticRegistrar
    _kos_app_regar_kGachaInst(_kos_app_entry_kGachaInst);
}  // namespace kos
