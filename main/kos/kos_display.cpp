#include "kos_display.h"

namespace kos {
namespace display {

lv_obj_t *screen_root() { return lv_scr_act(); }

void clear_all()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
}

}  // namespace display
}  // namespace kos
