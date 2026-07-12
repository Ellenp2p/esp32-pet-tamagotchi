// KOS — 小型 ESP32 OS 的公共头
//
// kos 是轻量级的“操作系统”抽象,本质是:
//   - 一个 App 注册表 + 状态机
//   - 一组 *资源 API**(display / input / storage / imu / ble / audio / settings)
//   - 一层最薄的 LVGL / BSP 适配
//
// 所有 App 通过 kos_app interface 接入,与 OS 共享同一进程地址空间。
// 后续可以扩展为动态加载 ELF(SPIFFS),但 v0.3.0 仅做静态链接版本。
#pragma once

#include <cstdint>

namespace kos {

// 启动整个 KOS。需要在 LVGL/BSP/NVS 初始化完毕之后调用。
//
// 内部会:
//   1. 扫描所有静态注册的 App(通过 KOS_APP_DEFINE 宏)
//   2. 默认启动 Launcher App
//
// 调用前必须持有 LVGL port lock。
void boot();

// KOS 主循环:每 100ms tick 调度活跃 App + 检查输入 / IMU / BLE。
//
// 该函数不会返回 —— 内部是 while(true) + vTaskDelay。
// 实际上被包装成 FreeRTOS task(参见 kos.cpp::kos_task)。
void loop_tick();

// 启动整个 KOS 任务(INTERNAL):用于在 boot 后启动 FreeRTOS task。
// 调用前必须持有 LVGL port lock。
void start_task();

// 当前活动 App 的 id(launcher / pet / gacha / settings)。
const char *active_app_id();

// 内部使用 —— 由 registry 在切换 App 时调用。
void set_active_id(const char *id);

}  // namespace kos
