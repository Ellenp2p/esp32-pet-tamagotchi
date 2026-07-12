#pragma once

#include "lvgl.h"

namespace pet {
namespace game_whack {

// Build the Whack-a-Mole UI as a child of `parent`. Returns the root widget so
// the page system can destroy it on page switch.
lv_obj_t *build(lv_obj_t *parent);

// Destroy the UI and free timers. Called by pet_pages when the Games page is
// torn down or the player leaves mid-round.
void destroy(lv_obj_t *root);

}  // namespace game_whack
}  // namespace pet