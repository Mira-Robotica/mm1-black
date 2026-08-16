/**
 * @file p4_boot.h
 * @brief Boot splash + chime helpers for ESP32-P4.
 */
#pragma once

#include <stdint.h>

/** Fill panel black and draw the 320×480 MIRA splash centered on 480×800. */
void p4_boot_show_splash(const uint8_t *rgb565, int img_w, int img_h);

/** Init PWM buzzer on MM1_BUZZER_PIN (+ amp enable). */
void p4_boot_audio_init(void);

void p4_boot_buzzer_note(unsigned freq_hz, unsigned dur_ms);
