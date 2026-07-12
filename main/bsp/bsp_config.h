#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"

/******************************************************************************/
/***************************  I2C *********************************************/
#define BSP_I2C_SDA           (GPIO_NUM_1)
#define BSP_I2C_SCL           (GPIO_NUM_2)
#define BSP_I2C_NUM           (0)
#define BSP_I2C_FREQ_HZ       (100000)

/*******************************************************************************/
/***************************  QMI8658A  ***************************************/
#define QMI8658_SENSOR_ADDR   (0x6A)

/******************************************************************************/
/***************************  PCA9557  ****************************************/
#define PCA9557_SENSOR_ADDR   (0x19)
#define PCA9557_INPUT_PORT              0x00
#define PCA9557_OUTPUT_PORT             0x01
#define PCA9557_POLARITY_INVERSION_PORT 0x02
#define PCA9557_CONFIGURATION_PORT      0x03

#define LCD_CS_GPIO                 BIT(0)
#define PA_EN_GPIO                  BIT(1)
#define DVP_PWDN_GPIO               BIT(2)

/******************************************************************************/
/***************************  LCD  ********************************************/
#define BSP_LCD_PIXEL_CLOCK_HZ     (80 * 1000 * 1000)
#define BSP_LCD_SPI_NUM            (SPI3_HOST)
#define LCD_CMD_BITS               (8)
#define LCD_PARAM_BITS             (8)
#define BSP_LCD_BITS_PER_PIXEL     (16)
#define LCD_LEDC_CH                LEDC_CHANNEL_0

#define BSP_LCD_H_RES              (320)
#define BSP_LCD_V_RES              (240)

#define BSP_LCD_SPI_MOSI      (GPIO_NUM_40)
#define BSP_LCD_SPI_CLK       (GPIO_NUM_41)
#define BSP_LCD_SPI_CS        (GPIO_NUM_NC)
#define BSP_LCD_DC            (GPIO_NUM_39)
#define BSP_LCD_RST           (GPIO_NUM_NC)
#define BSP_LCD_BACKLIGHT     (GPIO_NUM_42)

#define BSP_LCD_DRAW_BUF_HEIGHT    (20)

/******************************************************************************/
/***************************  BOOT KEY  ***************************************/
#define BSP_BOOT_KEY_GPIO     (GPIO_NUM_0)
