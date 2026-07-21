#pragma once

#include "lvgl.h"

namespace pet {

class PetPages {
public:
    enum class Page : int {
        Status = 0,
        Games,
        Shop,
        Settings,
        AIUsage,
        Count,
    };

    using BuildFn   = lv_obj_t *(*)(lv_obj_t *parent);
    using DestroyFn = void (*)(lv_obj_t *page_root);

    static PetPages &instance() noexcept;

    int   page_count() noexcept;
    bool  ai_usage_enabled() noexcept;
    void  set_ai_usage_enabled(bool on) noexcept;
    void  build_tabs(lv_obj_t *screen) noexcept;
    void  switch_page(Page page) noexcept;
    Page  current_page() noexcept;
    void  register_page(Page page, BuildFn build, DestroyFn destroy) noexcept;

private:
    PetPages() = default;

    struct PageHandlers {
        BuildFn   build   = nullptr;
        DestroyFn destroy = nullptr;
        lv_obj_t *root    = nullptr;
    };

    void highlight_tab(Page page) noexcept;
    static void tab_event_cb(lv_event_t *e) noexcept;

    PageHandlers handlers_[(int)Page::Count]{};
    Page         current_       = Page::Status;
    lv_obj_t    *tabs_[(int)Page::Count]  = {};
    lv_obj_t    *content_       = nullptr;
    bool         ai_enabled_    = false;
};

} // namespace pet
