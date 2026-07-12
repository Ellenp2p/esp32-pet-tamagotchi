#pragma once

#include "lvgl.h"

namespace kos {
namespace display {

// 全屏父对象 —— 所有 App 把 UI 挂到这个上。
lv_obj_t *screen_root();

// 清空当前屏幕内容(删除所有子对象)。App 切换时使用。
void clear_all();

}  // namespace display
}  // namespace kos
