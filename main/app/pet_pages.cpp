#include "pet_pages.h"
#include "esp_log.h"

namespace pet {

static const char *TAG = "pet_pages";
static const char *kTabLabels[] = {"Status", "Games", "Shop", "Settings", "AI"};

static void style_active_tab(lv_obj_t *tab, bool active)
{
    if (active) {
        lv_obj_set_style_bg_color(tab, lv_color_hex(0x1976D2), 0);
    } else {
        lv_obj_set_style_bg_color(tab, lv_color_hex(0x37474F), 0);
    }
}

PetPages &PetPages::instance() noexcept
{
    static PetPages s;
    return s;
}

bool PetPages::ai_usage_enabled() noexcept { return ai_enabled_; }
void PetPages::set_ai_usage_enabled(bool on) noexcept { ai_enabled_ = on; }
int  PetPages::page_count() noexcept { return ai_enabled_ ? 5 : 4; }
PetPages::Page PetPages::current_page() noexcept { return current_; }

void PetPages::register_page(Page page, BuildFn build, DestroyFn destroy) noexcept
{
    int idx = (int)page;
    if (idx < 0 || idx >= (int)Page::Count) return;
    handlers_[idx].build = build;
    handlers_[idx].destroy = destroy;
}

void PetPages::tab_event_cb(lv_event_t *e) noexcept
{
    Page target = (Page)(intptr_t)lv_event_get_user_data(e);
    if (target == instance().current_) return;
    instance().switch_page(target);
}

void PetPages::build_tabs(lv_obj_t *screen) noexcept
{
    content_ = lv_obj_create(screen);
    lv_obj_set_size(content_, 320, 208);
    lv_obj_set_pos(content_, 0, 0);
    lv_obj_set_style_bg_color(content_, lv_color_black(), 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 0, 0);

    int n = page_count();
    int btn_w = 320 / n;
    for (int i = 0; i < n; i++) {
        lv_obj_t *btn = lv_button_create(screen);
        lv_obj_set_size(btn, btn_w, 32);
        lv_obj_set_pos(btn, i * btn_w, 208);
        lv_obj_add_event_cb(btn, tab_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        style_active_tab(btn, i == (int)current_);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, kTabLabels[i]);
        lv_obj_center(label);
        tabs_[i] = btn;
    }
}

void PetPages::highlight_tab(Page page) noexcept
{
    int n = page_count();
    for (int i = 0; i < n; i++) {
        style_active_tab(tabs_[i], i == (int)page);
    }
}

void PetPages::switch_page(Page page) noexcept
{
    int idx = (int)page;
    if (idx < 0 || idx >= (int)Page::Count) return;
    if (!handlers_[idx].build) {
        ESP_LOGW(TAG, "Page %d has no build handler", idx);
        return;
    }

    if (handlers_[(int)current_].root) {
        lv_obj_t *root = handlers_[(int)current_].root;
        if (handlers_[(int)current_].destroy) {
            handlers_[(int)current_].destroy(root);
        }
        if (lv_obj_is_valid(root)) {
            lv_obj_del(root);
        }
        handlers_[(int)current_].root = nullptr;
    }

    current_ = page;
    handlers_[idx].root = handlers_[idx].build(content_);
    highlight_tab(page);
    ESP_LOGI(TAG, "Switched to page %d", idx);
}

} // namespace pet
