# 引脚一览

> 数据来源:`main/bsp/bsp_config.h`

## I2C 总线(I2C0)

| 信号 | GPIO |
|------|------|
| SDA   | GPIO 1 |
| SCL   | GPIO 2 |

总线频率:100 kHz。挂载设备:

| 地址 | 设备 |
|------|------|
| 0x19 | PCA9557PW(IO 扩展器) |
| 0x38 | FT5x06(触摸) |
| 0x6A | QMI8658(IMU) |

## SPI 总线(SPI3,用于 LCD)

| 信号 | GPIO |
|------|------|
| MOSI | GPIO 40 |
| CLK  | GPIO 41 |
| DC   | GPIO 39 |
| CS   | NC(由 PCA9557 bit0 控制) |
| RST  | NC(由 PCA9557 控制) |
| BL   | GPIO 42(LEDC 通道 0) |

像素时钟 80 MHz,16-bit RGB565,320×240。

## PCA9557PW(IO 扩展器)

| 位 | 功能 |
|----|------|
| bit0 | LCD_CS(片选) |
| bit1 | PA_EN(功放使能) |
| bit2 | DVP_PWDN(摄像头掉电) |

## BOOT 按键

GPIO 0(低电平按下)。当前未在 main.cpp 中注册回调,仅供后续扩展。

## 备注

- 所有 GPIO 编号对应 ESP32-S3 芯片引脚号,不是开发板丝印编号。
- LCD 没有 RESET 引脚,复位通过 PCA9557 控制。
- LCD CS 也由 PCA9557 控制,GPIO NC 实际不连接。