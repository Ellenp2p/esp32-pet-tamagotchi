#pragma once

#include "esp_err.h"

namespace pet {

class Pet;  // fwd

namespace save {

// Opens NVS namespace "pet_save". Safe to call multiple times.
esp_err_t init();

// Loads the previously saved snapshot into the Pet singleton. Falls back silently
// to defaults if no snapshot exists (first boot). Returns ESP_OK on success.
esp_err_t load(Pet &pet);

// Saves the current Pet state if dirty. Cheap if not dirty.
// pass_save=true forces a flush regardless.
esp_err_t save_if_dirty(Pet &pet, bool force = false);

// Erases the saved snapshot (used by "factory reset").
esp_err_t clear();

}  // namespace save
}  // namespace pet