/**
 * @file p4_sd.h
 * @brief SDIO microSD mount for the Waveshare ESP32-P4 board.
 */
#pragma once

#include <Arduino.h>

bool p4_sd_begin();
bool p4_sd_ready();
uint64_t p4_sd_total_mb();
uint64_t p4_sd_used_mb();
String p4_sd_status_line();
