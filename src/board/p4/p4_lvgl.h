/**
 * @file p4_lvgl.h
 * @brief Minimal LVGL 8.3 port for the Waveshare ESP32-P4 panel.
 */
#pragma once

#include <esp_display_panel.hpp>

bool p4_lvgl_init(esp_panel::board::Board *board);
void p4_lvgl_handler();
