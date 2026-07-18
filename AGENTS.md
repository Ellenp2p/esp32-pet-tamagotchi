# ESP32-Pet — Agent Instructions

## Build & run

```bash
# Build (auto-fetches managed_components on first run)
idf.py build

# Flash (override port: `make flash PORT=COM5`)
idf.py -p COM5 flash

# Monitor
idf.py -p COM5 monitor --print-filter '*:I' --disable-auto-color

# Menuconfig → project settings under "Pet Project Configuration"
idf.py menuconfig

# Convenience
make build flash monitor
```

Windows PS1 wrappers (`build.ps1`, `flash.ps1`) source the Espressif PowerShell profile from `$env:IDF_TOOLS_PATH`; pass `-Port COM5` to `flash.ps1`.

## Project structure

```
CMakeLists.txt          # cmake_minimum_required 3.16, project(esp32-pet)
sdkconfig.defaults      # esp32s3, 16MB flash, 8MB oct PSRAM, NimBLE, LVGL 16bpp
partitions.csv          # factory 3MB + SPIFFS 12MB
main/
  main.cpp              # app_main init order: NVS → I2C → PCA9557 → LCD → Touch
                        #   → QMI8658 → LVGL → pet_ui → BootKey → ScreenPower → AIUsage
  bsp/                  # Singleton BSP drivers: bsp::I2C, bsp::PCA9557, bsp::LCD, etc.
  lvgl/                 # LVGL port (lvgl_app::init)
  app/                  # Business logic in namespace pet
    pet_state.cpp       # State machine (fullness/happy/energy/health, level, coins)
    pet_save.cpp        # NVS auto-save (5s throttle)
    pet_pages.cpp       # Multi-page tab manager (Status/Games/Shop/Settings/AIUsage)
    pet_ui.cpp          # Main UI + pet_task (FreeRTOS, periodic decay + render)
    screen_power.cpp    # Auto-off timer, wake by BOOT key or IMU shake
    ble_pet.cpp         # NimBLE GATT service 0x1234, notify 0x1235, write 0x1236
    wifi_manager.cpp    # Wi-Fi scan/connect/auto-reconnect
    pet_ai_usage.cpp    # 5-min poll of Kimi + MiniMax usage APIs
```

## Key dependencies (idf_component.yml)

- `lvgl/lvgl ~9.4.0` — 16bpp, RGB565A8 (CONFIG_LV_DRAW_SW_SUPPORT_RGB565A8=y)
- `espressif/esp_lvgl_port ~2.6.0`
- `espressif/cjson ~1.7.18` — Kimi/MiniMax JSON parsing
- IDF `>=6.0`, target `esp32s3`

## Config quirks

| Setting | Why |
|---------|-----|
| `CONFIG_LV_COLOR_16_SWAP=n` | ST7789 expects little-endian per-pixel RGB565 |
| `CONFIG_LV_DRAW_SW_SUPPORT_RGB565A8=y` | Sprite compositing on 16bpp display |
| `CONFIG_BT_NIMBLE_ENABLED=y` | BLE via NimBLE (not Bluedroid) |
| `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` | Required for HTTPS AI usage API calls |
| `CONFIG_SPIRAM_MODE_OCT=y` | 8MB octal PSRAM |

API keys (Wi-Fi SSID/password, Kimi, MiniMax) are set via `idf.py menuconfig` → Pet Project Configuration, or passed as env vars: `CONFIG_PET_WIFI_SSID="..." idf.py build`.

## Critical init order (`main.cpp`)

1. `nvs_flash_init()` — must come first
2. `bsp::I2C` → `PCA9557` → `LCD` — hardware bring-up
3. `bsp::Touch` → `QMI8658` — sensors
4. `lvgl_app::init()` — LVGL + touch registration
5. `pet::start_ui()` — creates pet_task FreeRTOS task
6. **`bsp::BootKey` must init before `pet::ScreenPower`** — BootKey ISR queue feeds ScreenPower worker
7. `pet::ai_usage::start()` — no-op if API keys are empty

## LVGL threading

All LVGL API calls outside `lvgl_init.cpp` must be wrapped in:
```cpp
lvgl_port_lock(0);
// ... lvgl calls ...
lvgl_port_unlock();
```

## Compile flags (main/CMakeLists.txt)

`-Wno-missing-field-initializers -Wno-deprecated-enum-enum-conversion`

## Code style

- `.clang-format` in repo root (LLVM-derived, 4-space indent, 120 cols, Allman braces)
- Run `clang-format -i main/**/*.cpp main/**/*.h` to format
- Namespaces: `bsp`, `lvgl_app`, `pet`
- Singleton BSP drivers via `bsp::Foo::instance().init()`

## Tests

No test framework is set up. No `test/` directory.
