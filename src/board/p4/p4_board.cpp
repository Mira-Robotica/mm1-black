/**
 * @file p4_board.cpp
 * @brief Panel + backlight + SD for MM1 on ESP32-P4.
 */

#include "p4_board.h"

#include <Arduino.h>

#include <esp_display_panel.hpp>

#include "mm1_p4_pins.h"
#include "p4_sd.h"

using namespace esp_panel::drivers;

namespace {
esp_panel::board::Board *g_board = nullptr;

bool lcd_is_ready(void)
{
    if (g_board == nullptr) {
        return false;
    }
    auto lcd = g_board->getLCD();
    return lcd != nullptr && lcd->isOverState(LCD::State::BEGIN);
}

} // namespace

bool p4_board_init(esp_panel::board::Board **out_board)
{
    if (MM1_LCD_BL_EN >= 0) {
        pinMode(MM1_LCD_BL_EN, OUTPUT);
        digitalWrite(MM1_LCD_BL_EN, HIGH);
    }
    if (MM1_USER_BUTTON >= 0) {
        pinMode(MM1_USER_BUTTON, INPUT_PULLUP);
    }

    g_board = new esp_panel::board::Board();
    if (!g_board->init()) {
        Serial.println("p4_board: panel init failed");
        return false;
    }

    const bool began = g_board->begin();
    if (!began) {
        /* LCD usually comes up before GT911. A wedged touch I2C (e.g. leftover
         * wiring on silk SDA/SCL) must not brick bring-up of button/IMU/laser. */
        Serial.println("p4_board: Board::begin failed (often GT911 I2C)");
        Serial.println("p4_board: keep silk SDA/SCL free of the BNO (touch bus)");
        if (!lcd_is_ready()) {
            Serial.println("p4_board: LCD also not ready — cannot continue");
            return false;
        }
        Serial.println("p4_board: salvaging LCD without touch");
        if (auto bl = g_board->getBacklight()) {
            if (!bl->isOverState(Backlight::State::BEGIN)) {
                (void)bl->begin();
            }
            (void)bl->on();
            (void)bl->setBrightness(80);
        }
    } else if (auto bl = g_board->getBacklight()) {
        bl->setBrightness(80);
    }

    /* Re-apply after Board::begin — panel bring-up can reset GPIO matrix. */
    if (MM1_USER_BUTTON >= 0) {
        pinMode(MM1_USER_BUTTON, INPUT_PULLUP);
    }

    p4_sd_begin();
    if (out_board) {
        *out_board = g_board;
    }
    return true;
}

esp_panel::board::Board *p4_board_get()
{
    return g_board;
}
