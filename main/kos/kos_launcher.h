#pragma once

#include "kos_app.h"

namespace kos {

// Launcher —— KOS 默认启动 App。
// 显示已安装 App 网格 + 一个 “Install via BLE” 占位按钮。
class AppLauncher : public App {
public:
    const AppManifest &manifest() override;
    void on_start(AppContext &ctx) override;
    void on_stop() override;

private:
    lv_obj_t *root_ = nullptr;
    AppContext ctx_{};
};

}  // namespace kos
