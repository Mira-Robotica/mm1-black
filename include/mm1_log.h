#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef ARDUINO_ARCH_ESP32

/** Light SD usage / fault log. Never blocks setup or the UI thread. */

void mm1_log_begin(bool sd_ok);
void mm1_log_poll(void);

/** kind is a short token (BOOT, CLICK, FAIL, …). fmt is printf-style, no newline. */
void mm1_log_event(const char *kind, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

void mm1_log_fail(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

void mm1_log_battery(int pct, float volts);
void mm1_log_feed(uint8_t vol_pct, uint32_t shots, uint32_t btn_taps,
                  uint32_t ble_legs, uint32_t ble_acks);

/** Skip SD writes while another SD stream is open (BLE CSV TX). */
void mm1_log_hold(bool hold);

uint32_t mm1_log_boot_n(void);
const char *mm1_log_path(void);
bool mm1_log_ready(void);

#endif
