/**
 * @file mm1_p4_pins.h
 * @brief Pin map — Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 + MM1 peripherals.
 *
 * On-board signals: Waveshare BSP + schematic
 *   docs/datasheets/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf
 *
 * J3 40-pin silk labels are ESP32-P4 GPIO numbers (SDA/SCL = GPIO7/8).
 * GPIOs 16–20 are reserved for the ESP32-C6 SDIO link — not on J3.
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

// ── Audio (ES8311 path in p4_boot; not a GPIO piezo) ───────────────────────
#define MM1_I2S_MCLK       13
#define MM1_I2S_SCLK       12
#define MM1_I2S_LCLK       10
#define MM1_I2S_DOUT       9
#define MM1_I2S_DSIN       11
#define MM1_AMP_EN         53

// ── MM1 on J3 (40-pin) ──────────────────────────────────────────────────────
/* BNO086 on the silk SDA/SCL pads (shared bus GPIO7/8 with touch/codecs).
 * Address 0x4B — no clash with GT911 (0x5D), ES8311 (0x18), ES7210 (0x40).
 * Firmware still skips IMU until SH-2 talks over the legacy I2C driver
 * (Wire/i2c_master cannot coexist with Display_Panel). INT unused (poll only). */
#define MM1_IMU_ADDR       0x4B
#define MM1_IMU_SDA        MM1_I2C_SDA   /* silk SDA */
#define MM1_IMU_SCL        MM1_I2C_SCL   /* silk SCL */
#define MM1_IMU_INT        (-1)
#define MM1_IMU_RST        (-1)

#define MM1_LZR_UART_NUM   1
#define MM1_LZR_RX         21   /* silk 21 — module TX → ESP RX */
#define MM1_LZR_TX         22   /* silk 22 — module RX → ESP TX */
#define MM1_LZR_ENA        (-1)

/* Capture button: silk GPIO5 sits next to GND — wire button to 5↔GND (active low).
 * Alternatives: GPIO2 / GPIO3 / GPIO4 (also free on J3). */
#define MM1_USER_BUTTON    5

#define MM1_BUZZER_PIN     (-1) /* chimes via ES8311; no spare piezo GPIO reserved */
#define MM1_BUZZER_LEDC_CH 7

#define MM1_BAT_ADC        (-1) /* LiPo sense not exposed on this carrier */
