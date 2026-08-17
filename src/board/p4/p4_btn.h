/**
 * @file p4_btn.h
 * @brief Capture button on J3 silk 5 (GPIO5).
 *
 * 4-pin NO switch: use NO and C only (not LED +/−).
 * C → GND, NO → silk 5. Press pulls the pin low (same as v0.7.2).
 */
#pragma once

#include <stdbool.h>

void p4_btn_init(void);

/** Raw pin level (1 = released with pull-up). */
int p4_btn_level(void);

/** True while the switch is closed. */
bool p4_btn_pressed(void);
