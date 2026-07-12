# ESP32-S3 KOS v0.3.0

> **K-OS**: 一个跑在 [立创·实战派 ESP32-S3](docs/PINOUT.md) 上的轻量级「虚拟 OS」。
> 提供 App 平台管理(注册 / 切换 / 拉起),内建默认 App(Launcher / Pet / Gacha / Settings),后续可通过 OTA 升级。

## ✨ v0.3.0 重头戏

| 方向 | 内容 |
|------|------|
| 🐢 **慢节奏** | 全局衰减速度减半,体感更顺 |
| 🎰 **扭蛋机** | 新增 Gacha App:30 张卡池 × 5 档稀有度 `[60/25/10/4/1]`、`album` 持久化 |
| 🧩 **App 平台** | KOS 框架:Launcher 2×2 网格、Home 切换、App 自注册,空出 OTA 通路 |
| 💾 **OTA 通路** | 16MB Flash 重分区 → otadata + ota_0(1.5MB) + storage(~14MB),sdkconfig 启用 `BOOTLOADER_APP_ROLLBACK_ENABLE` |

## ✨ v0.3.0 内置 App

| App id | 名称 | 颜色 | 说明 |
|--------|------|------|------|
| `launcher` | Launcher | 🟦 | 2×2 应用网格,首页 |
| `pet` | Pet | 🟩 | 状态条 + 4 动作按钮 + 衰减 / 摇晃 |
| `gacha` | Gacha | 🟥 | 30 张卡扭蛋,10c / 抽,album 持久化 |
| `settings` | About | ⬛ | 版本 + 已装 App 列表 |

> KOS 启动后默认在 Launcher。
> 任何 App 都通过 KOS Home 按钮返回。App 状态保留,Pet App 状态由 NVS 自动恢复。

## 🛠 硬件清单(同 v0.2.0)

| 组件 | 型号 | 接口 |
|------|------|------|
| 主控 | ESP32-S3-WROOM-1 (8MB PSRAM, 16MB Flash) | - |
| LCD | ST7789 320×240 | SPI3 |
| 触摸 | FT5x06 电容触摸 | I2C0 |
| 6 轴 IMU | QMI8658 (加速度+陀螺仪) | I2C0 |
| IO 扩展 | PCA9557PW | I2C0 |
| 用户按键 | BOOT 按键 | GPIO0 |

## 📁 目录结构

```
esp32-pet/
├── CMakeLists.txt
├── partitions.csv              # 16MB 自定义分区(otadata + ota_0 + storage)
├── sdkconfig.defaults
├── main/
│   ├── main.cpp                # 板级初始化 + kos::boot()
│   ├── bsp/                    # 板级支持包
│   ├── lvgl/                   # LVGL 端口
│   ├── app/                    # 共享业务模块
│   │   ├── pet_state.{h,cpp}   # 状态机 + 衰减行为(M1 重写)
│   │   ├── pet_save.{h,cpp}    # NVS 持久化
│   │   ├── pet_idle_events.cpp # 60~120s 离线奖励
│   │   ├── pet_game_gacha.{h,cpp}    # Gacha UI / 卡池 / album
│   │   ├── ble_pet.{h,cpp}     # NimBLE GATT
│   ├── kos/                    # KOS 内核(M3)
│   │   ├── kos.{h,cpp}         # boot() / start_task() / tick 调度
│   │   ├── kos_app.h           # App / AppManifest / AppContext 接口
│   │   ├── kos_app_registry.{h,cpp}  # 注册表 + launch()
│   │   ├── kos_display.{h,cpp} # 全屏父对象管理
│   │   ├── kos_input.{h,cpp}   # IMU / shake 检测
│   │   ├── kos_launcher.cpp    # Launcher App
│   │   ├── kos_app_pet.{h,cpp} # 迁过来的 Pet App
│   │   ├── kos_app_gacha.{h,cpp}      # Gacha App
│   │   └── kos_app_settings.{h,cpp}   # About App
└── docs/
    ├── PINOUT.md
    ├── GAMEPLAY.md
    ├── KOS.md                  # KOS 架构(M6)
    └── APP_AUTHORING.md        # 如何写新 App(M6)
```

## 🚀 构建

```bash
./idf.py --version
./idf.py build
./idf.py -p COM5 flash
./idf.py -p COM5 monitor
```

> 注:v0.3.0 改动了分区表(`otadata + ota_0 + storage`)。
> 烧录前请确保已经 `idf.py -p COM5 erase-flash`,否则旧分区可能不识别。
> 当前 user (你) 已经把开发板拔了 —— 明天起来先 `erase-flash` 再 `flash`。

## 🧠 KOS 架构 (v0.3.0 概览)

KOS 是一个「**单一进程静态链接 App 平台**」:

- 所有 App 用 C++ 写,继承 `kos::App`,通过 `kos::AppContext` 拿到屏幕 + tick 时钟。
- App **静态注册**:每个 `app.cpp` 末尾放一个静态实例 + 一个 `kos::registry::internals::StaticRegistrar` 自动注册(`KOS_APP_DEFINE` 在 GCC 嵌套命名空间下有 paste bug,这里手写)。
- `registry::launch(id)` 切换 App;旧的 `on_pause()` → 新的 `on_start(ctx)`。
- 主循环在 FreeRTOS 任务里 100ms 调一次 `current->on_tick(now_ms)`。
- 资源(显示 / 输入 / 存储 / IMU / BLE / 音频)都走 `kos_*` 前缀函数。

后续 v0.4.0 计划:真正从 SPIFFS `.bin` 动态装卸 App。当前是「manifest 在 SPIFFS,代码在 OS .bin 内」—— 数据和代码分离。

完整设计见 [docs/KOS.md](docs/KOS.md)和 [docs/APP_AUTHORING.md](docs/APP_AUTHORING.md)。

## 📡 BLE

v0.3.0 不改 BLE 协议,继续使用 v0.2.0 的 `0x1234/0x1235/0x1236`。
具体命令见 [docs/GAMEPLAY.md](docs/GAMEPLAY.md)。

> 下一步:扩展 `0x1236` 写一个 `I` 命令进入 App 安装模式(接收 SPIFFS App 包)。

## 🎮 玩法

| 关键 | 内容 |
|------|------|
| 玩 Pet | 摇动 / Play 按钮 / Feed / Sleep |
| 玩 Gacha | 10 饼干抽 1 次,5% 概率抽到 Rare+ |
| 升级 | Pet `age_ticks / 300s` 自动 +1 level,Lv ≥ 2 解锁高级内容 |

## 🤝 贡献 & 下一步

- [ ] 真实音效(LEDC PWM 蜂鸣器)
- [ ] SPIFFS 动态装卸 App(ELF 加载)
- [ ] 多 OTA 槽位(ota_0 / ota_1 双分区)
- [ ] Gacha 主题色 / 全屏动画
- [ ] 真实卡片图像资源(BMP / PNG)

## 📜 许可证

[MIT](LICENSE)
