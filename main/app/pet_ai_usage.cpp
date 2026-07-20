#include "pet_ai_usage.h"
#include "pet_pages.h"
#include "wifi_manager.h"
#include "util/HttpClient.h"
#include "util/clock_util.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <mutex>

namespace pet {
namespace ai_usage {

static const char *TAG = "ai_usage";

using clock_util::format_iso_utc_to_cn;
using clock_util::format_epoch_ms_utc_to_cn;

// 5 minutes between polls. v0.6.7-fix: 12 KB task stack — mbedTLS cert
// verification during the HTTPS handshake needs ~4 KB of working
// memory on top of the HTTP parser + cJSON tree, which overflowed the
// previous 6 KB stack and triggered a TASK stack-overflow abort.
static constexpr int POLL_PERIOD_MS    = 5 * 60 * 1000;
static constexpr int RX_BUF           = 2048;
static constexpr int TASK_STACK_BYTES = 12288;

static std::mutex s_mtx;
static Snapshot  s_snap;
static TaskHandle_t s_worker_task = nullptr;  // for v0.6.7 manual refresh

// ---------- key resolution: Kconfig then NVS escape hatch ---------------
// Kconfig wins because that's what the user fills via menuconfig; the NVS
// `pet_ai_secrets` namespace exists so an OTA / provisioning script can
// inject keys without rebuilding.
static const char *kNsSecrets  = "pet_ai_secrets";
static const char *kKeyKimi    = "kimi_key";
static const char *kKeyMinimax = "minimax_key";

static char s_kimi_key[160];
static char s_minimax_key[160];

static void read_nvs_key(const char *ns, const char *key, char *out, size_t out_sz)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) { out[0] = 0; return; }
    size_t sz = out_sz;
    if (nvs_get_str(h, key, out, &sz) != ESP_OK) out[0] = 0;
    nvs_close(h);
}

static bool has_key(const char *k) { return k && k[0] != 0; }

bool enabled()
{
    // Kconfig is the canonical source at compile time.
    bool kc_kimi    = has_key(CONFIG_PET_AI_USAGE_KIMI_KEY);
    bool kc_minimax = has_key(CONFIG_PET_AI_USAGE_MINIMAX_KEY);
    // NVS escape hatch (filled at runtime, e.g. by an OTA script).
    if (!kc_kimi || !kc_minimax) {
        read_nvs_key(kNsSecrets, kKeyKimi,    s_kimi_key,    sizeof(s_kimi_key));
        read_nvs_key(kNsSecrets, kKeyMinimax, s_minimax_key, sizeof(s_minimax_key));
    }
    bool nv_kimi    = has_key(s_kimi_key);
    bool nv_minimax = has_key(s_minimax_key);
    return (kc_kimi || nv_kimi) || (kc_minimax || nv_minimax);
}

// Resolves the effective key for a given provider, preferring Kconfig.
static const char *effective_kimi_key()
{
    if (has_key(CONFIG_PET_AI_USAGE_KIMI_KEY)) return CONFIG_PET_AI_USAGE_KIMI_KEY;
    return s_kimi_key;
}
static const char *effective_minimax_key()
{
    if (has_key(CONFIG_PET_AI_USAGE_MINIMAX_KEY)) return CONFIG_PET_AI_USAGE_MINIMAX_KEY;
    return s_minimax_key;
}

// ---------- HTTP fetch (delegated to pet::util::HttpClient) -------------
static util::HttpClient s_http;

// ---------- JSON parsing: Kimi ------------------------------------------
static int64_t cjson_str_to_i64(const cJSON *v)
{
    if (!cJSON_IsString(v) || !v->valuestring) return -1;
    return (int64_t)atoll(v->valuestring);
}

// v0.6.7-fix: Kimi API contract (confirmed 2026-07-17 against the live
// response):
//   usage.{limit,remaining}              = weekly window (resets weekly)
//   usage.used                          = weekly used
//   limits[0].window.duration           = 300 minutes (5 hour)
//   limits[0].detail.{limit,remaining}   = 5-hour sliding window
//   usage.resetTime / detail.resetTime   = ISO-8601 next reset
static void poll_kimi(Snapshot *s, const char *key)
{
    char body[RX_BUF];
    ESP_LOGI(TAG, "kimi: GET /usages");
    auto resp = s_http.fetch("https://api.kimi.com/coding/v1/usages",
                             key, body, sizeof(body));
    if (!resp.ok()) {
        ESP_LOGE(TAG, "kimi: http err=%s status=%d",
                 esp_err_to_name(resp.err), resp.status);
        s->kimi.any_ok = false;
        s->kimi.weekly.ok = false;
        s->kimi.five_hours.ok = false;
        snprintf(s->kimi.err, sizeof(s->kimi.err), "http:%s",
                 resp.err == ESP_OK ? "status" : esp_err_to_name(resp.err));
        return;
    }
    ESP_LOGI(TAG, "kimi: body %d bytes", (int)strlen(body));
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        ESP_LOGE(TAG, "kimi: cjson parse fail");
        s->kimi.any_ok = false;
        snprintf(s->kimi.err, sizeof(s->kimi.err), "json parse fail");
        return;
    }
    // Weekly: usage.{limit, remaining, resetTime}; used = limit - remaining
    cJSON *usage = cJSON_GetObjectItemCaseSensitive(root, "usage");
    if (cJSON_IsObject(usage)) {
        s->kimi.weekly.limit     = cjson_str_to_i64(cJSON_GetObjectItemCaseSensitive(usage, "limit"));
        s->kimi.weekly.remaining = cjson_str_to_i64(cJSON_GetObjectItemCaseSensitive(usage, "remaining"));
        cJSON *rst = cJSON_GetObjectItemCaseSensitive(usage, "resetTime");
        if (cJSON_IsString(rst) && rst->valuestring) {
            strncpy(s->kimi.weekly.reset_iso, rst->valuestring,
                    sizeof(s->kimi.weekly.reset_iso) - 1);
            s->kimi.weekly.reset_iso[sizeof(s->kimi.weekly.reset_iso) - 1] = 0;
            format_iso_utc_to_cn(rst->valuestring, s->kimi.weekly.reset_human,
                                  sizeof(s->kimi.weekly.reset_human));
        }
        if (s->kimi.weekly.limit > 0 && s->kimi.weekly.remaining >= 0) {
            s->kimi.weekly.used = s->kimi.weekly.limit - s->kimi.weekly.remaining;
            s->kimi.weekly.used_pct =
                (int)((s->kimi.weekly.used * 100) / s->kimi.weekly.limit);
            if (s->kimi.weekly.used_pct > 100) s->kimi.weekly.used_pct = 100;
        }
        s->kimi.weekly.ok = (s->kimi.weekly.limit > 0 &&
                             s->kimi.weekly.remaining >= 0);
        strncpy(s->kimi.weekly.label, "week",
                sizeof(s->kimi.weekly.label) - 1);
    }
    // 5h sliding window: limits[0].detail.{limit, remaining}
    cJSON *limits = cJSON_GetObjectItemCaseSensitive(root, "limits");
    if (cJSON_IsArray(limits) && cJSON_GetArraySize(limits) > 0) {
        cJSON *det = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetArrayItem(limits, 0), "detail");
        if (cJSON_IsObject(det)) {
            s->kimi.five_hours.limit = cjson_str_to_i64(cJSON_GetObjectItemCaseSensitive(det, "limit"));
            int64_t rem5 = cjson_str_to_i64(cJSON_GetObjectItemCaseSensitive(det, "remaining"));
            if (s->kimi.five_hours.limit > 0 && rem5 >= 0) {
                s->kimi.five_hours.remaining = rem5;
                s->kimi.five_hours.used = s->kimi.five_hours.limit - rem5;
                s->kimi.five_hours.used_pct =
                    (int)((s->kimi.five_hours.used * 100) / s->kimi.five_hours.limit);
                if (s->kimi.five_hours.used_pct > 100) s->kimi.five_hours.used_pct = 100;
            }
            cJSON *rst5 = cJSON_GetObjectItemCaseSensitive(det, "resetTime");
            if (cJSON_IsString(rst5) && rst5->valuestring) {
                strncpy(s->kimi.five_hours.reset_iso, rst5->valuestring,
                        sizeof(s->kimi.five_hours.reset_iso) - 1);
                s->kimi.five_hours.reset_iso[sizeof(s->kimi.five_hours.reset_iso) - 1] = 0;
                format_iso_utc_to_cn(rst5->valuestring, s->kimi.five_hours.reset_human,
                                      sizeof(s->kimi.five_hours.reset_human));
            }
            s->kimi.five_hours.ok = (s->kimi.five_hours.limit > 0 &&
                                      s->kimi.five_hours.remaining >= 0);
            strncpy(s->kimi.five_hours.label, "5h",
                    sizeof(s->kimi.five_hours.label) - 1);
        }
    }
    s->kimi.any_ok = (s->kimi.weekly.ok || s->kimi.five_hours.ok);
    ESP_LOGI(TAG, "kimi: week ok=%d (used=%lld limit=%lld rem=%lld pct=%d) "
                  "reset=%s, 5h ok=%d (used=%lld limit=%lld rem=%lld pct=%d) "
                  "reset=%s, any_ok=%d",
             s->kimi.weekly.ok,
             (long long)s->kimi.weekly.used, (long long)s->kimi.weekly.limit,
             (long long)s->kimi.weekly.remaining, s->kimi.weekly.used_pct,
             s->kimi.weekly.reset_human,
             s->kimi.five_hours.ok,
             (long long)s->kimi.five_hours.used, (long long)s->kimi.five_hours.limit,
             (long long)s->kimi.five_hours.remaining, s->kimi.five_hours.used_pct,
             s->kimi.five_hours.reset_human,
             s->kimi.any_ok);
    if (!s->kimi.any_ok && s->kimi.err[0] == 0) {
        snprintf(s->kimi.err, sizeof(s->kimi.err), "no data");
    }
    cJSON_Delete(root);
}

// ---------- JSON parsing: MiniMax ---------------------------------------
// MiniMax returns remaining-percent fields + raw epoch-ms timestamps.
//   end_time            = current 5h sliding-window end (ms since epoch)
//   weekly_end_time     = current weekly window end (ms since epoch)
//   current_interval_remaining_percent / current_weekly_remaining_percent
//                        = percent remaining (0..100)
static void fill_minimax(MiniMaxUsage *m, cJSON *it)
{
    cJSON *wk = cJSON_GetObjectItemCaseSensitive(it, "current_weekly_remaining_percent");
    if (cJSON_IsNumber(wk)) {
        int32_t rem = (int32_t)wk->valuedouble;
        if (rem >= 0 && rem <= 100) {
            int used_pct = 100 - rem;
            if (used_pct < 0)   used_pct = 0;
            if (used_pct > 100) used_pct = 100;
            m->weekly.used_pct = used_pct;
            m->weekly.remaining = rem;       // "remaining%" as raw value
            m->weekly.limit    = 100;       // so the UI can render "X/100%"
            m->weekly.used     = used_pct;
            m->weekly.ok = true;
        }
    }
    cJSON *wk_end = cJSON_GetObjectItemCaseSensitive(it, "weekly_end_time");
    if (cJSON_IsNumber(wk_end)) {
        int64_t ms = (int64_t)wk_end->valuedouble;
        format_epoch_ms_utc_to_cn(ms, m->weekly.reset_human,
                                  sizeof(m->weekly.reset_human));
    }

    cJSON *iv = cJSON_GetObjectItemCaseSensitive(it, "current_interval_remaining_percent");
    if (cJSON_IsNumber(iv)) {
        int32_t rem = (int32_t)iv->valuedouble;
        if (rem >= 0 && rem <= 100) {
            int used_pct = 100 - rem;
            if (used_pct < 0)   used_pct = 0;
            if (used_pct > 100) used_pct = 100;
            m->five_hours.used_pct = used_pct;
            m->five_hours.remaining = rem;
            m->five_hours.limit    = 100;
            m->five_hours.used     = used_pct;
            m->five_hours.ok = true;
        }
    }
    cJSON *iv_end = cJSON_GetObjectItemCaseSensitive(it, "end_time");
    if (cJSON_IsNumber(iv_end)) {
        int64_t ms = (int64_t)iv_end->valuedouble;
        format_epoch_ms_utc_to_cn(ms, m->five_hours.reset_human,
                                  sizeof(m->five_hours.reset_human));
    }
}

static void poll_minimax(Snapshot *s, const char *key)
{
    char body[RX_BUF];
    ESP_LOGI(TAG, "minimax: GET /token_plan/remains");
    auto resp = s_http.fetch("https://www.minimaxi.com/v1/token_plan/remains",
                             key, body, sizeof(body));
    if (!resp.ok()) {
        s->minimax.any_ok = false;
        s->minimax.weekly.ok = false;
        s->minimax.five_hours.ok = false;
        snprintf(s->minimax.err, sizeof(s->minimax.err), "http:%s",
                 resp.err == ESP_OK ? "status" : esp_err_to_name(resp.err));
        return;
    }
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        s->minimax.any_ok = false;
        snprintf(s->minimax.err, sizeof(s->minimax.err), "json parse fail");
        return;
    }
    strncpy(s->minimax.weekly.label, "week",
            sizeof(s->minimax.weekly.label) - 1);
    strncpy(s->minimax.five_hours.label, "5h",
            sizeof(s->minimax.five_hours.label) - 1);
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "model_remains");
    int arr_size = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (cJSON_IsArray(arr)) {
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            cJSON *nm = cJSON_GetObjectItemCaseSensitive(it, "model_name");
            ESP_LOGI(TAG, "minimax: model_remains[%d] name=%s",
                     arr_size--, nm && cJSON_IsString(nm) ? nm->valuestring : "?");
            if (!cJSON_IsString(nm) || !nm->valuestring) continue;
            if (strcmp(nm->valuestring, "general") != 0) continue;
            fill_minimax(&s->minimax, it);
            break;
        }
    }
    s->minimax.any_ok = (s->minimax.weekly.ok || s->minimax.five_hours.ok);
    ESP_LOGI(TAG, "minimax: week ok=%d pct=%d reset=%s, 5h ok=%d pct=%d reset=%s, any_ok=%d",
             s->minimax.weekly.ok, s->minimax.weekly.used_pct, s->minimax.weekly.reset_human,
             s->minimax.five_hours.ok, s->minimax.five_hours.used_pct, s->minimax.five_hours.reset_human,
             s->minimax.any_ok);
    if (!s->minimax.any_ok && s->minimax.err[0] == 0) {
        snprintf(s->minimax.err, sizeof(s->minimax.err), "no data");
    }
    cJSON_Delete(root);
}

// ---------- worker task -------------------------------------------------
static bool wifi_is_up()
{
    app::wifi_status st;
    app::wifi_manager_get_status(&st);
    return st.state == app::WIFI_CONN_CONNECTED;
}

static void do_poll_locked(Snapshot *s)
{
    s->refreshing = true;
    if (!wifi_is_up()) {
        const char *msg = "no wifi";
        s->kimi.any_ok = false;
        s->kimi.weekly.ok = false;
        s->kimi.five_hours.ok = false;
        snprintf(s->kimi.err, sizeof(s->kimi.err), "%s", msg);
        s->minimax.any_ok = false;
        s->minimax.weekly.ok = false;
        s->minimax.five_hours.ok = false;
        snprintf(s->minimax.err, sizeof(s->minimax.err), "%s", msg);
        s->refreshing = false;
        s->last_success_ms = esp_timer_get_time() / 1000;
        return;
    }
    const char *kk = effective_kimi_key();
    const char *mk = effective_minimax_key();
    if (has_key(kk)) {
        poll_kimi(s, kk);
    } else {
        s->kimi.any_ok = false;
        s->kimi.weekly.ok = false;
        s->kimi.five_hours.ok = false;
        snprintf(s->kimi.err, sizeof(s->kimi.err), "no key");
    }
    if (has_key(mk)) {
        poll_minimax(s, mk);
    } else {
        s->minimax.any_ok = false;
        s->minimax.weekly.ok = false;
        s->minimax.five_hours.ok = false;
        snprintf(s->minimax.err, sizeof(s->minimax.err), "no key");
    }
    s->refreshing = false;
    s->last_success_ms = esp_timer_get_time() / 1000;
}

// v0.6.7 fix: ai_task is created at boot, before Wi-Fi has finished
// auto-connecting from NVS. The first poll attempts DNS while the link
// is still IDLE and fails with EAI_AGAIN. Subsequent polls also fail if
// the link drops mid-life. Wait up to 30 s on entry for the link to come
// up, and on every poll skip when down (the 5-minute timer keeps
// ticking so we recover automatically).
//
// request_refresh() from the UI wakes the worker via a FreeRTOS task
// notification; the worker waits for either the notification or the
// next 5-minute tick.
static void ai_task(void * /*arg*/)
{
    int64_t period_us = (int64_t)POLL_PERIOD_MS * 1000;
    s_worker_task = xTaskGetCurrentTaskHandle();
    for (int waited_s = 0; waited_s < 30 && !wifi_is_up(); waited_s++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "waiting for Wi-Fi (%ds)", waited_s + 1);
    }
    while (true) {
        int64_t t0 = esp_timer_get_time();
        {
            std::lock_guard<std::mutex> lk(s_mtx);
            do_poll_locked(&s_snap);
        }
        int64_t elapsed = esp_timer_get_time() - t0;
        int64_t sleep_us = period_us - elapsed;
        if (sleep_us < 0) sleep_us = period_us;
        // Sleep until the next periodic tick OR an explicit refresh
        // notification from the UI (whichever comes first). Cap the
        // block at 60 s so we re-check WiFi state often enough.
        uint32_t block_ms = (uint32_t)(sleep_us / 1000);
        if (block_ms > 60000) block_ms = 60000;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(block_ms));
    }
}

void request_refresh()
{
    if (s_worker_task) {
        xTaskNotifyGive(s_worker_task);
    }
}

void start()
{
    if (!enabled()) {
        ESP_LOGI(TAG, "no keys configured; AI Usage tab hidden");
        return;
    }
    BaseType_t rc = xTaskCreate(ai_task, "ai_usage",
                                TASK_STACK_BYTES, nullptr, 3, nullptr);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "failed to create ai_usage task");
    } else {
        ESP_LOGI(TAG, "ai_usage task started");
    }
}

void get_snapshot(Snapshot *out)
{
    if (!out) return;
    std::lock_guard<std::mutex> lk(s_mtx);
    *out = s_snap;
}

// ---------- UI: build_page / destroy_page -------------------------------
// Layout (320 x 208):
//   y=0..16   "AI Usage" title 14pt CENTER + "HH:MM" 16pt RIGHT
//   y=24..40  "Kimi"     20pt left
//   y=46..56  week row   12pt   "week"                            "X/Y >MM-DD HH:MM"
//   y=58..64  week bar   290x6
//   y=70..80  5h row     12pt   "5h"
//   y=82..88  5h bar
//   y=98..114 "MiniMax"  20pt left
//   y=120..130 weekly row
//   y=132..138 weekly bar
//   y=144..154 5h row
//   y=156..162 5h bar
//   y=180..196 status + Refresh
struct BarWidgets {
    lv_obj_t *value_lbl = nullptr;   // right-aligned text
    lv_obj_t *bar      = nullptr;
};

struct AiusageCtx {
    // Header
    lv_obj_t    *clock_lbl         = nullptr;   // top-right real-time "HH:MM"
    // Kimi card
    lv_obj_t    *kimi_title        = nullptr;
    BarWidgets   kimi_week;
    BarWidgets   kimi_5h;
    // MiniMax card
    lv_obj_t    *minimax_title     = nullptr;
    BarWidgets   minimax_weekly;
    BarWidgets   minimax_5h;
    // Status + refresh
    lv_obj_t    *status_lbl        = nullptr;
    lv_timer_t  *poll              = nullptr;
};
static AiusageCtx s_ctx;

static lv_color_t color_for_pct(int pct)
{
    if (pct < 0)        return lv_color_hex(0x90A4AE);
    if (pct >= 90)      return lv_color_hex(0xEF5350);
    if (pct >= 75)      return lv_color_hex(0xFFD54F);
    return lv_color_hex(0x66BB6A);
}

// Render the local clock "HH:MM" in CN (UTC+8). Reads from the system
// time (set by SNTP when Wi-Fi is up). Before SNTP completes the
// display shows "--:--" instead of a nonsense 1970-derived time.
static void render_clock()
{
    if (!s_ctx.clock_lbl) return;
    time_t now = time(nullptr);
    if (now < 1700000000) {  // before ~2023-11-14; SNTP hasn't synced yet
        lv_label_set_text(s_ctx.clock_lbl, "--:--");
        return;
    }
    // Apply the CN offset (UTC+8) and render as wall-clock HH:MM.
    time_t local = now + 8 * 3600;
    struct tm tm;
    gmtime_r(&local, &tm);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(s_ctx.clock_lbl, buf);
}

// Compose "used/limit" or "used%" depending on which numbers we have.
static void format_bar_text(char *out, size_t sz,
                            int64_t used, int64_t limit, int used_pct)
{
    if (used_pct >= 0 && limit <= 0) {
        // MiniMax-style: only percent.
        snprintf(out, sz, "%d%%", used_pct);
    } else if (used >= 0 && limit > 0) {
        snprintf(out, sz, "%lld/%lld", (long long)used, (long long)limit);
    } else if (used >= 0) {
        snprintf(out, sz, "%lld", (long long)used);
    } else {
        snprintf(out, sz, "-");
    }
}

static void render_bar_widgets(BarWidgets &w, const ProviderBar &bar,
                               const char *label)
{
    if (!w.value_lbl || !w.bar) return;
    char val[40];
    if (bar.ok) {
        format_bar_text(val, sizeof(val), bar.used, bar.limit, bar.used_pct);
    } else {
        snprintf(val, sizeof(val), "-");
    }
    lv_label_set_text(w.value_lbl, val);
    lv_obj_set_style_text_color(w.value_lbl, color_for_pct(bar.used_pct), 0);
    int v = (bar.ok && bar.used_pct >= 0) ? bar.used_pct : 0;
    lv_bar_set_value(w.bar, v, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(w.bar, color_for_pct(bar.used_pct), LV_PART_INDICATOR);
    (void)label;  // reserved for future use
}

static void render_kimi(const Snapshot &s)
{
    if (!s_ctx.kimi_title) return;
    const KimiUsage &k = s.kimi;
    if (!k.any_ok) {
        lv_label_set_text(s_ctx.kimi_title, "Kimi: --");
        const char *err = k.err[0] ? k.err : "-";
        render_bar_widgets(s_ctx.kimi_week, k.weekly, "week");
        lv_label_set_text(s_ctx.kimi_week.value_lbl, err);
        lv_obj_set_style_text_color(s_ctx.kimi_week.value_lbl,
                                    lv_color_hex(0x90A4AE), 0);
        render_bar_widgets(s_ctx.kimi_5h, k.five_hours, "5h");
        lv_label_set_text(s_ctx.kimi_5h.value_lbl, "-");
        return;
    }
    lv_label_set_text(s_ctx.kimi_title, "Kimi");
    // The bars themselves are set by render_bar_widgets; the value
    // labels get the formatted "X/Y >MM-DD HH:MM" string below so the
    // user sees both the counter and when it resets in one line.
    lv_bar_set_value(s_ctx.kimi_week.bar,
                     (k.weekly.ok && k.weekly.used_pct >= 0)
                         ? k.weekly.used_pct : 0,
                     LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ctx.kimi_week.bar,
                              color_for_pct(k.weekly.used_pct),
                              LV_PART_INDICATOR);
    lv_bar_set_value(s_ctx.kimi_5h.bar,
                     (k.five_hours.ok && k.five_hours.used_pct >= 0)
                         ? k.five_hours.used_pct : 0,
                     LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ctx.kimi_5h.bar,
                              color_for_pct(k.five_hours.used_pct),
                              LV_PART_INDICATOR);
    // Append reset time to the value labels so the user sees both the
    // counter and when it resets, in compact form.
    char week_buf[40], five_h_buf[40];
    snprintf(week_buf, sizeof(week_buf), "%lld/%lld",
             (long long)k.weekly.used, (long long)k.weekly.limit);
    if (k.weekly.reset_human[0]) {
        size_t n = strlen(week_buf);
        snprintf(week_buf + n, sizeof(week_buf) - n,
                 " >%s", k.weekly.reset_human);
    }
    if (s_ctx.kimi_week.value_lbl) {
        lv_label_set_text(s_ctx.kimi_week.value_lbl, week_buf);
        lv_obj_set_style_text_color(s_ctx.kimi_week.value_lbl,
                                    color_for_pct(k.weekly.used_pct), 0);
    }
    snprintf(five_h_buf, sizeof(five_h_buf), "%lld/%lld",
             (long long)k.five_hours.used, (long long)k.five_hours.limit);
    if (k.five_hours.reset_human[0]) {
        size_t n = strlen(five_h_buf);
        snprintf(five_h_buf + n, sizeof(five_h_buf) - n,
                 " >%s", k.five_hours.reset_human);
    }
    if (s_ctx.kimi_5h.value_lbl) {
        lv_label_set_text(s_ctx.kimi_5h.value_lbl, five_h_buf);
        lv_obj_set_style_text_color(s_ctx.kimi_5h.value_lbl,
                                    color_for_pct(k.five_hours.used_pct), 0);
    }
}

static void render_minimax(const Snapshot &s)
{
    if (!s_ctx.minimax_title) return;
    const MiniMaxUsage &m = s.minimax;
    if (!m.any_ok) {
        lv_label_set_text(s_ctx.minimax_title, "MiniMax: --");
        const char *err = m.err[0] ? m.err : "-";
        render_bar_widgets(s_ctx.minimax_weekly, m.weekly, "weekly");
        lv_label_set_text(s_ctx.minimax_weekly.value_lbl, err);
        lv_obj_set_style_text_color(s_ctx.minimax_weekly.value_lbl,
                                    lv_color_hex(0x90A4AE), 0);
        render_bar_widgets(s_ctx.minimax_5h, m.five_hours, "5h");
        lv_label_set_text(s_ctx.minimax_5h.value_lbl, "-");
        return;
    }
    lv_label_set_text(s_ctx.minimax_title, "MiniMax");
    lv_bar_set_value(s_ctx.minimax_weekly.bar,
                     (m.weekly.ok && m.weekly.used_pct >= 0)
                         ? m.weekly.used_pct : 0,
                     LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ctx.minimax_weekly.bar,
                              color_for_pct(m.weekly.used_pct),
                              LV_PART_INDICATOR);
    lv_bar_set_value(s_ctx.minimax_5h.bar,
                     (m.five_hours.ok && m.five_hours.used_pct >= 0)
                         ? m.five_hours.used_pct : 0,
                     LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ctx.minimax_5h.bar,
                              color_for_pct(m.five_hours.used_pct),
                              LV_PART_INDICATOR);
    // Append reset time in CN-local "MM-DD HH:MM" to both bars.
    char week_buf[40], five_h_buf[40];
    snprintf(week_buf, sizeof(week_buf), "%d%%", m.weekly.used_pct);
    if (m.weekly.reset_human[0]) {
        size_t n = strlen(week_buf);
        snprintf(week_buf + n, sizeof(week_buf) - n,
                 " >%s", m.weekly.reset_human);
    }
    if (s_ctx.minimax_weekly.value_lbl) {
        lv_label_set_text(s_ctx.minimax_weekly.value_lbl, week_buf);
        lv_obj_set_style_text_color(s_ctx.minimax_weekly.value_lbl,
                                    color_for_pct(m.weekly.used_pct), 0);
    }
    snprintf(five_h_buf, sizeof(five_h_buf), "%d%%", m.five_hours.used_pct);
    if (m.five_hours.reset_human[0]) {
        size_t n = strlen(five_h_buf);
        snprintf(five_h_buf + n, sizeof(five_h_buf) - n,
                 " >%s", m.five_hours.reset_human);
    }
    if (s_ctx.minimax_5h.value_lbl) {
        lv_label_set_text(s_ctx.minimax_5h.value_lbl, five_h_buf);
        lv_obj_set_style_text_color(s_ctx.minimax_5h.value_lbl,
                                    color_for_pct(m.five_hours.used_pct), 0);
    }
}

static void render_status(const Snapshot &s, int64_t now_ms)
{
    if (!s_ctx.status_lbl) return;
    char line[64];
    lv_color_t color = lv_color_white();
    if (s.last_success_ms == 0 && !s.refreshing) {
        snprintf(line, sizeof(line), "Configure keys");
        color = lv_color_hex(0xFFB74D);
    } else if (s.refreshing) {
        snprintf(line, sizeof(line), "Refreshing...");
        color = lv_color_hex(0x90A4AE);
    } else {
        int64_t age = now_ms - s.last_success_ms;
        if (age >= 6 * 60 * 1000) {
            snprintf(line, sizeof(line), "Stale (%lds)",
                     (long)(age / 1000));
            color = lv_color_hex(0xEF5350);
        } else {
            int64_t remain_ms = POLL_PERIOD_MS - age;
            if (remain_ms < 0) remain_ms = 0;
            int m = (int)(remain_ms / 60000);
            int sec = (int)((remain_ms / 1000) % 60);
            snprintf(line, sizeof(line), "Next: %dm%02ds", m, sec);
            color = lv_color_hex(0x90A4AE);
        }
    }
    lv_label_set_text(s_ctx.status_lbl, line);
    lv_obj_set_style_text_color(s_ctx.status_lbl, color, 0);
}

static void aiusage_poll_cb(lv_timer_t *t)
{
    (void)t;
    Snapshot s;
    get_snapshot(&s);
    int64_t now_ms = esp_timer_get_time() / 1000;
    render_clock();
    render_kimi(s);
    render_minimax(s);
    render_status(s, now_ms);
}

// Build a single bar row: label on the left + value label on the right.
// The bar itself sits between them (so the layout fits 320 px wide).
static void build_bar_row(lv_obj_t *parent,
                          int y,
                          const char *label,
                          BarWidgets &out)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_label_set_text(l, label);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 8, y);

    out.value_lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(out.value_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(out.value_lbl, lv_color_white(), 0);
    lv_label_set_text(out.value_lbl, "-");
    lv_obj_align(out.value_lbl, LV_ALIGN_TOP_RIGHT, -8, y);

    out.bar = lv_bar_create(parent);
    lv_obj_set_size(out.bar, 290, 6);
    lv_obj_align(out.bar, LV_ALIGN_TOP_LEFT, 8, y + 12);
    lv_bar_set_range(out.bar, 0, 100);
    lv_bar_set_value(out.bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(out.bar, lv_color_hex(0x37474F),
                              LV_PART_INDICATOR);
}

lv_obj_t *build_page(lv_obj_t *parent)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 208);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    // Top: page title left-of-center, real-time clock at top-right.
    lv_obj_t *title = lv_label_create(root);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "AI Usage");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 4);

    s_ctx.clock_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(s_ctx.clock_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ctx.clock_lbl, lv_color_white(), 0);
    lv_label_set_text(s_ctx.clock_lbl, "--:--");
    lv_obj_align(s_ctx.clock_lbl, LV_ALIGN_TOP_RIGHT, -8, 2);

    // Kimi card. Big title at y=24, two rows at y=54 / y=80.
    s_ctx.kimi_title = lv_label_create(root);
    lv_obj_set_style_text_font(s_ctx.kimi_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ctx.kimi_title, lv_color_white(), 0);
    lv_label_set_text(s_ctx.kimi_title, "Kimi");
    lv_obj_align(s_ctx.kimi_title, LV_ALIGN_TOP_LEFT, 8, 24);

    build_bar_row(root, 54, "week", s_ctx.kimi_week);
    build_bar_row(root, 80, "5h",   s_ctx.kimi_5h);

    // MiniMax card. Big title at y=106, rows at y=134 / y=160.
    s_ctx.minimax_title = lv_label_create(root);
    lv_obj_set_style_text_font(s_ctx.minimax_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ctx.minimax_title, lv_color_white(), 0);
    lv_label_set_text(s_ctx.minimax_title, "MiniMax");
    lv_obj_align(s_ctx.minimax_title, LV_ALIGN_TOP_LEFT, 8, 106);

    build_bar_row(root, 134, "weekly", s_ctx.minimax_weekly);
    build_bar_row(root, 160, "5h",     s_ctx.minimax_5h);

    // Status line.
    s_ctx.status_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(s_ctx.status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ctx.status_lbl, lv_color_hex(0x90A4AE), 0);
    lv_label_set_text(s_ctx.status_lbl, "Configure keys");
    lv_obj_align(s_ctx.status_lbl, LV_ALIGN_BOTTOM_LEFT, 8, -4);

    // v0.6.7: manual Refresh button bottom-right.
    lv_obj_t *refresh_btn = lv_button_create(root);
    lv_obj_set_size(refresh_btn, 70, 24);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -2);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x1976D2), 0);
    lv_obj_t *refresh_lbl = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_lbl, "Refresh");
    lv_obj_center(refresh_lbl);
    lv_obj_add_event_cb(refresh_btn, [](lv_event_t *ev) {
        (void)ev;
        request_refresh();
    }, LV_EVENT_CLICKED, nullptr);

    s_ctx.poll = lv_timer_create(aiusage_poll_cb, 500, nullptr);
    return root;
}

void destroy_page(lv_obj_t *root)
{
    (void)root;
    if (s_ctx.poll) {
        lv_timer_del(s_ctx.poll);
        s_ctx.poll = nullptr;
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
}

void register_page_handlers()
{
    if (!enabled()) return;
    pet::pages::set_ai_usage_enabled(true);
    pet::pages::register_page(pet::pages::Page::AIUsage,
                              build_page, destroy_page);
    ESP_LOGI(TAG, "AIUsage page registered");
}

}  // namespace ai_usage
}  // namespace pet