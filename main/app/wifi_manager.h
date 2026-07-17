#pragma once

#include "esp_err.h"
#include "esp_wifi.h"
#include <cstdint>

namespace app {

// Lifecycle: starts the wifi_task (which initialises esp_netif / esp_wifi
// in STA mode, sets up event handlers, and waits for an IP). Always
// succeeds — even with no SSID configured the wifi subsystem is brought
// up so that runtime scan/connect can work.
esp_err_t wifi_manager_init();

// Connection status surfaced for the Settings page. (1) current state of
// the link (enum, below) and (2) the AP we're associated with (empty
// when disconnected). `ip` is the last IP we got from IP_EVENT_STA_GOT_IP
// or 0.0.0.0 if not connected yet. Caller passes a wifi_status_t* buffer.
struct wifi_status {
    int      state;       // matches enum wifi_conn_state below
    char     ssid[33];    // 32 + NUL, matches wifi_config_t.ssid length
    char     ip[16];      // "255.255.255.255" or "0.0.0.0"
    int8_t   rssi;        // last seen RSSI (0 if unknown)
    uint8_t  pad;         // align
};
enum wifi_conn_state {
    WIFI_CONN_IDLE       = 0,  // wifi up, not configured
    WIFI_CONN_SCANNING   = 1,  // scan in progress
    WIFI_CONN_CONNECTING  = 2,  // STA in the process of associating
    WIFI_CONN_CONNECTED   = 3,  // IP acquired
    WIFI_CONN_FAILED      = 4,  // last connect attempt failed
    WIFI_CONN_DISCONNECTED= 5,  // explicit disconnect
};
void wifi_manager_get_status(wifi_status *out);

// Trigger a Wi-Fi scan. The result list is updated asynchronously; poll
// wifi_manager_is_scan_done() afterwards. Returns ESP_OK if the scan was
// accepted; ESP_ERR_WIFI_STATE if a scan is already in flight.
esp_err_t wifi_manager_scan_start();
bool      wifi_manager_is_scan_done();
int       wifi_manager_scan_count();   // 0..WIFI_MANAGER_MAX_APS
const wifi_ap_record_t *wifi_manager_scan_results();  // array of length scan_count()

// Connect / disconnect. Both schedule a deferred operation on the
// wifi_task; the connection_state in wifi_status reflects the progress
// (CONNECTING → CONNECTED or FAILED). On connect, the new credentials are
// persisted to NVS so the next boot reconnects automatically.
esp_err_t wifi_manager_connect(const char *ssid, const char *password);
esp_err_t wifi_manager_disconnect();
esp_err_t wifi_manager_forget();  // erase the saved NVS credentials

} // namespace app