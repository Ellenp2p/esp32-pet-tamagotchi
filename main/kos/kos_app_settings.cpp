#include "kos_app_settings.h"
#include "kos_app_registry.h"
#include "lvgl.h"
#include "esp_log.h"

namespace kos {

static const char *TAG __attribute__((unused)) = "app_settings";
static AppManifest s_manifest = {"settings", "About", "0.3.0", 0x37474F, 0};

const AppManifest &AppSettings::manifest() { return s_manifest; }

AppSettings::~AppSettings() {}

void AppSettings::on_start(AppContext &ctx)
{
    (void)ctx;
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    lv_obj_t *t = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_label_set_text(t, "KOS v0.3.0\nESP32-S3 Pet\nNimBLE+LVGL 8.3\nFS:BSD Pet Project");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *sub = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0xBBBBBB), 0);
    lv_label_set_text(sub, "Apps installed:\n- Launcher\n- Pet\n- Gacha\n- Settings");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 110);

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

void AppSettings::on_stop()
{
    lv_obj_clean(lv_scr_act());
}

}  // namespace kos

namespace kos {
static AppSettings _kos_app_inst_kSettingsInst;
const registry::internals::Entry _kos_app_entry_kSettingsInst = {
    "settings", static_cast<App *>(&_kos_app_inst_kSettingsInst),
};
registry::internals::StaticRegistrar
    _kos_app_regar_kSettingsInst(_kos_app_entry_kSettingsInst);
}  // namespace kos
