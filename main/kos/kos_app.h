#pragma once

#include "lvgl.h"
#include <cstdint>

namespace kos {

// App 元数据,由 manifest() 返回。
struct AppManifest {
    const char *id;            // "pet", "gacha", "launcher", ...
    const char *name;          // 显示名
    const char *version;       // "0.3.0"
    uint32_t    icon_color;    // Launcher 中图标的背景色(LVGL hex)
    uint32_t    min_storage;   // 该 app 自身在 SPIFFS 中占用的最小空间(估算)
};

// App 运行时由 OS 提供的“上下文”。本质上是 OS 给 App 的视图。
struct AppContext {
    lv_obj_t *screen_root;     // 全屏父对象,App 把 widget 都挂在这里
    uint32_t  now_ms;          // 当前 time tick (ms)

    // 该 App 主动切换到另一个 App(由按钮回调调用)。
    // 可以填 nullptr 表示不切换。
    void (*request_launch)(const char *app_id);
};

// App 基类 —— 所有内嵌 App 都继承该类。
//
// on_start:首次启动 LVGL 创建。
// on_resume:从挂起恢复(允许重新刷新 UI)。
// on_pause:离开前保存状态。
// on_stop:彻底销毁 LVGL 子树 + 释放资源。
// on_tick:每 100ms 调一次,可读输入/IMU/update UI。
class App {
public:
    virtual ~App() = default;
    virtual const AppManifest &manifest() = 0;
    virtual void on_start(AppContext &ctx) = 0;
    virtual void on_resume(AppContext &ctx) { (void)ctx; }
    virtual void on_pause() {}
    virtual void on_stop() {}
    virtual void on_tick(uint32_t now_ms) { (void)now_ms; }
};

}  // namespace kos
