# KOS 架构

> **K-OS** = K(???) OS 的简称,ES-OS = ESP32-S3 OS。轻量级虚拟 OS。

## 设计目标

1. **App 平台管理**:用户写 App,OS 管理屏幕 / 输入 / 状态切换。
2. **资源抽象**:App 不直接调 BSP,通过 `kos_*` API 访问。
3. **可升级**:当前所有 App 静态链入 OS `.bin`,后续可升级为 SPIFFS 动态 ELF。

## 模块分层

```
+---------------------------------+
|  KOS App (Pet / Gacha / …)      |   ← 用户编写,纯 LVGL + kos_* API
+---------------------------------+
|  KOS Resources (display/input/…) |   ← OS 提供,封装 BSP + LVGL
+---------------------------------+
|  KOS Registry (boot + launch)    |   ← OS 内核
+---------------------------------+
|  BSP / LVGL / NimBLE / NVS       |   ← 硬件层(未抽象)
+---------------------------------+
```

## 资源 API

| API | 用途 | 当前状态 |
|-----|------|---------|
| `kos::display::screen_root()` | 全屏父对象 | ✅ |
| `kos::display::clear_all()`   | 清空子树 | ✅ |
| `kos::input::shake_detected()`| IMU 摇动 | ✅ |
| `kos::registry::launch(id)`   | 切换 App | ✅ |
| `kos::storage::*`             | SPIFFS / NVS 抽象 | 🚧 stub(直接用 NVS) |
| `kos::ble::*`                 | BLE GATT 抽象 | 🚧 stub(仍调 pet::ble_pet) |
| `kos::imu::*`                 | 读加速度 | 🚧 stub(直接 bsp::QMI8658) |
| `kos::audio::play_tone()`     | 音效钩子 | 🚧 stub(只 log) |
| `kos::settings::*`            | 全局设置 | 🚧 NVS 直读 |

## App 生命周期

```
static init:   register -> s_table
boot():        registry::init() -> launch("launcher")
launch(id):    on_pause() old -> on_start(ctx) new
on_tick(ms):   every 100ms
on_stop():     lvgl cleanup
```

## 内存模型

- 所有 App 都是静态对象(全局 `static`),无动态分配。
- LVGL 内存池在 `lvgl_init.cpp` 里全局分配;App 自身不直接 alloc。
- NVS 命名空间:`pet_state`、`gacha_album`、…(每个 App 自管)。
- SPIFFS v0.3.0 暂未用,留作 v0.4.0 App 动态装卸。

## 注册机制详解

每个 `app.cpp` 文件末尾:

```cpp
namespace kos {
static AppPet _kos_app_inst_kPetInst;
const registry::internals::Entry _kos_app_entry_kPetInst = {
    "pet", static_cast<App *>(&_kos_app_inst_kPetInst),
};
registry::internals::StaticRegistrar
    _kos_app_regar_kPetInst(_kos_app_entry_kPetInst);
}
```

- `_kos_app_inst_kPetInst` 是全局静态实例。
- `_kos_app_entry_kPetInst` 描述 `id` + 实例指针。
- `_kos_app_regar_kPetInst` ctor 时把 Entry 推入 `internals::s_table`(`#init_array` 阶段触发)。
- `static const Entry = {...}` 要求 Entry 是 literal-initializable;无 ctor 副作用。

## Tick 调度

`kos_task` FreeRTOS task:

```cpp
while (true) {
    lvgl_port_lock(0);
    input::poll();                     // 读 IMU
    App *cur = registry::current();
    if (cur) cur->on_tick(now_ms);     // 100ms 一次
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

App 在 `on_tick` 里只做轻量更新:读 Pet 状态、写 LVGL label、做 NVS dirty save。
BLE / WiFi 仍在 `pet_ble` 单独 task(因为 notify_state 是 5 字节 1Hz)。

## OTA 通路

- 分区:`nvs` (24KB) + `phy` (4KB) + `otadata` (8KB) + `ota_0` (1.5MB) + `storage` (~14MB)。
- `BOOTLOADER_APP_ROLLBACK_ENABLE=y`,bad OTA 自动滚回旧版本。
- **当前 v0.3.0 还没真正写 OTA 写流程**,只配了分区 + rollback。升级函数 `esp_ota_write()` 调用留作 v0.4.0 实现(可以参考 esp_https_ota 的 sample)。

## 后续路线

- **v0.3.1**:Launcher UI 加 install 真实打通 BLE → 写入 SPIFFS manifest
- **v0.4.0**:SPIFFS `.bin` → 动态 ELF load(实际可装卸 app)
- **v0.4.x**:双 OTA 槽(ota_0 + ota_1),稳态升级
