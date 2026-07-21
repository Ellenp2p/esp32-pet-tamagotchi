#pragma once

#include "esp_err.h"
#include "esp_http_client.h"

#include <cstddef>

namespace pet {
namespace util {

// Lightweight wrapper around esp_http_client for one-shot JSON GETs.
//
// Each call is independent — no client handle is kept between calls,
// so callers can rebuild the URL or change headers without explicit
// teardown. Designed for low-frequency use (5-minute polling); not
// suitable for streaming or pipelined requests.
struct HttpResponse {
    int      status = 0;
    esp_err_t err   = ESP_OK;
    int      bytes = 0;

    bool ok() const noexcept {
        return err == ESP_OK && status >= 200 && status < 300;
    }
};

class HttpClient {
public:
    HttpClient() noexcept = default;

    // Performs a single HTTPS GET to `url` with a Bearer token in the
    // Authorization header. Response body is written into `out` up to
    // `cap - 1` bytes, then NUL-terminated. Returns the result struct;
    // caller checks `.ok()`.
    HttpResponse fetch(const char *url,
                      const char *bearer,
                      char       *out,
                      size_t      cap) noexcept;

private:
    struct RxCtx {
        char  *buf;
        int    cap;
        int    len;
    };

    static esp_err_t on_event(esp_http_client_event_t *e) noexcept;

    static constexpr int kTimeoutMs = 8000;  // matches prior HTTP_TIMEOUT_MS
};

}  // namespace util
}  // namespace pet
