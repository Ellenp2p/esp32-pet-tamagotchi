#pragma once

#include "nvs.h"
#include "nvs_flash.h"

#include <cstring>
#include <string>
#include <type_traits>

// v0.8 Phase 1: NvsStorage<T> header-only template. Built and
// compile-checked this phase; not yet wired into any consumer
// (pet_save.cpp / screen_power.cpp / wifi_manager.cpp). Phase 4 will
// adopt it where the existing nvs_open/get/set/commit boilerplate
// can be replaced.
//
// For trivially-copyable T, the value is stored as an NVS blob
// (sizeof(T) bytes). std::string is stored as an NVS string.
//
// The template assumes T is default-constructible (load() writes
// into a caller-owned T by overwriting the bytes).

template <class T>
class NvsStorage {
    static_assert(!std::is_reference_v<T>, "T must not be a reference");
    static_assert(!std::is_const_v<T>,    "T must not be const");
public:
    NvsStorage(const char *ns, const char *key) noexcept
        : ns_(ns), key_(key) {}

    // Stores the value. Returns true on success.
    bool save(const T &value) noexcept {
        nvs_handle_t h;
        if (nvs_open(ns_, NVS_READWRITE, &h) != ESP_OK) return false;
        bool ok;
        if constexpr (std::is_same_v<T, std::string>) {
            ok = nvs_set_str(h, key_, value.c_str()) == ESP_OK;
        } else {
            static_assert(std::is_trivially_copyable_v<T>,
                          "T must be trivially copyable (or specialise)");
            ok = nvs_set_blob(h, key_, &value, sizeof(T)) == ESP_OK;
        }
        if (ok) ok = nvs_commit(h) == ESP_OK;
        nvs_close(h);
        return ok;
    }

    // Loads the value into *out. Returns true on success.
    bool load(T *out) noexcept {
        nvs_handle_t h;
        if (nvs_open(ns_, NVS_READONLY, &h) != ESP_OK) return false;
        bool ok;
        if constexpr (std::is_same_v<T, std::string>) {
            size_t needed = 0;
            if (nvs_get_str(h, key_, nullptr, &needed) != ESP_OK) {
                nvs_close(h); return false;
            }
            std::string tmp(needed, '\0');
            if (nvs_get_str(h, key_, tmp.data(), &needed) != ESP_OK) {
                nvs_close(h); return false;
            }
            // nvs_get_str writes the null terminator inside `needed`
            // bytes; pop any trailing null that std::string added.
            while (!tmp.empty() && tmp.back() == '\0') tmp.pop_back();
            *out = std::move(tmp);
            ok = true;
        } else {
            size_t len = sizeof(T);
            ok = nvs_get_blob(h, key_, out, &len) == ESP_OK
                 && len == sizeof(T);
        }
        nvs_close(h);
        return ok;
    }

    // Erases the key. Returns true on success (or if the key was
    // already absent).
    bool erase() noexcept {
        nvs_handle_t h;
        if (nvs_open(ns_, NVS_READWRITE, &h) != ESP_OK) return false;
        bool ok = nvs_erase_key(h, key_) == ESP_OK
                  || nvs_erase_key(h, key_) == ESP_ERR_NVS_NOT_FOUND;
        if (ok) ok = nvs_commit(h) == ESP_OK;
        nvs_close(h);
        return ok;
    }

private:
    const char *ns_;
    const char *key_;
};