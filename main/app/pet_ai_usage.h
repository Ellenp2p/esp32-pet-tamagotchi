#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

namespace pet {
namespace ai_usage {

// v0.6.7-fix: unified usage bar. Each provider exposes two of these
// (long-term + short-term window). Field semantics:
//   used      = consumed units; -1 = unknown
//   limit     = total units;     -1 = unknown
//   remaining = units left until reset; -1 = unknown
//   used_pct  = percentage used (0..100)
//   reset_iso = ISO timestamp of next reset (full); reset_human is
//               the same value formatted in local-time "MM-DD HH:MM"
//               for the UI.
//   ok        = this single bar has data
struct ProviderBar {
    char    label[16]  = {0};
    int64_t used       = -1;
    int64_t limit      = -1;
    int64_t remaining  = -1;
    int     used_pct   = -1;
    char    reset_iso[32] = {0};
    char    reset_human[24] = {0};
    bool    ok         = false;
};

struct KimiUsage {
    ProviderBar weekly;      // usage.{limit,remaining}    (long window)
    ProviderBar five_hours;  // limits[0].detail.*         (5h sliding)
    char err[48]   = {0};
    bool any_ok    = false;
};

struct MiniMaxUsage {
    ProviderBar weekly;      // current_weekly_remaining_percent
    ProviderBar five_hours;  // current_interval_remaining_percent
    char err[48]   = {0};
    bool any_ok    = false;
};

// Snapshot owned by the worker; UI copies it under mutex.
struct Snapshot {
    KimiUsage    kimi;
    MiniMaxUsage minimax;
    int64_t      last_success_ms = 0;
    bool         refreshing      = false;
};

bool enabled();
void start();
void get_snapshot(Snapshot *out);
void request_refresh();

lv_obj_t *build_page(lv_obj_t *parent);
void      destroy_page(lv_obj_t *root);
void      register_page_handlers();

}  // namespace ai_usage
}  // namespace pet