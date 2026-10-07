#pragma once
#include <deque>
#include "../Arduino.h"
using gpio_num_t = int;
constexpr int GPIO_PULLUP_ONLY = 0, GPIO_MODE_INPUT = 0, GPIO_MODE_OUTPUT = 1;
inline bool fake_scl_stuck = false;
inline int fake_scl_pin = 31;
inline std::deque<int> fake_sda_bits;
inline void gpio_reset_pin(int) {}
inline void gpio_set_pull_mode(int, int) {}
inline void gpio_set_direction(int, int) {}
inline void gpio_set_level(int, int) {}
inline int gpio_input_enable(int) { return 0; }
inline int gpio_get_level(int pin) {
    for (auto it = fake_pin_writes.rbegin(); it != fake_pin_writes.rend(); ++it)
        if (it->pin == pin) return it->level;
    if (pin == fake_scl_pin) return !fake_scl_stuck;
    if (fake_sda_bits.empty()) return 1; // Disconnected sensor: NACK.
    const auto bit = fake_sda_bits.front(); fake_sda_bits.pop_front(); return bit;
}
