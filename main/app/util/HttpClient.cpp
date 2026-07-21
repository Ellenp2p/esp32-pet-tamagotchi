#include "HttpClient.h"

#include "esp_crt_bundle.h"
#include "esp_log.h"

#include <cstring>

namespace pet {
namespace util {

static const char *TAG = "HttpClient";

esp_err_t HttpClient::on_event(esp_http_client_event_t *e) noexcept
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    auto *c = static_cast<RxCtx *>(e->user_data);
    if (c->len + e->data_len < c->cap) {
        memcpy(c->buf + c->len, e->data, e->data_len);
        c->len += e->data_len;
    }
    return ESP_OK;
}

HttpResponse HttpClient::fetch(const char *url,
                               const char *bearer,
                               char       *out,
                               size_t      cap) noexcept
{
    HttpResponse resp{};
    out[0] = 0;
    RxCtx ctx{out, static_cast<int>(cap), 0};

    esp_http_client_config_t cfg{};
    cfg.url            = url;
    cfg.timeout_ms     = kTimeoutMs;
    cfg.event_handler  = &HttpClient::on_event;
    cfg.user_data      = &ctx;
    // mbedTLS cert verification via the x509 bundle shipped in
    // sdkconfig.defaults (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE).
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    auto *c = esp_http_client_init(&cfg);
    if (!c) {
        resp.err = ESP_ERR_NO_MEM;
        return resp;
    }

    char auth[200];
    snprintf(auth, sizeof(auth), "Bearer %s", bearer);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type",  "application/json");

    resp.err = esp_http_client_perform(c);
    if (resp.err == ESP_OK) {
        resp.status = esp_http_client_get_status_code(c);
    } else {
        ESP_LOGE(TAG, "perform err=%s url=%s", esp_err_to_name(resp.err), url);
    }
    esp_http_client_cleanup(c);

    if (resp.err != ESP_OK) return resp;
    if (resp.status < 200 || resp.status >= 300) {
        ESP_LOGW(TAG, "HTTP %d url=%s", resp.status, url);
        return resp;
    }
    out[ctx.len] = 0;
    resp.bytes = ctx.len;
    return resp;
}

}  // namespace util
}  // namespace pet
