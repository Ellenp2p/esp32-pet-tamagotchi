// KOS Pet App —— 状态条 + 4 个动作按钮 + 衰减 / 升级。
//
// 直接复用 pet::Pet / pet::save / pet::idle_events,UI 从零建立。
#pragma once

#include "kos_app.h"

namespace kos {

class AppPet : public App {
public:
    AppPet() = default;
    ~AppPet() override;

    const AppManifest &manifest() override;
    void on_start(AppContext &ctx) override;
    void on_pause() override;
    void on_stop() override;
    void on_tick(uint32_t now_ms) override;

private:
    lv_obj_t *face_label_ = nullptr;
    lv_obj_t *status_label_ = nullptr;
    lv_obj_t *bars_[4] = {nullptr};
    lv_obj_t *btn_sleep_ = nullptr;
    bool started_ = false;

    void set_bar_value_with_warn(lv_obj_t *bar, int value);
    void refresh_ui();
};

}  // namespace kos
