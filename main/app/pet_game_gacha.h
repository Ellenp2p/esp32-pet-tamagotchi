#pragma once

#include "lvgl.h"

namespace pet {
namespace game_gacha {

// Build the Gacha UI as a child of `parent`. Returns the root widget.
lv_obj_t *build(lv_obj_t *parent);

// Tear down UI + free timers + persist album to NVS.
void destroy(lv_obj_t *root);

// Pull one card from the gacha pool. Used by build()'s PULL button and exposed
// for BLE / debug. Returns card rarity index 0..4.
int pull_one();

}  // namespace game_gacha
}  // namespace pet