/**
 * @file p4_bringup.cpp
 * @brief Hardware bring-up for the ESP32-P4 target.
 *
 * Step 1 of the port: prove the toolchain, the MIPI-DSI panel, the shared I2C
 * bus and the GT911 touch before any MM1 application code is moved over.
 * The LVGL UI and the survey logic still live in src/main.cpp on the CYD target.
 */

#include <algorithm>

#include <Arduino.h>
#include <driver/i2c.h>
#include <esp_display_panel.hpp>

#include "mm1_p4_pins.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

namespace {

Board *g_board = nullptr;
bool g_touch_ok = false;

/* Legacy driver on purpose: ESP32_Display_Panel installs the I2C host with
 * driver/i2c.h, and mixing it with Wire's i2c_master driver aborts at boot. */
void i2c_scan()
{
    Serial.printf("I2C scan on port 0 (SDA=%d SCL=%d)\n", MM1_I2C_SDA, MM1_I2C_SCL);
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        const esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK) {
            Serial.printf("  found 0x%02X\n", addr);
            found++;
        }
    }
    Serial.printf("I2C devices found: %d\n", found);
}

/* Raw read of the GT911 touch-status register (0x814E). Bit 7 means "coordinates
 * ready", the low nibble is the number of touched points. Polling it directly
 * tells whether the controller sees anything at all, independently of the driver. */
constexpr bool kRawTouchDiag = true;
constexpr uint8_t kGt911Addr = 0x5D;
constexpr uint16_t kGt911StatusReg = 0x814E;

/* The controller only refreshes its coordinate buffer once the host acknowledges
 * the previous frame by zeroing 0x814E, so every read must be followed by this. */
bool gt911_clear_status()
{
    const uint8_t payload[3] = {kGt911StatusReg >> 8, kGt911StatusReg & 0xFF, 0};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (kGt911Addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, payload, sizeof(payload), true);
    i2c_master_stop(cmd);
    const esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

/* Reads the status byte and the first contact. Register layout from 0x814E:
 * [0]=status, [1]=reserved, [2]=track id, [3..4]=x little-endian, [5..6]=y. */
bool gt911_read_point(uint8_t *status, uint16_t *x, uint16_t *y)
{
    const uint8_t reg[2] = {kGt911StatusReg >> 8, kGt911StatusReg & 0xFF};
    uint8_t buf[7] = {0};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (kGt911Addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, reg, sizeof(reg), true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (kGt911Addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, sizeof(buf) - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &buf[sizeof(buf) - 1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    const esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        return false;
    }
    *status = buf[0];
    *x = static_cast<uint16_t>(buf[3]) | (static_cast<uint16_t>(buf[4]) << 8);
    *y = static_cast<uint16_t>(buf[5]) | (static_cast<uint16_t>(buf[6]) << 8);
    return true;
}

constexpr int kBandRows = 16;
uint16_t g_band[MM1_LCD_W * kBandRows];

void fill_screen(uint16_t rgb565)
{
    auto lcd = g_board->getLCD();
    if (lcd == nullptr) {
        return;
    }
    const int w = lcd->getFrameWidth();
    const int h_total = lcd->getFrameHeight();
    for (size_t i = 0; i < sizeof(g_band) / sizeof(g_band[0]); i++) {
        g_band[i] = rgb565;
    }
    for (int y = 0; y < h_total; y += kBandRows) {
        const int h = std::min(kBandRows, h_total - y);
        lcd->drawBitmap(0, y, w, h, reinterpret_cast<const uint8_t *>(g_band), 1000);
    }
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("MM1-BLACK — ESP32-P4 bring-up");
    Serial.printf("chip: %s, PSRAM: %u KB, heap: %u KB\n",
                  ESP.getChipModel(),
                  (unsigned)(ESP.getPsramSize() / 1024),
                  (unsigned)(ESP.getFreeHeap() / 1024));

    /* Backlight boost rail — the PWM pin alone does not light the panel. */
    if (MM1_LCD_BL_EN >= 0) {
        pinMode(MM1_LCD_BL_EN, OUTPUT);
        digitalWrite(MM1_LCD_BL_EN, HIGH);
    }

    g_board = new Board();
    if (!g_board->init()) {
        Serial.println("board init failed");
        return;
    }
    /* A failing touch controller must not hide whether the panel itself works,
     * so keep going and let the colour sweep speak for the display. */
    g_touch_ok = g_board->begin();
    if (!g_touch_ok) {
        Serial.println("board begin reported a failure — continuing to test the panel");
    }

    i2c_scan();

    if (auto backlight = g_board->getBacklight()) {
        backlight->setBrightness(80);
    }

    Serial.printf("panel: %dx%d\n", g_board->getLcdWidth(), g_board->getLcdHeight());
    Serial.println("colour sweep running continuously");
}

void loop()
{
    if (g_board == nullptr) {
        delay(1000);
        return;
    }

    /* Sweep forever so the panel can be inspected at any moment, not just in the
     * few seconds after a reset. */
    static const struct { const char *name; uint16_t rgb565; } kSweep[] = {
        {"red",   0xF800},
        {"green", 0x07E0},
        {"blue",  0x001F},
        {"white", 0xFFFF},
    };
    static size_t sweep_idx = 0;
    static uint32_t last_swap_ms = 0;
    if (millis() - last_swap_ms >= 1200) {
        last_swap_ms = millis();
        Serial.printf("filling %s\n", kSweep[sweep_idx].name);
        fill_screen(kSweep[sweep_idx].rgb565);
        sweep_idx = (sweep_idx + 1) % (sizeof(kSweep) / sizeof(kSweep[0]));
    }

    static uint32_t last_status_ms = 0;
    static int status_fail = 0, touch_frames = 0;
    static uint32_t last_summary_ms = 0;
    if (millis() - last_status_ms >= 20) {
        last_status_ms = millis();
        uint8_t st = 0;
        uint16_t x = 0, y = 0;
        if (gt911_read_point(&st, &x, &y)) {
            const int points = st & 0x0F;
            if ((st & 0x80) && points > 0) {
                touch_frames++;
                Serial.printf("gt911 TOQUE: points=%d x=%u y=%u\n", points, x, y);
            }
            if (st & 0x80) {
                gt911_clear_status();
            }
        } else {
            status_fail++;
        }
    }
    if (millis() - last_summary_ms >= 5000) {
        last_summary_ms = millis();
        Serial.printf("gt911 resumo: frames com toque=%d, falhas i2c=%d\n",
                      touch_frames, status_fail);
        touch_frames = 0;
        status_fail = 0;
    }

    /* Driver polling is off while the raw register is under investigation: the
     * driver clears the status flag on every read and would race this poll. */
    auto touch = (g_touch_ok && !kRawTouchDiag) ? g_board->getTouch() : nullptr;
    if (touch != nullptr) {
        TouchPoint points[5];
        const int n = touch->readPoints(points, 5, 0);
        for (int i = 0; i < n; i++) {
            Serial.printf("touch[%d]: x=%d y=%d strength=%d\n",
                          i, points[i].x, points[i].y, points[i].strength);
        }
    }
    delay(20);
}
