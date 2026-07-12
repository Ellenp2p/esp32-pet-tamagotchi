#include "kos_launcher.h"
#include "kos_app_registry.h"
#include "kos_display.h"
#include "lvgl.h"
#include "esp_log.h"

// KOS auto-registration needs `static Klass _kos_inst_<INST>` to find Klass.
// The macro is invoked at global scope, but the launcher definition is
// nested inside namespace kos. To make Klass resolvable, we forward-declare
// it at global scope with the full namespace qualifier first.
//
// 简化方案:启动"使用一个 launcher wrapper 文件",把宏调用整体放在 namespace 内
// —— 即 launcher.cpp 顶层就是 `namespace kos { ... }`,文件末尾宏调用也在内。

namespace kos {

static const char *TAG = "launcher";
static AppManifest s_manifest = {
    "launcher",
    "Launcher",
    "0.3.0",
    0x1976D2,  // blue
    0,
};

const AppManifest &AppLauncher::manifest() { return s_manifest; }

static void tile_event_cb(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    if (id) registry::launch(id);
}

static void install_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Install via BLE — not yet implemented");
}

void AppLauncher::on_start(AppContext &ctx)
{
    ctx_ = ctx;
    root_ = ctx.screen_root;
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_clean(root_);

    lv_obj_t *title = lv_label_create(root_);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "KOS Launcher");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    const int cols = 2, rows = 2;
    const int tile_w = 145, tile_h = 70;
    const int start_x = 10, start_y = 36;
    const int gap_x = 10, gap_y = 8;

    int n = registry::count();
    int idx = 0;
    for (int i = 0; i < n && idx < 4; i++) {
        const AppManifest &m = registry::app(i).manifest();
        if (strcmp(m.id, "launcher") == 0) continue;

        int col = idx % cols;
        int row = idx / cols;
        lv_obj_t *tile = lv_btn_create(root_);
        lv_obj_set_size(tile, tile_w, tile_h);
        lv_obj_set_pos(tile, start_x + col * (tile_w + gap_x),
                       start_y + row * (tile_h + gap_y));
        lv_obj_set_style_bg_color(tile, lv_color_hex(m.icon_color), 0);

        lv_obj_t *name = lv_label_create(tile);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_label_set_text(name, m.name);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t *sub = lv_label_create(tile);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(sub, lv_color_hex(0xDDDDDD), 0);
        lv_label_set_text(sub, m.version);
        lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -8);

        lv_obj_add_event_cb(tile, tile_event_cb, LV_EVENT_CLICKED,
                            (void *)m.id);
        idx++;
    }

    lv_obj_t *install = lv_btn_create(root_);
    lv_obj_set_size(install, 300, 28);
    lv_obj_set_pos(install, 10, 184);
    lv_obj_set_style_bg_color(install, lv_color_hex(0x37474F), 0);
    lv_obj_t *it_lbl = lv_label_create(install);
    lv_obj_set_style_text_font(it_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(it_lbl, lv_color_white(), 0);
    lv_label_set_text(it_lbl, "Install App via BLE");
    lv_obj_center(it_lbl);
    lv_obj_add_event_cb(install, install_event_cb, LV_EVENT_CLICKED, nullptr);
}

void AppLauncher::on_stop()
{
    if (root_) {
        lv_obj_clean(root_);
        root_ = nullptr;
    }
}

// 静态实例 + 自动注册。macro 在 namespace 内展开,static Klass 指 kos::AppLauncher。
static AppLauncher _kos_app_inst_kLauncherInst;
const registry::internals::Entry _kos_app_entry_kLauncherInst = {
    "launcher", static_cast<App *>(&_kos_app_inst_kLauncherInst),
};
registry::internals::StaticRegistrar
    _kos_app_regar_kLauncherInst(_kos_app_entry_kLauncherInst);

}  // namespace kos
