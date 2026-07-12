// KOS Gacha App —— 把原 pet_game_gacha 包装成 KOS App,加 Home 按钮。
#pragma once

#include "kos_app.h"

namespace kos {

class AppGacha : public App {
public:
    AppGacha() = default;
    ~AppGacha() override;

    const AppManifest &manifest() override;
    void on_start(AppContext &ctx) override;
    void on_stop() override;

private:
    lv_obj_t *gacha_root_ = nullptr;
};

}  // namespace kos
