#include "wifi_manager.h"

#include <cstring>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "util/NvsHandle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

namespace app {

static const char *TAG = "wifi_manager";

// NVS namespace for runtime-saved Wi-Fi credentials. Key schema:
//   key "ssid"     string
//   key "password" string
static const char *kNsWifi = "wifi_cfg";
static const char *kKeySsid = "ssid";
static const char *kKeyPassword = "password";

// ---- singleton ---------------------------------------------------------------
WifiManager &WifiManager::instance() noexcept
{
    static WifiManager s;
    return s;
}

// ---- helpers -----------------------------------------------------------------
void WifiManager::persist_credentials(const char *ssid, const char *password)
{
    {
        NvsHandle h(kNsWifi);
        h.set_str(kKeySsid, ssid ? ssid : "");
        h.set_str(kKeyPassword, password ? password : "");
        h.commit();
    }
    nvs_loaded_ = true;
    strncpy(nvs_ssid_, ssid ? ssid : "", sizeof(nvs_ssid_) - 1);
    nvs_ssid_[sizeof(nvs_ssid_) - 1] = 0;
    strncpy(nvs_password_, password ? password : "", sizeof(nvs_password_) - 1);
    nvs_password_[sizeof(nvs_password_) - 1] = 0;
}

void WifiManager::load_credentials(char *out_ssid, size_t ssid_sz,
                                   char *out_pass, size_t pass_sz)
{
    NvsHandle h(kNsWifi, NVS_READONLY);
    size_t sz = ssid_sz;
    if (h.get_str(kKeySsid, out_ssid, &sz) != ESP_OK) {
        strncpy(out_ssid, CONFIG_PET_WIFI_SSID, ssid_sz - 1);
        out_ssid[ssid_sz - 1] = 0;
        strncpy(out_pass, CONFIG_PET_WIFI_PASSWORD, pass_sz - 1);
        out_pass[pass_sz - 1] = 0;
        return;
    }
    sz = pass_sz;
    if (h.get_str(kKeyPassword, out_pass, &sz) != ESP_OK) {
        out_pass[0] = 0;
    }
}

void WifiManager::post_event(wifi_conn_state kind, const char *ssid, const char *ip, int8_t rssi)
{
    if (!evt_queue_) return;
    WifiEvent ev{};
    ev.kind = kind;
    if (ssid) strncpy(ev.ssid, ssid, sizeof(ev.ssid) - 1);
    if (ip)   strncpy(ev.ip,   ip,   sizeof(ev.ip)   - 1);
    ev.rssi = rssi;
    if (xQueueSend(evt_queue_, &ev, 0) != pdTRUE) {
        WifiEvent drop;
        xQueueReceive(evt_queue_, &drop, 0);
        xQueueSend(evt_queue_, &ev, 0);
    }
}

void WifiManager::set_status_ssid(const char *ssid)
{
    strncpy(status_.ssid, ssid ? ssid : "", sizeof(status_.ssid) - 1);
    status_.ssid[sizeof(status_.ssid) - 1] = 0;
}

// ---- event handler (static, dispatches via singleton) ------------------------
void WifiManager::event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    auto &self = instance();

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        self.status_.state = wifi_conn_state::Connecting;
        self.set_status_ssid(self.status_.ssid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *event = static_cast<wifi_event_sta_connected_t *>(event_data);
        self.set_status_ssid((const char *)event->ssid);
        self.status_.state = wifi_conn_state::Connecting;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event =
            static_cast<wifi_event_sta_disconnected_t *>(event_data);
        ESP_LOGW(TAG, "Disconnected from AP, reason=%d, rssi=%d",
                 event->reason, event->rssi);
        self.status_.state = wifi_conn_state::Failed;
        strncpy(self.status_.ip, "0.0.0.0", sizeof(self.status_.ip));
        self.post_event(wifi_conn_state::Failed, nullptr, nullptr, 0);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        self.status_.state = wifi_conn_state::Connected;
        snprintf(self.status_.ip, sizeof(self.status_.ip), IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(self.events_, BIT0);

        // v0.6.6 fix: SNTP can only be init'd once. Re-entering the
        // event after a disconnect/reconnect would call
        // esp_sntp_setoperatingmode() while the client is still running
        // and trip an lwip assertion that abort()s the entire app.
        // Skip the init path on subsequent GOT_IP events — the server
        // name + operatingmode already survive in the LWIP SNTP module.
        static bool s_sntp_inited = false;
        if (!s_sntp_inited) {
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            s_sntp_inited = true;
            ESP_LOGI(TAG, "SNTP init requested (server: pool.ntp.org)");
        } else {
            ESP_LOGI(TAG, "SNTP already running; skipping re-init");
        }
        self.post_event(wifi_conn_state::Connected, self.status_.ssid, self.status_.ip, self.status_.rssi);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        uint16_t n = kMaxAps;
        if (esp_wifi_scan_get_ap_records(&n, self.scan_) == ESP_OK) {
            self.scan_count_ = (int)n;
        } else {
            self.scan_count_ = 0;
        }
        self.scan_inflight_.store(false);
        ESP_LOGI(TAG, "Scan done, %d APs", self.scan_count_);
        // v0.6.6 fix: SCAN_DONE must return state to IDLE so the UI
        // poll can detect the SCANNING->IDLE transition and rebuild the
        // AP list. Without this the state stays SCANNING forever.
        if (self.status_.state == wifi_conn_state::Scanning) {
            self.status_.state = wifi_conn_state::Idle;
        }
        self.post_event(wifi_conn_state::Idle, nullptr, nullptr, 0);  // re-arms UI refresh
    }
}

// ---- connect helper (called from worker task) --------------------------------
void WifiManager::do_connect_locked(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {};
    strncpy(reinterpret_cast<char *>(cfg.sta.ssid), ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy(reinterpret_cast<char *>(cfg.sta.password), pass, sizeof(cfg.sta.password) - 1);
    // PMF required=false: the AP on this user's network doesn't advertise
    // PMF capability (NEWCJIA-8925), and the driver rejects the profile
    // outright if required=true. The pm stop / state run->init disconnect
    // bug is independent of PMF and is addressed by WIFI_PS_NONE below.
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    cfg.sta.pmf_cfg.capable    = true;
    cfg.sta.pmf_cfg.required   = false;
    cfg.sta.scan_method        = WIFI_ALL_CHANNEL_SCAN;

    // v0.6.6 fix: explicitly disconnect before set_config+connect so the
    // driver's internal state machine resets. Without this, a wrong
    // password that left the driver mid-retry causes subsequent scan
    // calls to be silently dropped (busy).
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    set_status_ssid(ssid);
    status_.state = wifi_conn_state::Connecting;
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_connect();
    ESP_LOGI(TAG, "Connect requested: ssid=%s", ssid);

    EventBits_t bits = xEventGroupWaitBits(
        events_, BIT0,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(12000));

    if (bits & BIT0) {
        status_.state = wifi_conn_state::Connected;
        ESP_LOGI(TAG, "Connected to %s", ssid);
    } else {
        // v0.6.6 fix: do NOT call esp_wifi_disconnect() here. Doing so
        // after a successful connect forces the driver back into the
        // auth/assoc state machine and re-triggers the "state: run->init"
        // 10 ms DISASSOC race (reason=8). The next scan cmd path already
        // force-disconnects if needed.
        status_.state = wifi_conn_state::Failed;
        ESP_LOGW(TAG, "Connect to %s failed/timed out", ssid);
    }

    post_event(status_.state, ssid, status_.ip, 0);
}

// ---- worker task loop --------------------------------------------------------
void WifiManager::task_loop()
{
    events_ = xEventGroupCreate();
    evt_queue_ = xQueueCreate(kEventQLen, sizeof(WifiEvent));
    cmd_queue_ = xQueueCreate(kCmdQLen, sizeof(WifiCmd));

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
    // SCAN_DONE is covered by the ESP_EVENT_ANY_ID registration above —
    // do not register it separately or event_handler() fires twice and
    // the second call to esp_wifi_scan_get_ap_records() returns 0.

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));

    // v0.6.6 fix: STA-only mode requires an explicit country before
    // esp_wifi_scan_start() will broadcast probe requests. Without this
    // the scan call returns ESP_OK but never produces a SCAN_DONE event.
    wifi_country_t cc = {};
    cc.cc[0] = 'C'; cc.cc[1] = 'N'; cc.cc[2] = 0;
    cc.schan = 1;
    cc.nchan = 13;
    cc.policy = WIFI_COUNTRY_POLICY_AUTO;
    esp_err_t cc_err = esp_wifi_set_country(&cc);
    ESP_LOGI(TAG, "esp_wifi_set_country(CN) returned %s",
             esp_err_to_name(cc_err));

    // v0.6.6 fix: ESP-IDF v6.0.2 + ESP32-S3 rev 0.2 has a known issue
    // where the driver kicks the AP into "pm stop" ~10 ms after DHCP
    // completes and then DISASSOCs the STA with reason=8. The workaround
    // is to keep power save disabled — esp_wifi_set_ps(WIFI_PS_NONE)
    // must be reasserted after every reconnect, since the driver silently
    // flips it back to MIN_MODEM during connect.
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));
    esp_wifi_set_ps(WIFI_PS_NONE);

    status_.state = wifi_conn_state::Idle;
    strncpy(status_.ssid, "", sizeof(status_.ssid));
    strncpy(status_.ip, "0.0.0.0", sizeof(status_.ip));
    status_.rssi = 0;

    char ssid[33] = {};
    char pass[64] = {};
    load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (ssid[0] != 0) {
        ESP_LOGI(TAG, "Found saved SSID '%s'; auto-connecting", ssid);
        xEventGroupClearBits(events_, BIT0);
        do_connect_locked(ssid, pass);
    } else {
        ESP_LOGI(TAG, "No saved SSID; idle");
    }

    while (true) {
        WifiCmd cmd;
        if (xQueueReceive(cmd_queue_, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd.kind == 0) {
                if (scan_inflight_.load()) {
                    ESP_LOGW(TAG, "Scan already in flight; ignoring");
                    continue;
                }
                // v0.6.6 fix: a bad-password connect can leave the driver
                // mid-retry even after event_handler marks FAILED.
                // esp_wifi_disconnect() guarantees a clean IDLE state so
                // the scan call doesn't bounce back as ESP_ERR_WIFI_STATE.
                if (status_.state == wifi_conn_state::Connecting ||
                    status_.state == wifi_conn_state::Failed) {
                    ESP_LOGW(TAG, "Force-disconnecting before scan");
                    esp_wifi_disconnect();
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                scan_inflight_.store(true);
                status_.state = wifi_conn_state::Scanning;
                wifi_scan_config_t sc = {};
                sc.show_hidden = false;
                sc.scan_type   = WIFI_SCAN_TYPE_ACTIVE;
                esp_err_t err = esp_wifi_scan_start(&sc, false);
                if (err != ESP_OK) {
                    scan_inflight_.store(false);
                    status_.state = wifi_conn_state::Idle;
                    ESP_LOGE(TAG, "esp_wifi_scan_start: %s",
                             esp_err_to_name(err));
                }
            } else if (cmd.kind == 1) {
                persist_credentials(cmd.ssid, cmd.pass);
                xEventGroupClearBits(events_, BIT0);
                do_connect_locked(cmd.ssid, cmd.pass);
            } else if (cmd.kind == 2) {
                esp_wifi_disconnect();
                status_.state = wifi_conn_state::Disconnected;
                strncpy(status_.ip, "0.0.0.0", sizeof(status_.ip));
                post_event(wifi_conn_state::Disconnected, nullptr, nullptr, 0);
            }
        }
    }
}

void WifiManager::task_trampoline(void *arg)
{
    static_cast<WifiManager *>(arg)->task_loop();
    vTaskDelete(nullptr);
}

// ---- public API --------------------------------------------------------------
esp_err_t WifiManager::init()
{
    if (task_) return ESP_OK;
    BaseType_t rc = xTaskCreate(task_trampoline, "wifi_task", 6144, this, 5, &task_);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void WifiManager::get_status(wifi_status &out)
{
    out = status_;
}

esp_err_t WifiManager::scan_start()
{
    if (!cmd_queue_) return ESP_ERR_WIFI_STATE;
    WifiCmd cmd = {};
    cmd.kind = 0;
    if (xQueueSend(cmd_queue_, &cmd, 0) != pdTRUE) {
        return ESP_ERR_WIFI_STATE;
    }
    return ESP_OK;
}

bool WifiManager::is_scan_done() { return !scan_inflight_.load(); }
int  WifiManager::scan_count()   { return scan_count_; }
const wifi_ap_record_t *WifiManager::scan_results() { return scan_; }

esp_err_t WifiManager::connect(const char *ssid, const char *password)
{
    if (!cmd_queue_ || !ssid || !password) return ESP_ERR_INVALID_ARG;
    WifiCmd cmd = {};
    cmd.kind = 1;
    strncpy(cmd.ssid, ssid, sizeof(cmd.ssid) - 1);
    strncpy(cmd.pass, password, sizeof(cmd.pass) - 1);
    if (xQueueSend(cmd_queue_, &cmd, 0) != pdTRUE) {
        return ESP_ERR_WIFI_STATE;
    }
    return ESP_OK;
}

esp_err_t WifiManager::disconnect()
{
    if (!cmd_queue_) return ESP_ERR_WIFI_STATE;
    WifiCmd cmd = {};
    cmd.kind = 2;
    if (xQueueSend(cmd_queue_, &cmd, 0) != pdTRUE) {
        return ESP_ERR_WIFI_STATE;
    }
    return ESP_OK;
}

esp_err_t WifiManager::forget()
{
    NvsHandle h(kNsWifi);
    esp_err_t e1 = h.erase(kKeySsid);
    esp_err_t e2 = h.erase(kKeyPassword);
    h.commit();
    bool ok = (e1 == ESP_OK || e1 == ESP_ERR_NVS_NOT_FOUND)
           && (e2 == ESP_OK || e2 == ESP_ERR_NVS_NOT_FOUND);
    nvs_loaded_ = false;
    nvs_ssid_[0] = 0;
    nvs_password_[0] = 0;
    return ok ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

bool WifiManager::get_saved_credentials(char *out_ssid, size_t ssid_sz,
                                        char *out_pass, size_t pass_sz)
{
    if (!out_ssid || !out_pass || ssid_sz == 0 || pass_sz == 0) return false;
    if (!nvs_loaded_ || nvs_ssid_[0] == 0) {
        out_ssid[0] = 0;
        out_pass[0] = 0;
        return false;
    }
    strncpy(out_ssid, nvs_ssid_, ssid_sz - 1);
    out_ssid[ssid_sz - 1] = 0;
    strncpy(out_pass, nvs_password_, pass_sz - 1);
    out_pass[pass_sz - 1] = 0;
    return true;
}

} // namespace app
