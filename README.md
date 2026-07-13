# ESP32-S3 虚拟电子宠物 v0.2.0

> 一只跑在 **立创·实战派 ESP32-S3** 开发板上的桌面电子宠物,带 LVGL 界面、触摸交互、6 轴体感、蓝牙遥控、NVS 存档。

## ✨ 功能列表

- **📊 状态仪表盘**:Fullness / Happy / Energy / Health 四条进度条,颜色随数值变化(绿 → 橙 → 红)
- **😊 ASCII 表情脸**:根据状态自动切换 `(-_-)` / `(>_<)` / `(^o^)` / `(x_x)` 等
- **🎮 多页面切换**:底部 Tab 栏 × 4(状态 / 游戏 / 商店 / 关于)
- **🎯 两个小游戏**:
  - **打地鼠**(默认解锁):3 个洞位随机点亮,1.5 秒内点中得 `+5 饼干`,Miss 扣 `1 饼干`
  - **Sequence Tap**(Lv ≥ 2 解锁):3×3 数字格乱序,按 1→9 顺序点完通关 `+30 饼干`
- **🛒 商店**:饼干买小食(Snack/Meal/Treat/Feast 4 档),买完自动喂食
- **🎲 离线奖励**:宠物每隔 60~120 秒自己「发现」1~5 个饼干
- **💤 作息节律**:Sleep/Wake 按钮控制状态,睡眠时减饥饿、加速回精力
- **🪙 货币与等级**:饼干持久化、自动升级(`age_ticks / 300s = level`)
- **📳 摇晃交互**:QMI8658 检测甩动 → 玩耍(休眠时唤醒),自适应阈值避免误触
- **📡 BLE 远程控制**:NimBLE GATT 服务 `0x1234`,特征 `0x1235`(状态通知)/ `0x1236`(命令写入)
- **💾 状态持久化**:NVS 自动 5 秒节流保存,断电重启完整恢复

## 🛠 硬件清单

| 组件 | 型号 | 接口 |
|------|------|------|
| 主控 | ESP32-S3-WROOM-1 (8MB PSRAM, 16MB Flash) | - |
| LCD | ST7789 320×240 | SPI3 |
| 触摸 | FT5x06 电容触摸 | I2C0 |
| 6 轴 IMU | QMI8658 (加速度+陀螺仪) | I2C0, addr 0x6A |
| IO 扩展 | PCA9557PW | I2C0, addr 0x19 |
| 用户按键 | BOOT 按键 | GPIO0 |

完整引脚映射见 [docs/PINOUT.md](docs/PINOUT.md)。

## 📁 目录结构

```
esp32-pet/
├── CMakeLists.txt              # 根 CMake
├── dependencies.lock           # 依赖锁定
├── partitions.csv              # 16MB 自定义分区表
├── sdkconfig.defaults          # Kconfig 默认值
├── idf.py                      # Windows/MSYS 的 idf.py 包装
├── monitor_read.py             # 简易串口读取脚本(45s)
├── main/
│   ├── main.cpp                # 板级初始化入口
│   ├── bsp/                    # 板级支持包(I2C / LCD / Touch / IMU / PCA9557)
│   ├── lvgl/                   # LVGL 端口初始化
│   └── app/                    # 业务逻辑
│       ├── pet_state.{h,cpp}   # Pet 状态机 + 行为
│       ├── pet_save.{h,cpp}    # NVS 持久化
│       ├── pet_pages.{h,cpp}   # 多页面管理器
│       ├── pet_ui.{cpp}        # Status / Shop / About 页面 + tab 栏
│       ├── pet_game_whack.{h,cpp}     # 打地鼠游戏
│       ├── pet_game_sequence.{h,cpp}  # 数字顺序游戏
│       ├── pet_idle_events.cpp        # 离线奖励调度
│       ├── ble_pet.{h,cpp}     # BLE GATT 服务
│       └── wifi_manager.{h,cpp}        # Wi-Fi 留作扩展
├── docs/
│   ├── PINOUT.md               # 引脚一览(中文)
│   └── GAMEPLAY.md             # 玩法说明(中文)
└── managed_components/         # 拉取的 ESP-IDF 组件(已在 .gitignore)
```

## 🚀 构建步骤

### 前置依赖

- [ESP-IDF v6.0.2](https://github.com/espressif/esp-idf/releases) (含 Python 3.11+ / CMake / Ninja)
- ESP32-S3 USB 驱动(CH343 / CP210x)
- 立创·实战派 ESP32-S3 开发板

### Windows + Git Bash(本机已验证)

本仓库根目录的 `idf.py` 包装脚本处理了 ESP-IDF 在 `C:/Espressif/tools` + git-bash MSYS 下的非标准路径问题。**直接用**:

```bash
# 1. 拉取组件(首次)
./idf.py --version

# 2. 编译
./idf.py build

# 3. 烧录(开发板插上 USB,COM 端口按机器调整)
./idf.py -p COM5 flash

# 4. 打开串口监视
./idf.py -p COM5 monitor
# 或者用本仓库自带的 45 秒脚本(自动拉低 DTR 复位)
python monitor_read.py
```

### Linux / macOS

```bash
source $IDF_PATH/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

> 注:在 Linux 下 `monitor_read.py` 需要修改 `COM5` 为 `/dev/ttyUSB0`。

## ⚙️ 配置 Wi-Fi(可选,默认未连接)

`sdkconfig.defaults` 中 Wi-Fi 凭证已置空。需要联网时:

```bash
./idf.py menuconfig
# 导航到: Pet project defaults  --->  Wi-Fi SSID / Password
./idf.py build flash
```

或通过环境变量一次性传入:

```bash
CONFIG_PET_WIFI_SSID="你的 SSID" CONFIG_PET_WIFI_PASSWORD="你的密码" ./idf.py build
```

> Wi-Fi 模块当前**未在 main.cpp 启动**,只保留作为后续扩展(联网解锁 / OTA)的接口。

## 📲 蓝牙遥控

开发板以 `ESP-Pet` 名称广播,使用任意 BLE 调试器(推荐 nRF Connect / LightBlue)连接:

| UUID | 属性 | 说明 |
|------|------|------|
| `0x1234` | Service | 自定义 Pet 服务 |
| `0x1235` | READ + NOTIFY | 5 字节状态通知:`[fullness, happiness, energy, health, sleeping]` |
| `0x1236` | WRITE | 单字节命令:`f` 喂食 / `p` 玩 / `s` 睡 / `w` 醒 / `c` 抚摸 |

> ⚠️ **协议变更(v0.2.0)**:第 0 字节语义从 "hunger"(0=饱 → 100=饿)反转为 "fullness"(0=饿 → 100=饱)。**所有 v0.2.0 之前版本的 BLE 客户端需要反转处理第 0 字节**。下一个版本会引入 `protocol_version` 字节前导以彻底解决兼容。

## 🎮 玩法

完整说明见 [docs/GAMEPLAY.md](docs/GAMEPLAY.md)。

要点速览:

- 4 根条都是「**高=好**」。Fullness 降到 < 25 时变橙,< 10 变红,提醒你赶紧喂食。
- 摇动开发板 = Play(休眠时=Wake)。
- 60~120 秒后回来检查,可能多了几个饼干。
- 升级后 Sequence Tap 解锁,商店能买更贵的小食。

## 📝 已知限制

- 没有真实音效(蜂鸣器未驱动)。
- 没有动画精灵,只有 ASCII 表情。
- BLE 没有配对绑定(`sm_bonding=0`),谁连都能控制。
- 自动重启会清空 idle event 调度器(下次启动重新随机 60~120 秒)。
- 长时间不喂食 → fullness 归零 → health 持续下降。**别让你的宠物饿死**。

## 🤝 贡献

欢迎 PR!建议的方向:
- 接入真实音效(LEDC PWM 蜂鸣器)
- 加新游戏(贪吃蛇?记忆翻牌?)
- SPIFFS 存档代替 NVS 存历史统计
- 多只宠物并存

## 📜 许可证

[MIT](LICENSE)