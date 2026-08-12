/**
 * @file p4_board.cpp
 * @brief Panel + backlight + SD for MM1 on ESP32-P4.
 */

#include "p4_board.h"

#include <Arduino.h>

#include "mm1_p4_pins.h"
#include "p4_sd.h"

namespace {
esp_panel::board::Board *g_board = nullptr;
}

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
    if (!g_board->init() || !g_board->begin()) {
        Serial.println("p4_board: panel init/begin failed");
        return false;
    }
    if (auto bl = g_board->getBacklight()) {
        bl->setBrightness(80);
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
