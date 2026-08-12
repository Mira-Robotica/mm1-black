/**
 * @file p4_board.h
 * @brief One-shot board bring-up for MM1 on ESP32-P4.
 */
#pragma once

#include <esp_display_panel.hpp>

bool p4_board_init(esp_panel::board::Board **out_board);
esp_panel::board::Board *p4_board_get();
