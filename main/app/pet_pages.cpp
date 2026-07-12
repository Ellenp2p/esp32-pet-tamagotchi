#include "pet_pages.h"
#include "esp_log.h"

namespace pet {
namespace pages {

static const char *TAG = "pet_pages";

static const char *kTabLabels[(int)Page::Count] = {"Status", "Games", "Shop", "About"};

struct PageHandlers {
    BuildFn build = nullptr;
    DestroyFn destroy = nullptr;
    lv_obj_t *root = nullptr;  // current page widget tree (cleared on switch)
};

static PageHandlers s_handlers[(int)Page::Count];
static Page s_current = Page::Status;
static lv_obj_t *s_tabs[(int)Page::Count] = {nullptr};
static lv_obj_t *s_content = nullptr;

void register_page(Page page, BuildFn build, DestroyFn destroy)
{
    int idx = (int)page;
    if (idx < 0 || idx >= (int)Page::Count) return;
    s_handlers[idx].build = build;
    s_handlers[idx].destroy = destroy;
}

static void style_active_tab(lv_obj_t *tab, bool active)
{
    if (active) {
        lv_obj_set_style_bg_color(tab, lv_color_hex(0x1976D2), 0);  // blue
    } else {
        lv_obj_set_style_bg_color(tab, lv_color_hex(0x37474F), 0);  // dark gray
    }
}

static void tab_event_cb(lv_event_t *e)
{
    Page target = (Page)(intptr_t)lv_event_get_user_data(e);
    if (target == s_current) return;
    switch_page(target);
}

void build_tabs(lv_obj_t *screen)
{
    // Content area sits above the tab bar.
    s_content = lv_obj_create(screen);
    lv_obj_set_size(s_content, 320, 208);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_style_bg_color(s_content, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);

    // Tab bar at the bottom: 4 buttons of 80x32.
    for (int i = 0; i < (int)Page::Count; i++) {
        lv_obj_t *btn = lv_btn_create(screen);
        lv_obj_set_size(btn, 80, 32);
        lv_obj_set_pos(btn, i * 80, 208);
        lv_obj_add_event_cb(btn, tab_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        style_active_tab(btn, i == (int)s_current);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, kTabLabels[i]);
        lv_obj_center(label);
        s_tabs[i] = btn;
    }
}

static void highlight_tab(Page page)
{
    for (int i = 0; i < (int)Page::Count; i++) {
        style_active_tab(s_tabs[i], i == (int)page);
    }
}

void switch_page(Page page)
{
    int idx = (int)page;
    if (idx < 0 || idx >= (int)Page::Count) return;
    if (!s_handlers[idx].build) {
        ESP_LOGW(TAG, "Page %d has no build handler", idx);
        return;
    }

    // Tear down current page.
    if (s_handlers[(int)s_current].destroy && s_handlers[(int)s_current].root) {
        s_handlers[(int)s_current].destroy(s_handlers[(int)s_current].root);
    } else if (s_handlers[(int)s_current].root) {
        lv_obj_del(s_handlers[(int)s_current].root);
    }
    s_handlers[(int)s_current].root = nullptr;

    // Build new page as child of content container.
    s_current = page;
    s_handlers[idx].root = s_handlers[idx].build(s_content);
    highlight_tab(page);
    ESP_LOGI(TAG, "Switched to page %d", idx);
}

Page current_page() { return s_current; }

}  // namespace pages
}  // namespace pet