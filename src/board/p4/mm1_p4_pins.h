/**
 * @file mm1_p4_pins.h
 * @brief Pin map for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 carrier.
 *
 * Everything except the MM1-specific peripherals is confirmed against Waveshare's
 * own BSP for this exact board (waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3,
 * components/esp32_p4_wifi6_touch_lcd_4_3) and the published schematic.
 * Values marked TBD need a free GPIO picked from the 40-pin header.
 */

#pragma once

// ── Shared I2C bus (GT911 touch, ES8311 codec, ES7210, camera, MM1 IMU) ──────
#define MM1_I2C_SDA        7
#define MM1_I2C_SCL        8
#define MM1_I2C_FREQ_HZ    400000

// ── Display ─────────────────────────────────────────────────────────────────
#define MM1_LCD_W          480
#define MM1_LCD_H          800
#define MM1_LCD_RST        27
#define MM1_LCD_BL_PWM     26   // driven by ESP32_Display_Panel (LEDC)
#define MM1_LCD_BL_EN      33   // backlight boost enable, must be high
#define MM1_TP_RST         23
#define MM1_TP_INT         (-1) // routed to test point TP2 only, not to a GPIO

// ── microSD (SDIO 3.0, 4-bit) ───────────────────────────────────────────────
#define MM1_SD_CLK         43
#define MM1_SD_CMD         44
#define MM1_SD_D0          39
#define MM1_SD_D1          40
#define MM1_SD_D2          41
#define MM1_SD_D3          42
/* The slot is powered through on-chip LDO channel 4 (sd_pwr_ctrl_by_on_chip_ldo),
 * not a GPIO enable — see Waveshare's bsp_sdcard_init. */
#define MM1_SD_LDO_CHAN    4

// ── Audio codec (ES8311 playback + ES7210 capture), unused by MM1 so far ─────
#define MM1_I2S_MCLK       13
#define MM1_I2S_SCLK       12
#define MM1_I2S_LCLK       10
#define MM1_I2S_DOUT       9
#define MM1_I2S_DSIN       11
#define MM1_AMP_EN         53

// ── MM1 peripherals on the 40-pin header ────────────────────────────────────
/* BNO086 shares the board I2C bus; 0x4B does not collide with GT911 (0x5D/0x14),
 * ES8311 (0x18), ES7210 (0x40/0x41) or the camera (0x36). */
#define MM1_IMU_ADDR       0x4B
#define MM1_IMU_INT        (-1)  // TBD
#define MM1_IMU_RST        (-1)

/* Laser rangefinder gets a dedicated UART here: unlike the CYD, there is no
 * reason to share the USB bridge lines. */
#define MM1_LZR_UART_NUM   1
#define MM1_LZR_RX         (-1)  // TBD
#define MM1_LZR_TX         (-1)  // TBD
#define MM1_LZR_ENA        (-1)

#define MM1_USER_BUTTON    (-1)  // TBD — capture button, active low

/* Battery: the board charges a 3.7 V LiPo on the MX1.25 header. Whether the
 * cell voltage is exposed to an ADC pin has to come from the schematic. */
#define MM1_BAT_ADC        (-1)  // TBD
