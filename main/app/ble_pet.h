#pragma once

#include "esp_err.h"

namespace pet_ble {

esp_err_t init();

/**
 * @brief Send a notification of the current pet state to any subscribed peer.
 *
 * Safe to call from any task; does nothing if no peer is subscribed.
 */
void notify_state();

} // namespace pet_ble
