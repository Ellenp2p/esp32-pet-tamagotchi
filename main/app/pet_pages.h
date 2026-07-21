#pragma once

#include "lvgl.h"

namespace pet {
namespace pages {

// Logical pages the pet UI knows about. AIUsage is always present in the
// enum (so the build/destroy handlers have a stable slot) but is hidden
// from the tab bar when no AI API keys are configured.
enum class Page {
    Status = 0,
    Games,
    Shop,
    Settings,
    AIUsage,
    // Sentinel — not a real page. Use page_count() instead.
    Count,
};

// Number of tabs the tab bar should render. 4 by default; 5 when the AI
// Usage feature is enabled.
int  page_count();

// True when at least one AI API key is configured and the worker is
// running. The AIUsage tab is drawn only when this is true.
bool ai_usage_enabled();

// Flip the runtime flag. Called from AiUsagePage::register_handlers
// after deciding whether to mount the page.
void set_ai_usage_enabled(bool on);

// Build the shared tab bar at the bottom of the screen. Must be called once
// after build_ui() and before switch_page(). Lives on the screen object.
void build_tabs(lv_obj_t *screen);

// Switches the visible page. Destroys the current page widget tree and rebuilds
// the requested one. Must be called with lvgl_port_lock(0) held.
void switch_page(Page page);

// Returns the currently active page.
Page current_page();

// Convenience: each page exposes a build function for its content area.
// Caller owns the lvgl_port_lock for the duration.
using BuildFn = lv_obj_t *(*)(lv_obj_t *parent);
using DestroyFn = void (*)(lv_obj_t *page_root);

void register_page(Page page, BuildFn build, DestroyFn destroy);

}  // namespace pages
}  // namespace pet