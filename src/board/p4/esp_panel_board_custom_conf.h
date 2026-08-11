/**
 * @file esp_panel_board_custom_conf.h
 * @brief ESP32_Display_Panel board description — Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3.
 *
 * Panel: 4.3" IPS 480x800 portrait, ST7701 over MIPI-DSI 2-lane.
 * Touch: GT911 capacitive (5-point) on the board-wide I2C bus (GPIO7/GPIO8),
 *        shared with ES8311, ES7210 and the camera connector.
 *
 * The DSI timings come from a known-good 4.3" 480x800 ST7701S panel bring-up:
 * 500 Mbps/lane, 34 MHz DPI clock, LDO channel 3 for the DSI PHY.
 * GPIO numbers follow the -4B pin table of the same product family and must be
 * re-checked against the 4.3 schematic on first hardware run (see docs/ESP32_P4_PORT.md).
 */

#pragma once

// *INDENT-OFF*

#define ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM (1)

#if ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM

#define ESP_PANEL_BOARD_NAME    "Waveshare:ESP32-P4-WIFI6-Touch-LCD-4.3"
#define ESP_PANEL_BOARD_WIDTH   (480)
#define ESP_PANEL_BOARD_HEIGHT  (800)

// ── LCD ──────────────────────────────────────────────────────────────────────
#define ESP_PANEL_BOARD_USE_LCD         (1)
#define ESP_PANEL_BOARD_LCD_CONTROLLER  ST7701
#define ESP_PANEL_BOARD_LCD_BUS_TYPE    (ESP_PANEL_BUS_TYPE_MIPI_DSI)

#define ESP_PANEL_BOARD_LCD_MIPI_DSI_LANE_NUM       (2)
#define ESP_PANEL_BOARD_LCD_MIPI_DSI_LANE_RATE_MBPS (500)

#define ESP_PANEL_BOARD_LCD_MIPI_DPI_CLK_MHZ        (30)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_PIXEL_BITS     (ESP_PANEL_LCD_COLOR_BITS_RGB565)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_HPW            (12)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_HBP            (42)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_HFP            (42)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_VPW            (8)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_VBP            (2)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_VFP            (60)

/* P4 needs an internal LDO channel to power the DSI PHY (2.5 V). */
#define ESP_PANEL_BOARD_LCD_MIPI_PHY_LDO_ID         (3)

/* Panel-specific ST7701 power-on sequence, taken from Waveshare's own BSP for this
 * board (ESP32-P4-WIFI6-Touch-LCD-4.3). Without it the controller never leaves
 * sleep and the panel stays dark even though the DSI link is up. Ends with 0x11
 * (sleep out, 120 ms) and 0x29 (display on). */
#define ESP_PANEL_BOARD_LCD_VENDOR_INIT_CMD() \
    { \
        {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0}, \
        {0xEF, (uint8_t[]){0x08}, 1, 0}, \
        {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0}, \
        {0xC0, (uint8_t[]){0x63, 0x00}, 2, 0}, \
        {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0}, \
        {0xC2, (uint8_t[]){0x17, 0x08}, 2, 0}, \
        {0xCC, (uint8_t[]){0x10}, 1, 0}, \
        {0xB0, (uint8_t[]){0x40, 0xC9, 0x94, 0x0E, 0x10, 0x05, 0x0B, 0x09, 0x08, 0x26, 0x04, 0x52, 0x10, 0x69, 0x6B, 0x69}, 16, 0}, \
        {0xB1, (uint8_t[]){0x40, 0xD2, 0x98, 0x0C, 0x92, 0x07, 0x09, 0x08, 0x07, 0x25, 0x02, 0x0E, 0x0C, 0x6E, 0x78, 0x55}, 16, 0}, \
        {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0}, \
        {0xB0, (uint8_t[]){0x5D}, 1, 0}, \
        {0xB1, (uint8_t[]){0x4E}, 1, 0}, \
        {0xB2, (uint8_t[]){0x87}, 1, 0}, \
        {0xB3, (uint8_t[]){0x80}, 1, 0}, \
        {0xB5, (uint8_t[]){0x4E}, 1, 0}, \
        {0xB7, (uint8_t[]){0x85}, 1, 0}, \
        {0xB8, (uint8_t[]){0x21}, 1, 0}, \
        {0xB9, (uint8_t[]){0x10, 0x1F}, 2, 0}, \
        {0xBB, (uint8_t[]){0x03}, 1, 0}, \
        {0xBC, (uint8_t[]){0x00}, 1, 0}, \
        {0xC1, (uint8_t[]){0x78}, 1, 0}, \
        {0xC2, (uint8_t[]){0x78}, 1, 0}, \
        {0xD0, (uint8_t[]){0x88}, 1, 0}, \
        {0xE0, (uint8_t[]){0x00, 0x3A, 0x02}, 3, 0}, \
        {0xE1, (uint8_t[]){0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40}, 11, 0}, \
        {0xE2, (uint8_t[]){0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00}, 13, 0}, \
        {0xE3, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0}, \
        {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0}, \
        {0xE5, (uint8_t[]){0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0}, 16, 0}, \
        {0xE6, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0}, \
        {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0}, \
        {0xE8, (uint8_t[]){0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0}, 16, 0}, \
        {0xEB, (uint8_t[]){0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00}, 7, 0}, \
        {0xEC, (uint8_t[]){0x08, 0x01}, 2, 0}, \
        {0xED, (uint8_t[]){0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B}, 16, 0}, \
        {0xEF, (uint8_t[]){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0}, \
        {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0}, \
        {0x11, (uint8_t[]){0x00}, 0, 120}, \
        {0x29, (uint8_t[]){0x00}, 0, 0}, \
    }

#define ESP_PANEL_BOARD_LCD_COLOR_BITS        (ESP_PANEL_LCD_COLOR_BITS_RGB565)
#define ESP_PANEL_BOARD_LCD_COLOR_BGR_ORDER   (0)
#define ESP_PANEL_BOARD_LCD_COLOR_INEVRT_BIT  (0)

/* Panel is natively portrait; the UI is portrait too, so no transform. */
#define ESP_PANEL_BOARD_LCD_SWAP_XY   (0)
#define ESP_PANEL_BOARD_LCD_MIRROR_X  (0)
#define ESP_PANEL_BOARD_LCD_MIRROR_Y  (0)
#define ESP_PANEL_BOARD_LCD_GAP_X     (0)
#define ESP_PANEL_BOARD_LCD_GAP_Y     (0)

#define ESP_PANEL_BOARD_LCD_RST_IO    (27)
#define ESP_PANEL_BOARD_LCD_RST_LEVEL (0)

// ── Touch ────────────────────────────────────────────────────────────────────
#define ESP_PANEL_BOARD_USE_TOUCH        (1)
#define ESP_PANEL_BOARD_TOUCH_CONTROLLER GT911
#define ESP_PANEL_BOARD_TOUCH_BUS_TYPE   (ESP_PANEL_BUS_TYPE_I2C)

/* This library talks I2C through the legacy driver/i2c.h. Arduino's Wire uses the
 * new i2c_master driver, and ESP-IDF aborts if both are used in the same image
 * ("CONFLICT! driver_ng is not allowed to be used with this old driver"), so the
 * panel library owns the bus and Wire must stay unused on this target. */
#define ESP_PANEL_BOARD_TOUCH_BUS_SKIP_INIT_HOST (0)
#define ESP_PANEL_BOARD_TOUCH_I2C_HOST_ID        (0)
#define ESP_PANEL_BOARD_TOUCH_I2C_CLK_HZ         (400 * 1000)
#define ESP_PANEL_BOARD_TOUCH_I2C_SCL_PULLUP     (0)  // board has external pull-ups
#define ESP_PANEL_BOARD_TOUCH_I2C_SDA_PULLUP     (0)
#define ESP_PANEL_BOARD_TOUCH_I2C_IO_SCL         (8)
#define ESP_PANEL_BOARD_TOUCH_I2C_IO_SDA         (7)
#define ESP_PANEL_BOARD_TOUCH_I2C_ADDRESS        (0)  // 0 = default (GT911 0x5D)

#define ESP_PANEL_BOARD_TOUCH_SWAP_XY   (0)
#define ESP_PANEL_BOARD_TOUCH_MIRROR_X  (0)
#define ESP_PANEL_BOARD_TOUCH_MIRROR_Y  (0)

#define ESP_PANEL_BOARD_TOUCH_RST_IO    (23)
#define ESP_PANEL_BOARD_TOUCH_RST_LEVEL (0)
#define ESP_PANEL_BOARD_TOUCH_INT_IO    (-1)
#define ESP_PANEL_BOARD_TOUCH_INT_LEVEL (0)

// ── Backlight ────────────────────────────────────────────────────────────────
#define ESP_PANEL_BOARD_USE_BACKLIGHT       (1)
#define ESP_PANEL_BOARD_BACKLIGHT_TYPE      (ESP_PANEL_BACKLIGHT_TYPE_PWM_LEDC)
#define ESP_PANEL_BOARD_BACKLIGHT_IO        (26)
#define ESP_PANEL_BOARD_BACKLIGHT_ON_LEVEL  (1)
#define ESP_PANEL_BOARD_BACKLIGHT_PWM_FREQ_HZ         (5000)
#define ESP_PANEL_BOARD_BACKLIGHT_PWM_DUTY_RESOLUTION (10)
#define ESP_PANEL_BOARD_BACKLIGHT_IDLE_OFF  (0)

// ── IO expander (none on this board) ─────────────────────────────────────────
#define ESP_PANEL_BOARD_USE_EXPANDER (0)

#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MAJOR 1
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MINOR 2
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_PATCH 0

#endif // ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM

// *INDENT-ON*
