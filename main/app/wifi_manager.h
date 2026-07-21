#pragma once

#include "esp_err.h"
#include "esp_wifi.h"
#include <cstdint>
#include <atomic>

namespace app {

class WifiManager;

// Connection state enum — declared first so wifi_status can use it.
enum class wifi_conn_state : int {
    Idle       = 0,
    Scanning   = 1,
    Connecting = 2,
    Connected  = 3,
    Failed     = 4,
    Disconnected = 5,
};

// Connection status surfaced for the Settings page.
struct wifi_status {
    wifi_conn_state state;
    char     ssid[33];
    char     ip[16];
    int8_t   rssi;
    uint8_t  pad;
};

// Singleton wrapper around the Wi-Fi subsystem. Owns the event loop, the
// worker task, and all scan/connect state.
class WifiManager {
public:
    static WifiManager &instance() noexcept;

    esp_err_t init();
    void get_status(wifi_status *out);
    esp_err_t scan_start();
    bool      is_scan_done();
    int       scan_count();
    const wifi_ap_record_t *scan_results();
    esp_err_t connect(const char *ssid, const char *password);
    esp_err_t disconnect();
    esp_err_t forget();
    bool      get_saved_credentials(char *out_ssid, size_t ssid_sz,
                                    char *out_pass, size_t pass_sz);

private:
    WifiManager() = default;
    WifiManager(const WifiManager &) = delete;
    WifiManager &operator=(const WifiManager &) = delete;

    // ---- internal types ----
    struct WifiEvent {
        wifi_conn_state kind;
        char     ssid[33];
        char     ip[16];
        int8_t   rssi;
    };
    struct WifiCmd {
        int      kind;  // 0 = scan, 1 = connect, 2 = disconnect
        char     ssid[33];
        char     pass[64];
    };

    static constexpr int kMaxAps = 20;
    static constexpr int kEventQLen = 8;
    static constexpr int kCmdQLen = 4;

    // ---- FreeRTOS task ----
    static void task_trampoline(void *arg);
    void task_loop();

    // ---- helpers ----
    void persist_credentials(const char *ssid, const char *password);
    void load_credentials(char *out_ssid, size_t ssid_sz,
                          char *out_pass, size_t pass_sz);
    void post_event(wifi_conn_state kind, const char *ssid, const char *ip, int8_t rssi);
    void set_status_ssid(const char *ssid);
    void do_connect_locked(const char *ssid, const char *pass);

    static void event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data);

    // ---- state ----
    EventGroupHandle_t  events_        = nullptr;
    QueueHandle_t       evt_queue_     = nullptr;
    QueueHandle_t       cmd_queue_     = nullptr;
    wifi_status         status_        = {};
    std::atomic<bool>   scan_inflight_ {false};
    wifi_ap_record_t    scan_[kMaxAps] = {};
    int                 scan_count_    = 0;
    bool                nvs_loaded_    = false;
    char                nvs_ssid_[33]  = {};
    char                nvs_password_[64] = {};
    TaskHandle_t        task_          = nullptr;
};
} // namespace app
