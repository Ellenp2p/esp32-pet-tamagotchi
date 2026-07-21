#pragma once

#include "nvs.h"
#include "nvs_flash.h"

#include <cstdint>
#include <cstring>

// RAII wrapper around nvs_handle_t. Opens in constructor, closes in
// destructor. Non-copyable, move-enabled. All methods forward directly
// to the NVS C API.
//
// Usage:
//   {
//       NvsHandle h("my_ns");
//       uint8_t v;
//       if (h.get_u8("my_key", &v) == ESP_OK) { ... }
//       h.set_u8("my_key", 42);
//       h.commit();
//   }  // auto-closes

class NvsHandle {
public:
    explicit NvsHandle(const char *ns,
                       nvs_open_mode_t mode = NVS_READWRITE) noexcept
    {
        nvs_open(ns, mode, &h_);
    }

    ~NvsHandle() noexcept
    {
        if (h_) nvs_close(h_);
    }

    NvsHandle(const NvsHandle &) = delete;
    NvsHandle &operator=(const NvsHandle &) = delete;

    NvsHandle(NvsHandle &&other) noexcept : h_(other.h_)
    {
        other.h_ = 0;
    }

    NvsHandle &operator=(NvsHandle &&other) noexcept
    {
        if (this != &other) {
            if (h_) nvs_close(h_);
            h_ = other.h_;
            other.h_ = 0;
        }
        return *this;
    }

    explicit operator bool() const noexcept { return h_ != 0; }

    esp_err_t get_u8(const char *key, uint8_t *v) const noexcept
    {
        return h_ ? nvs_get_u8(h_, key, v) : ESP_ERR_NVS_NOT_INITIALIZED;
    }

    esp_err_t set_u8(const char *key, uint8_t v) noexcept
    {
        return h_ ? nvs_set_u8(h_, key, v) : ESP_ERR_NVS_NOT_INITIALIZED;
    }

    esp_err_t get_i32(const char *key, int32_t *v) const noexcept
    {
        return h_ ? nvs_get_i32(h_, key, v) : ESP_ERR_NVS_NOT_INITIALIZED;
    }

    esp_err_t set_i32(const char *key, int32_t v) noexcept
    {
        return h_ ? nvs_set_i32(h_, key, v) : ESP_ERR_NVS_NOT_INITIALIZED;
    }

    esp_err_t get_str(const char *key, char *buf, size_t *sz) const noexcept
    {
        return h_ ? nvs_get_str(h_, key, buf, sz) : ESP_ERR_NVS_NOT_INITIALIZED;
    }

    esp_err_t set_str(const char *key, const char *v) noexcept
    {
        return h_ ? nvs_set_str(h_, key, v) : ESP_ERR_NVS_NOT_INITIALIZED;
    }

    esp_err_t get_blob(const char *key, void *buf, size_t *sz) const noexcept
    {
        return h_ ? nvs_get_blob(h_, key, buf, sz) : ESP_ERR_NVS_NOT_INITIALIZED;
    }

    esp_err_t set_blob(const char *key, const void *v, size_t sz) noexcept
    {
        return h_ ? nvs_set_blob(h_, key, v, sz) : ESP_ERR_NVS_NOT_INITIALIZED;
    }

    esp_err_t erase(const char *key) noexcept
    {
        return h_ ? nvs_erase_key(h_, key) : ESP_ERR_NVS_NOT_INITIALIZED;
    }

    esp_err_t commit() noexcept
    {
        return h_ ? nvs_commit(h_) : ESP_ERR_NVS_NOT_INITIALIZED;
    }

private:
    nvs_handle_t h_ = 0;
};
