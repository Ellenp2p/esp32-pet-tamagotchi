#pragma once

#include "esp_err.h"
#include <cstdint>

/* Forward declarations of NimBLE types (at global scope so they don't
   become pet_ble::ble_gap_event etc.) */
struct ble_gap_event;
struct ble_gatt_register_ctxt;
struct ble_gatt_access_ctxt;
struct os_mbuf;

namespace pet_ble {

// Singleton wrapper around the NimBLE GATT service (0x1234).
// Owns the BLE host task, GATT service definitions, and connection state.
class BlePet {
public:
    static BlePet &instance() noexcept;

    esp_err_t init();
    void notify_state();

    // Callbacks exposed for the file-level static gatt_svcs[] table
    // (NimBLE C API constraint — must be addressable from a static
    // initializer at file scope).
    static int pet_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
    static int pet_gap_event(struct ble_gap_event *event, void *arg);
    static void pet_on_sync(void);
    static void pet_on_reset(int reason);
    static void pet_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
    static void ble_host_task(void *param);

private:
    BlePet() = default;
    BlePet(const BlePet &) = delete;
    BlePet &operator=(const BlePet &) = delete;

    void update_state_mbuf(struct os_mbuf *om);
    void advertise();

    // ---- state ----
    uint16_t conn_handle_ = 0xFFFF;
    bool notify_subscribed_ = false;
};

} // namespace pet_ble
