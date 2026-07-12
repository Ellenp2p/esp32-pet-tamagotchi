# App Authoring Guide

> 如何给 KOS 加一个新 App。

## 最小模板

新建 `main/kos/kos_app_mynew.h` + `.cpp`:

### mynew.h

```cpp
#pragma once
#include "kos_app.h"

namespace kos {

class AppMyNew : public App {
public:
    AppMyNew() = default;
    ~AppMyNew() override;
    const AppManifest &manifest() override;
    void on_start(AppContext &ctx) override;
    void on_stop() override;
};

}  // namespace kos
```

### mynew.cpp

```cpp
#include "kos_app_mynew.h"
#include "lvgl.h"
#include "esp_log.h"

namespace kos {

static const char *TAG = "app_mynew";
static AppManifest s_manifest = {
    "mynew",         // id (launcher tile 唯一标识)
    "My New App",    // 显示名
    "0.1.0",         // 版本
    0x1976D2,        // icon color
    64,              // 占用 NVS 字节(估算)
};

const AppManifest &AppMyNew::manifest() { return s_manifest; }
AppMyNew::~AppMyNew() {}

void AppMyNew::on_start(AppContext &ctx)
{
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    lv_obj_t *t = lv_label_create(lv_scr_act());
    lv_label_set_text(t, "Hello from MyNew");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);

    // Home button:返回 launcher
    lv_obj_t *home = lv_btn_create(lv_scr_act());
    lv_obj_set_size(home, 36, 22);
    lv_obj_align(home, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_t *lbl = lv_label_create(home);
    lv_label_set_text(lbl, "Home");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(home, [](lv_event_t *e) {
        (void)e;
        registry::launch("launcher");
    }, LV_EVENT_CLICKED, nullptr);
}

void AppMyNew::on_stop()
{
    lv_obj_clean(lv_scr_act());
}

// 静态实例 + 自动注册。文件末尾必须 hand-written(不能 KOS_APP_DEFINE 宏)。
namespace kos {
static AppMyNew _kos_app_inst_kMyNewInst;
const registry::internals::Entry _kos_app_entry_kMyNewInst = {
    "mynew", static_cast<App *>(&_kos_app_inst_kMyNewInst),
};
registry::internals::StaticRegistrar
    _kos_app_regar_kMyNewInst(_kos_app_entry_kMyNewInst);
}

}  // namespace kos
```

### CMakeLists.txt

`main/CMakeLists.txt` 加一行:

```cmake
"kos/kos_app_mynew.cpp"
```

## 进阶

### 持久化

每个 App 自己用 NVS:

```cpp
nvs_handle_t h;
nvs_open("mynew_storage", NVS_READWRITE, &h);
nvs_set_u32(h, "high_score", score);
nvs_commit(h);
nvs_close(h);
```

### Tick

`on_tick(now_ms)` 每 100ms 调一次。可读 input / IMU / BLE 状态,做刷新。

```cpp
void AppMyNew::on_tick(uint32_t now_ms) {
    if (now_ms - last_refresh_ > 500) {
        refresh_ui();
        last_refresh_ = now_ms;
    }
}
```

### 资源 API

- `kos::display::screen_root()` → 拿到全屏 lv_obj_t*
- `kos::input::shake_detected()` → 摇动检测
- `kos::input::clear_shake()` → 清摇动标志
- `kos::registry::launch("pet")` → 切到其他 App
- BLE:暂时不用过 kos::ble,继续用 legacy `pet_ble` 模块

## 注册机制:为什么不用宏

`KOS_APP_DEFINE` 在 GCC + 嵌套 namespace 下 paste 有 bug:
- `__LINE__` 在同一 macro body 内多次 paste 不会重新展开
- 多 token(`##__LINE__` / `##__COUNTER__`)会在第二次 paste 时"黏住"

**实际做法**:
文件末尾手写等价的 3 行(静态实例 + Entry + StaticRegistrar)。
Token 后缀必须 unique(`kMyNewInst`,`kLauncherInst` 等),避免冲突。
