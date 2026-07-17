#include "wifi_manager.h"

#include <cstring>
#include <atomic>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

namespace app {

static const char *TAG = "wifi_manager";

#define WIFI_MANAGER_MAX_APS 20
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_EVENT_Q_LEN    8

// NVS namespace for runtime-saved Wi-Fi credentials. Key schema:
//   key "ssid"     string
//   key "password" string
static const char *kNsWifi = "wifi_cfg";
static const char *kKeySsid = "ssid";
static const char *kKeyPassword = "password";

struct WifiEvent {
    int      kind;       // matches enum wifi_conn_state values
    char     ssid[33];
    char     ip[16];
    int8_t   rssi;
};

// Module-level state, all initialised by wifi_task before any events fire.
static EventGroupHandle_t  s_events      = nullptr;
static QueueHandle_t       s_evt_queue    = nullptr;   // WifiEvent msgs
static wifi_status          s_status;                    // latest snapshot
static std::atomic<bool>   s_scan_inflight{false};
static wifi_ap_record_t    s_scan[WIFI_MANAGER_MAX_APS];
static int                 s_scan_count  = 0;
static bool                s_nvs_loaded  = false;
static char                s_nvs_ssid[33];
static char                s_nvs_password[64];

// ---- helpers ---------------------------------------------------------------
static void persist_credentials(const char *ssid, const char *password)
{
    nvs_handle_t h;
    if (nvs_open(kNsWifi, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, kKeySsid, ssid ? ssid : "");
    nvs_set_str(h, kKeyPassword, password ? password : "");
    nvs_commit(h);
    nvs_close(h);
    s_nvs_loaded = true;
    strncpy(s_nvs_ssid, ssid ? ssid : "", sizeof(s_nvs_ssid) - 1);
    s_nvs_ssid[sizeof(s_nvs_ssid) - 1] = 0;
    strncpy(s_nvs_password, password ? password : "", sizeof(s_nvs_password) - 1);
    s_nvs_password[sizeof(s_nvs_password) - 1] = 0;
}

static void load_credentials(char *out_ssid, size_t ssid_sz,
                           char *out_pass, size_t pass_sz)
{
    nvs_handle_t h;
    if (nvs_open(kNsWifi, NVS_READONLY, &h) != ESP_OK) {
        // Fall back to Kconfig defaults so a freshly-flashed board with
        // CONFIG_PET_WIFI_SSID set still works.
        strncpy(out_ssid, CONFIG_PET_WIFI_SSID, ssid_sz - 1);
        out_ssid[ssid_sz - 1] = 0;
        strncpy(out_pass, CONFIG_PET_WIFI_PASSWORD, pass_sz - 1);
        out_pass[pass_sz - 1] = 0;
        return;
    }
    size_t s1 = ssid_sz, s2 = pass_sz;
    nvs_get_str(h, kKeySsid, out_ssid, &s1);
    nvs_get_str(h, kKeyPassword, out_pass, &s2);
    nvs_close(h);
}

static void post_event(int kind, const char *ssid, const char *ip, int8_t rssi)
{
    if (!s_evt_queue) return;
    WifiEvent ev{};
    ev.kind = kind;
    if (ssid) strncpy(ev.ssid, ssid, sizeof(ev.ssid) - 1);
    if (ip)   strncpy(ev.ip,   ip,   sizeof(ev.ip)   - 1);
    ev.rssi = rssi;
    // Drop the oldest if the queue is full (non-blocking).
    if (xQueueSend(s_evt_queue, &ev, 0) != pdTRUE) {
        WifiEvent drop;
        xQueueReceive(s_evt_queue, &drop, 0);
        xQueueSend(s_evt_queue, &ev, 0);
    }
}

static void set_status_ssid(const char *ssid)
{
    strncpy(s_status.ssid, ssid ? ssid : "", sizeof(s_status.ssid) - 1);
    s_status.ssid[sizeof(s_status.ssid) - 1] = 0;
}

// ---- event handler ----------------------------------------------------------
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_status.state = WIFI_CONN_CONNECTING;
        set_status_ssid(s_status.ssid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *event = static_cast<wifi_event_sta_connected_t *>(event_data);
        // BSSID is 6 bytes; we just log the SSID here.
        set_status_ssid((const char *)event->ssid);
        s_status.state = WIFI_CONN_CONNECTING;  // wait for IP
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event =
            static_cast<wifi_event_sta_disconnected_t *>(event_data);
        ESP_LOGW(TAG, "Disconnected from AP, reason=%d, rssi=%d",
                 event->reason, event->rssi);
        s_status.state = WIFI_CONN_FAILED;
        strncpy(s_status.ip, "0.0.0.0", sizeof(s_status.ip));
        // If we're inside the connect-then-fail flow, post a notification.
        post_event(WIFI_CONN_FAILED, nullptr, nullptr, 0);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_status.state = WIFI_CONN_CONNECTED;
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);

        // v0.6: kick off SNTP once we have an IP. If SNTP fails, PetMeta
        // skips — no streak change, no false reset.
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        ESP_LOGI(TAG, "SNTP init requested (server: pool.ntp.org)");
        post_event(WIFI_CONN_CONNECTED, s_status.ssid, s_status.ip, s_status.rssi);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        // esp_wifi_scan_get_ap_records fills `s_scan` with up to MAX_APS.
        uint16_t n = WIFI_MANAGER_MAX_APS;
        if (esp_wifi_scan_get_ap_records(&n, s_scan) == ESP_OK) {
            s_scan_count = (int)n;
        } else {
            s_scan_count = 0;
        }
        s_scan_inflight.store(false);
        ESP_LOGI(TAG, "Scan done, %d APs", s_scan_count);
        post_event(WIFI_CONN_IDLE, nullptr, nullptr, 0);  // re-arms UI refresh
    }
}

// ---- worker task: drives scan/connect from a request queue -----------------
struct WifiCmd {
    int      kind;  // 0 = scan, 1 = connect, 2 = disconnect
    char     ssid[33];
    char     pass[64];
};
static QueueHandle_t s_cmd_queue = nullptr;

static void do_connect_locked(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {};
    strncpy(reinterpret_cast<char *>(cfg.sta.ssid), ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy(reinterpret_cast<char *>(cfg.sta.password), pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    cfg.sta.pmf_cfg.capable    = true;
    cfg.sta.pmf_cfg.required   = false;
    cfg.sta.scan_method        = WIFI_ALL_CHANNEL_SCAN;

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    set_status_ssid(ssid);
    s_status.state = WIFI_CONN_CONNECTING;
    esp_wifi_connect();
    ESP_LOGI(TAG, "Connect requested: ssid=%s", ssid);

    // Wait up to 30 s for IP_EVENT_STA_GOT_IP or fail.
    EventBits_t bits = xEventGroupWaitBits(
        s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_FAIL_BIT) {
        s_status.state = WIFI_CONN_FAILED;
        ESP_LOGE(TAG, "Connect to %s failed", ssid);
    } else if (bits & WIFI_CONNECTED_BIT) {
        s_status.state = WIFI_CONN_CONNECTED;
        ESP_LOGI(TAG, "Connected to %s", ssid);
    } else {
        s_status.state = WIFI_CONN_FAILED;
        ESP_LOGW(TAG, "Connect to %s timed out", ssid);
    }
    post_event(s_status.state, ssid, s_status.ip, 0);
}

static void wifi_task(void *arg)
{
    s_events = xEventGroupCreate();
    s_evt_queue = xQueueCreate(WIFI_EVENT_Q_LEN, sizeof(WifiEvent));
    s_cmd_queue = xQueueCreate(4, sizeof(WifiCmd));

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any = nullptr;
    esp_event_handler_instance_t inst_got_ip = nullptr;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &event_handler, nullptr, &inst_any);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &event_handler, nullptr, &inst_got_ip);
    // Also register SCAN_DONE handler.
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                        &event_handler, nullptr, nullptr);

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));

    // v0.6.6 fix: STA-only mode requires an explicit country before
    // esp_wifi_scan_start() will broadcast probe requests. Without this
    // the scan call returns ESP_OK but never produces a SCAN_DONE event
    // and the AP list stays empty indefinitely.
    wifi_country_t cc = {};
    cc.cc[0] = 'C'; cc.cc[1] = 'N'; cc.cc[2] = 0;
    cc.schan = 1;
    cc.nchan = 13;
    cc.policy = WIFI_COUNTRY_POLICY_AUTO;
    esp_err_t cc_err = esp_wifi_set_country(&cc);
    ESP_LOGI(TAG, "esp_wifi_set_country(CN) returned %s",
             esp_err_to_name(cc_err));

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));

    // Initial status: not connected.
    s_status.state = WIFI_CONN_IDLE;
    strncpy(s_status.ssid, "", sizeof(s_status.ssid));
    strncpy(s_status.ip, "0.0.0.0", sizeof(s_status.ip));
    s_status.rssi = 0;

    // If NVS has saved credentials, attempt first connect.
    char ssid[33] = {};
    char pass[64] = {};
    load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (ssid[0] != 0) {
        ESP_LOGI(TAG, "Found saved SSID '%s'; auto-connecting", ssid);
        // Clear the connect bit so we can wait for the right edge.
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        do_connect_locked(ssid, pass);
    } else {
        ESP_LOGI(TAG, "No saved SSID; idle");
    }

    // Worker loop: dispatch scan / connect / disconnect requests.
    while (true) {
        WifiCmd cmd;
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd.kind == 0) {
                if (s_scan_inflight.load()) {
                    ESP_LOGW(TAG, "Scan already in flight; ignoring");
                    continue;
                }
                s_scan_inflight.store(true);
                s_status.state = WIFI_CONN_SCANNING;
                wifi_scan_config_t sc = {};
                sc.show_hidden = false;
                sc.scan_type   = WIFI_SCAN_TYPE_ACTIVE;
                // scan_time 0 = "use driver default minimal dwell" which
                // is what every ESP-IDF example uses.
                esp_err_t err = esp_wifi_scan_start(&sc, false);
                if (err != ESP_OK) {
                    s_scan_inflight.store(false);
                    s_status.state = WIFI_CONN_IDLE;
                    ESP_LOGE(TAG, "esp_wifi_scan_start: %s",
                             esp_err_to_name(err));
                }
            } else if (cmd.kind == 1) {
                // Persist + connect.
                persist_credentials(cmd.ssid, cmd.pass);
                // Clear bits before waiting.
                xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
                do_connect_locked(cmd.ssid, cmd.pass);
            } else if (cmd.kind == 2) {
                esp_wifi_disconnect();
                s_status.state = WIFI_CONN_DISCONNECTED;
                strncpy(s_status.ip, "0.0.0.0", sizeof(s_status.ip));
                post_event(WIFI_CONN_DISCONNECTED, nullptr, nullptr, 0);
            }
        }
    }
}

// ---- public API ------------------------------------------------------------
esp_err_t wifi_manager_init()
{
    static bool started = false;
    if (started) return ESP_OK;
    started = true;
    BaseType_t rc = xTaskCreate(wifi_task, "wifi_task", 6144, nullptr, 5, nullptr);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void wifi_manager_get_status(wifi_status *out)
{
    if (!out) return;
    *out = s_status;
}

esp_err_t wifi_manager_scan_start()
{
    if (!s_cmd_queue) return ESP_ERR_WIFI_STATE;
    WifiCmd cmd = {};
    cmd.kind = 0;
    if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
        return ESP_ERR_WIFI_STATE;
    }
    return ESP_OK;
}

bool wifi_manager_is_scan_done() { return !s_scan_inflight.load(); }
int  wifi_manager_scan_count()    { return s_scan_count; }
const wifi_ap_record_t *wifi_manager_scan_results() { return s_scan; }

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!s_cmd_queue || !ssid || !password) return ESP_ERR_INVALID_ARG;
    WifiCmd cmd = {};
    cmd.kind = 1;
    strncpy(cmd.ssid, ssid, sizeof(cmd.ssid) - 1);
    strncpy(cmd.pass, password, sizeof(cmd.pass) - 1);
    if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
        return ESP_ERR_WIFI_STATE;
    }
    return ESP_OK;
}

esp_err_t wifi_manager_disconnect()
{
    if (!s_cmd_queue) return ESP_ERR_WIFI_STATE;
    WifiCmd cmd = {};
    cmd.kind = 2;
    if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
        return ESP_ERR_WIFI_STATE;
    }
    return ESP_OK;
}

esp_err_t wifi_manager_forget()
{
    nvs_handle_t h;
    if (nvs_open(kNsWifi, NVS_READWRITE, &h) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    nvs_erase_key(h, kKeySsid);
    nvs_erase_key(h, kKeyPassword);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    s_nvs_loaded = false;
    s_nvs_ssid[0] = 0;
    s_nvs_password[0] = 0;
    return err;
}

} // namespace app