#pragma once

#include "lvgl.h"

namespace pet {
namespace game_sequence {

// Sequence-tap game: 9 tiles labeled 1..9 in shuffled order. Player taps them
// in numeric order to clear the round. Unlocked at level >= 2.
lv_obj_t *build(lv_obj_t *parent);

// Tear down UI + free timers.
void destroy(lv_obj_t *root);

}  // namespace game_sequence
}  // namespace pet