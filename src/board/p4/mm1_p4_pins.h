/**
 * @file mm1_p4_pins.h
 * @brief Pin map — Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 + MM1 peripherals.
 *
 * On-board signals: Waveshare BSP + schematic
 *   docs/datasheets/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf
 *
 * J3 40-pin silk labels are ESP32-P4 GPIO numbers (SDA/SCL = GPIO7/8).
 * GPIOs 14–19 + 54 are the ESP32-C6 Hosted SDIO link (CLK18 CMD19
 * D0=14 D1=15 D2=16 D3=17 RST=54). GPIO20 is the on-board LiPo ADC.
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
/* BNO086 on software I2C. Do not use silk SDA/SCL (GPIO7/8 = GT911).
 * GPIO2/3 = JTAG. GPIO28/29 sit on J3 pins that idle LOW (pin 20 is GND on
 * the Pi-style header). Use silk 30/31.
 * VIN=3V3 GND=GND SDA=silk 30 SCL=silk 31. Addr 0x4B (or 0x4A). RST unused. */
#define MM1_IMU_ADDR       0x4B
#define MM1_IMU_SDA        30   /* silk "30" */
#define MM1_IMU_SCL        31   /* silk "31" */
#define MM1_IMU_INT        (-1)
#define MM1_IMU_RST        (-1)

#define MM1_LZR_UART_NUM   1
#define MM1_LZR_RX         21   /* silk 21 — module TX → ESP RX */
#define MM1_LZR_TX         22   /* silk 22 — module RX → ESP TX */
#define MM1_LZR_ENA        (-1)

/* 4-pin illuminated NO button: switch is NO+C only (+/− is the LED).
 * C → GND, NO → silk 5 (GPIO5). Do not switch 3V3/5V into the GPIO. */
#define MM1_USER_BUTTON    5

#define MM1_BUZZER_PIN     (-1) /* chimes via ES8311; no spare piezo GPIO reserved */
#define MM1_BUZZER_LEDC_CH 7

/* BAT -- R12 200k -- GPIO20 -- R15 100k -- GND  (NLBAT0ADC). Vadc = Vbat/3.
 * GPIO20 is also ESP32-C6 SDIO D2; safe while ESP-Hosted is not started. */
#define MM1_BAT_ADC        20
#ifndef MM1_BAT_DIVIDER
#define MM1_BAT_DIVIDER    3.0f
#endif
