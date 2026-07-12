// KOS Settings App —— 显示版本 + 系统信息 + Home 按钮。
#pragma once

#include "kos_app.h"

namespace kos {

class AppSettings : public App {
public:
    AppSettings() = default;
    ~AppSettings() override;

    const AppManifest &manifest() override;
    void on_start(AppContext &ctx) override;
    void on_stop() override;
};

}  // namespace kos
