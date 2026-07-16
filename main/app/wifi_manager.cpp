#include "wifi_manager.h"

#include <cstring>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace app {

static const char *TAG = "wifi_manager";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = nullptr;
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = static_cast<wifi_event_sta_disconnected_t *>(event_data);
        ESP_LOGW(TAG, "Disconnected from AP, reason=%d, rssi=%d", event->reason, event->rssi);
        if (s_retry_num < CONFIG_PET_WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to AP (%d/%d)", s_retry_num, CONFIG_PET_WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // v0.6: kick off SNTP once we have an IP. The NTP sync lets
        // PetMeta::check_streak() compute a real "today" day. If SNTP
        // fails, PetMeta skips — no streak change, no false reset.
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        ESP_LOGI(TAG, "SNTP init requested (server: pool.ntp.org)");
    }
}

static void wifi_task(void *arg)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id = nullptr;
    esp_event_handler_instance_t instance_got_ip = nullptr;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        nullptr,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        nullptr,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid),
            CONFIG_PET_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy(reinterpret_cast<char *>(wifi_config.sta.password),
            CONFIG_PET_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 禁用 Wi-Fi 省电模式，避免部分路由器在共存场景下掉线
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "Wi-Fi STA started, connecting to %s", CONFIG_PET_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID:%s", CONFIG_PET_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID:%s", CONFIG_PET_WIFI_SSID);
    } else {
        ESP_LOGW(TAG, "Wi-Fi connection timeout");
    }

    vTaskDelete(nullptr);
}

esp_err_t wifi_manager_init()
{
    if (strlen(CONFIG_PET_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "Wi-Fi SSID not configured, skipping Wi-Fi");
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t rc = xTaskCreate(wifi_task, "wifi_task", 4096, nullptr, 5, nullptr);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

} // namespace app
