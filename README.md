# ESP32-S3 虚拟电子宠物 v0.7

> 一只跑在 **立创·实战派 ESP32-S3** 开发板上的桌面电子宠物,带 LVGL 9 界面、触摸交互、6 轴体感、Wi-Fi 设置、蓝牙遥控、NVS 存档、熄屏省电。

## ✨ 功能列表

### 核心养成

- **📊 状态仪表盘** — Fullness / Happy / Energy / Health 四条进度条,颜色随数值变化(绿 → 橙 → 红)
- **😊 ASCII 表情脸** — 根据状态自动切换 `(-_-)` / `(>_<)` / `(^o^)` / `(x_x)` 等
- **🪙 货币与等级** — 饼干持久化、自动升级(`age_ticks / 300s = level`),等级解锁新游戏
- **💾 状态持久化** — NVS 自动 5 秒节流保存,断电重启完整恢复

### 页面

- **🎮 Games 页**:两个小游戏
  - **打地鼠**:3 个洞位随机点亮,1.5 秒内点中得 `+5 饼干`
  - **Sequence Tap**(Lv ≥ 2 解锁):3×3 数字格乱序,按 1→9 顺序点完通关 `+30 饼干`
- **🛒 Shop 页**:饼干买小食(Snack/Meal/Treat/Feast 4 档),买完自动喂食
- **⚙️ Settings 页**:Wi-Fi 扫描 / 连接 / 状态 / 密码输入 + **Lock 按钮 + Auto-off(Off / 2 min / 5 min)**
- **🤖 AI Usage 页(v0.6.7)**:每 5 分钟轮询 Kimi + MiniMax API,展示「周」+「5h」两条配额柱,带重置时间

### 输入与交互

- **📳 摇晃交互** — QMI8658 检测甩动 → 玩耍(休眠时唤醒)
- **📳 摇晃亮屏** — 高频 jerk 检测器,拒绝慢速拿起,800ms 内出现正反 Δ 才触发
- **🔘 BOOT 键亮屏** — GPIO0 下降沿 ISR,熄屏时按一下亮屏
- **👆 触摸亮屏** — LVGL `LV_EVENT_PRESSED` 全局 hook(熄屏时无效,因为触摸轮询一并停了)
- **📡 BLE 远程控制** — NimBLE GATT 服务 `0x1234`(特征 `0x1235` 状态通知 / `0x1236` 命令)

### 电源与续航(v0.7)

- **🛌 屏幕自动熄屏** — 空闲 N 分钟无输入 → 关背光 + 停 LVGL tick + 停触摸轮询
- **🪫 默认 2 分钟** — Settings 可切 Off / 2 min / 5 min,持久化到 NVS
- **🔓 唤醒方式** — 摇晃 / BOOT 键 / Settings Lock 主动熄屏
- **🪙 熄屏时不进 light sleep** — 5 分钟一次的 AI 轮询继续跑,pet_task 持续衰减

### 离线奖励

- **🎲 自动找饼干** — 宠物每隔 60~120 秒自己「发现」1~5 个饼干

## 🛠 硬件清单

| 组件 | 型号 | 接口 |
|------|------|------|
| 主控 | ESP32-S3-WROOM-1 (8MB PSRAM, 16MB Flash) | - |
| LCD | ST7789 320×240 | SPI3 + LEDC PWM 背光 (GPIO42) |
| 触摸 | FT5x06 电容触摸 | I2C0 |
| 6 轴 IMU | QMI8658 (加速度+陀螺仪) | I2C0, addr 0x6A |
| IO 扩展 | PCA9557PW | I2C0, addr 0x19 |
| 用户按键 | BOOT 按键 | GPIO0 (RTC IO, light-sleep ext0 唤醒源预留) |

完整引脚映射见 [docs/PINOUT.md](docs/PINOUT.md)。

## 📁 目录结构

```
esp32-pet/
├── build.ps1                   # PowerShell 包装的 idf.py build(Windows Git Bash)
├── flash.ps1                   # PowerShell 包装的 idf.py flash
├── partitions.csv              # 16MB 自定义分区表
├── sdkconfig.defaults          # Kconfig 默认值
├── main/
│   ├── main.cpp                # 板级初始化入口 + BOOT/ScreenPower 启动
│   ├── CMakeLists.txt          # 组件源文件清单
│   ├── Kconfig.projbuild       # Wi-Fi / AI 用量 API key
│   ├── idf_component.yml       # 依赖清单 (lvgl / esp_lvgl_port / cjson)
│   ├── bsp/                    # 板级支持包
│   │   ├── bsp_i2c.cpp         # I2C 总线初始化
│   │   ├── bsp_pca9557.cpp     # IO 扩展器 (LCD CS / PA EN / DVP PWDN)
│   │   ├── bsp_lcd.cpp         # ST7789 驱动 + LEDC 背光
│   │   ├── bsp_touch.cpp       # FT5x06 触摸
│   │   ├── bsp_qmi8658.cpp     # 6 轴 IMU + shake / wake_motion 检测
│   │   └── bsp_key.cpp         # BOOT 键 GPIO0 ISR + xQueue
│   ├── lvgl/
│   │   └── lvgl_init.cpp       # LVGL port + 触摸事件 hook
│   └── app/                    # 业务逻辑
│       ├── pet_state.{h,cpp}   # Pet 状态机 + 行为
│       ├── pet_meta.{h,cpp}    # 元数据(阶段 / 物种等)
│       ├── pet_save.{h,cpp}    # NVS 持久化
│       ├── pet_pages.{h,cpp}   # 多页面管理器
│       ├── pet_idle_events.cpp # 离线奖励调度
│       ├── pet_game_whack.{h,cpp}     # 打地鼠
│       ├── pet_game_sequence.{h,cpp}  # 数字顺序
│       ├── pet_game_gacha.{h,cpp}     # 扭蛋机
│       ├── pet_frames.h        # ASCII 表情帧数据
│       ├── pet_ui.cpp          # 主 UI / Settings 页 / tab 栏 / pet_task
│       ├── ble_pet.{h,cpp}     # BLE GATT 服务
│       ├── wifi_manager.{h,cpp}       # Wi-Fi 扫描 / 连接 / 自动重连
│       ├── pet_ai_usage.{h,cpp}       # AI 用量轮询 worker + UI
│       └── screen_power.{h,cpp}       # 熄屏状态机 + 唤醒处理
├── docs/
│   ├── PINOUT.md               # 引脚一览(中文)
│   └── GAMEPLAY.md             # 玩法说明(中文)
├── .clang-format               # C++ 代码风格配置
├── .editorconfig               # 跨编辑器一致性配置
├── .gitattributes              # Git 属性(行尾、Linguist)
├── Makefile                    # 便携构建入口(跨平台)
├── .github/workflows/          # CI/CD 自动化构建
├── managed_components/         # 拉取的 ESP-IDF 组件(已在 .gitignore)
└── build/                      # 编译产物(已在 .gitignore)
```

## 🚀 构建步骤

### 前置依赖

- [ESP-IDF v6.0.2](https://github.com/espressif/esp-idf/releases) (含 Python 3.11+ / CMake / Ninja)
- ESP32-S3 USB 驱动(CH343 / CP210x)
- 立创·实战派 ESP32-S3 开发板

### Windows + Git Bash(本机已验证)

ESP-IDF v6 在 Git Bash 下会被 `MSYSTEM` 环境变量干扰。仓库根目录提供了 PowerShell 包装:

```powershell
# 1. 编译
./build.ps1

# 2. 烧录
./flash.ps1

# 3. 打开串口监视
idf.py -p COM5 monitor --print-filter *:I --disable-auto-color
```

`build.ps1` 内部做的事:

1. 临时 `Remove-Item Env:MSYSTEM`(Git Bash 泄露的)
2. `source 'C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1'` 激活 v6.0.2
3. `idf.py build` 把日志写到 `build.log`

如果直接调 `idf.py` 在 Git Bash 下不行,几乎都是这个原因。

### Linux / macOS

```bash
source $IDF_PATH/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## ⚙️ 配置项(`idf.py menuconfig` → Pet project defaults)

| 选项 | 默认 | 说明 |
|------|------|------|
| `Wi-Fi SSID` | 空 | 留空 = 不自动连接 |
| `Wi-Fi Password` | 空 | WPA2 PSK |
| `AI Usage → Kimi API Key` | 空 | 留空 = AI Usage tab 完全隐藏 |
| `AI Usage → MiniMax API Key` | 空 | 同上 |

API key 也可以通过 NVS 在运行时注入(`pet_ai_secrets` namespace,key 分别是 `kimi_key` / `minimax_key`),方便 OTA 配发而不重新编译。

也可以通过环境变量一次性传入:

```bash
CONFIG_PET_WIFI_SSID="..." CONFIG_PET_WIFI_PASSWORD="..." \
CONFIG_PET_AI_USAGE_KIMI_KEY="sk-..." \
CONFIG_PET_AI_USAGE_MINIMAX_KEY="ey..." \
idf.py build
```

## 🤖 AI Usage 页(v0.6.7)

每 5 分钟向 Kimi (`https://api.kimi.com/coding/v1/usages`) 和 MiniMax (`https://www.minimaxi.com/v1/token_plan/remains`) 发起 HTTPS 请求,展示两条柱:

- **周窗口** — Kimi `usage.*` / MiniMax `current_weekly_remaining_percent`
- **5h 滑动窗** — Kimi `limits[0].detail.*` / MiniMax `current_interval_remaining_percent`

每条柱的值格式:`X/Y >MM-DD HH:MM`,重置时间用 **CN 本地时间(UTC+8)** 精确到分钟。

- 右上角实时显示 `HH:MM`(16pt)
- 卡片标题 `Kimi` / `MiniMax`(20pt 大字)
- 手动 Refresh 按钮立即触发一次轮询

如果 API key 未配置,整个 tab 在 `pages::set_ai_usage_enabled(false)` 后从主界面移除,**没有 worker 启动、没有 UI 内存**。

## 🛌 熄屏省电(v0.7)

| 触发 | 行为 |
|------|------|
| 自动 | 空闲 2 / 5 分钟(Settings 配)→ 关背光 + `lvgl_port_stop()` + 停触摸轮询 |
| 手动 | Settings 页 `Lock` 按钮 → 立即熄屏 |
| 唤醒 — BOOT 键 | GPIO0 下降沿 ISR → xQueue → `screen_power_task` → `wake_up()` |
| 唤醒 — 摇晃 | QMI8658 高频 jerk 检测器(800ms 内正反 Δmag 各 ≥ 12000 LSB) |
| 唤醒 — 触摸 | 仅在 `lvgl_port_stop()` 之前(熄屏后触摸已停,无效) |

熄屏期间 `pet_task` 持续跑(衰减 / 存档 / BLE notify),AI Usage 5 分钟轮询正常进行(Wi-Fi 保持连接)。

> **不在 v0.7 范围**(后续可选):
> - `esp_light_sleep_start`(需开 `CONFIG_PM_ENABLE` + 改 `WIFI_PS_NONE`)
> - IMU 中断硬件唤醒(需要把 QMI8658 INT1 → RTC GPIO 走线)
> - 触摸中断唤醒(需要把 FT5x06 INT → RTC GPIO 走线)

## 📲 蓝牙遥控

开发板以 `ESP-Pet` 名称广播,使用任意 BLE 调试器(推荐 nRF Connect / LightBlue)连接:

| UUID | 属性 | 说明 |
|------|------|------|
| `0x1234` | Service | 自定义 Pet 服务 |
| `0x1235` | READ + NOTIFY | 5 字节状态通知:`[fullness, happiness, energy, health, sleeping]` |
| `0x1236` | WRITE | 单字节命令:`f` 喂食 / `p` 玩 / `s` 睡 / `w` 醒 / `c` 抚摸 |

> ⚠️ 第 0 字节在 v0.2.0 时从 `hunger`(0=饱 → 100=饿)反转为 `fullness`(0=饿 → 100=饱)。v0.2.0 之前的 BLE 客户端需要反转处理。

## 🎮 玩法

完整说明见 [docs/GAMEPLAY.md](docs/GAMEPLAY.md)。

要点速览:

- 4 根条都是「**高=好**」。Fullness 降到 < 25 时变橙,< 10 变红,提醒你赶紧喂食。
- 摇动开发板 = Play(休眠时 = Wake)。
- **熄屏后**摇一摇或按 BOOT 键可亮屏。
- 60~120 秒后回来检查,可能多了几个饼干。
- 升级后 Sequence Tap 解锁,商店能买更贵的小食。

## 📝 已知限制

- 没有真实音效(蜂鸣器未驱动)。
- 没有动画精灵,只有 ASCII 表情。
- BLE 没有配对绑定(`sm_bonding=0`),谁连都能控制。
- 自动重启会清空 idle event 调度器(下次启动重新随机 60~120 秒)。
- 长时间不喂食 → fullness 归零 → health 持续下降。**别让你的宠物饿死**。
- AI Usage 需要 Wi-Fi 连通,首次请求 TLS 握手约 +800 ms。
- 熄屏到亮屏之间 pet_task / Wi-Fi / BLE 不暂停,功耗节省来自背光和 LVGL tick。

## 🤝 贡献

欢迎 PR!建议的方向:

- 接入真实音效(LEDC PWM 蜂鸣器)
- 加新游戏(贪吃蛇?记忆翻牌?)
- SPIFFS 存档代替 NVS 存历史统计
- 多只宠物并存
- 把 QMI8658 的 INT1 接到 RTC GPIO,实现 shake 真正硬件唤醒

## 📜 许可证

[MIT](LICENSE)

## 🏷 版本历史

- **v0.7** — 熄屏省电 + 摇晃/按键唤醒
- **v0.6.7** — AI Usage 页(Kimi + MiniMax 5min 轮询)
- **v0.6.6** — Wi-Fi 设置 UI(扫描 / 连接 / 密码输入 / status badge)
- **v0.6.0** — 场景化 UI、打工功能、卫生
- **v0.5.1** — 修 status 溢出 + Shop 重设计
- **v0.5.0** — LVGL 9 升级 + 修 sprite 字节布局
- **v0.3.0** — 数值重设计(满格 100 撑 5 小时衰减)
- **v0.2.0** — ASCII 表情脸、5 字节 BLE 协议反转