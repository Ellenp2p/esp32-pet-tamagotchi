#pragma once

#include "esp_err.h"

namespace pet {

class Pet;  // fwd

namespace save {

// Opens NVS namespace "pet_save". Safe to call multiple times.
esp_err_t init();

// Loads the previously saved snapshot into the Pet singleton. Falls back
// silently to defaults if no snapshot exists (first boot). Tries the v2
// key first (state_v2, 41 bytes), then the legacy v1 key (state, 29 bytes)
// for forward compatibility with v0.5.x save files.
esp_err_t load(Pet &pet);

// Saves the current Pet state if dirty. Cheap if not dirty.
// force=true forces a flush regardless. Always writes to state_v2.
esp_err_t save_if_dirty(Pet &pet, bool force = false);

// Erases both v1 and v2 snapshot keys (factory reset).
esp_err_t clear();

}  // namespace save
}  // namespace pet