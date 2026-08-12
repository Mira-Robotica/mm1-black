/**
 * @file mm1_p4_pins.h
 * @brief Pin map — Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 + MM1 peripherals.
 *
 * On-board signals: Waveshare BSP + schematic.
 * Header peripherals: chosen free GPIOs (16–22 cluster on the 40-pin header).
 */

#pragma once

// ── On-board I2C (GT911, ES8311, ES7210) — legacy driver owned by Display_Panel
#define MM1_I2C_SDA        7
#define MM1_I2C_SCL        8
#define MM1_I2C_FREQ_HZ    400000

// ── Display / touch ─────────────────────────────────────────────────────────
#define MM1_LCD_W          480
#define MM1_LCD_H          800
#define MM1_LCD_RST        27
#define MM1_LCD_BL_PWM     26
#define MM1_LCD_BL_EN      33
#define MM1_TP_RST         23
#define MM1_TP_INT         (-1)

// ── microSD SDIO slot 0 IOMUX + LDO ch4 ─────────────────────────────────────
#define MM1_SD_CLK         43
#define MM1_SD_CMD         44
#define MM1_SD_D0          39
#define MM1_SD_D1          40
#define MM1_SD_D2          41
#define MM1_SD_D3          42
#define MM1_SD_LDO_CHAN    4

// ── Audio (unused by MM1 for now) ───────────────────────────────────────────
#define MM1_I2S_MCLK       13
#define MM1_I2S_SCLK       12
#define MM1_I2S_LCLK       10
#define MM1_I2S_DOUT       9
#define MM1_I2S_DSIN       11
#define MM1_AMP_EN         53

// ── MM1 on 40-pin header ────────────────────────────────────────────────────
/* BNO086 on Wire1 (not the touch bus): Display_Panel owns I2C0 with the legacy
 * driver; Wire uses i2c_master and the two cannot share a bus. Wire IMU to these
 * pins (3V3 + GND). Address still 0x4B. */
#define MM1_IMU_ADDR       0x4B
#define MM1_IMU_SDA        16
#define MM1_IMU_SCL        17
#define MM1_IMU_INT        18
#define MM1_IMU_RST        (-1)

#define MM1_LZR_UART_NUM   1
#define MM1_LZR_RX         21   // module TX → ESP RX
#define MM1_LZR_TX         20   // module RX → ESP TX
#define MM1_LZR_ENA        (-1)

#define MM1_USER_BUTTON    19   // active low, INPUT_PULLUP

#define MM1_BAT_ADC        (-1) // LiPo sense not exposed on this carrier
