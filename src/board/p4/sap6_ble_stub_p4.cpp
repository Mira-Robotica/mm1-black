/**
 * @file sap6_ble_stub_p4.cpp
 * @brief BLE stub until ESP-Hosted / C6 is wired on the P4 target.
 */
#include "sap6_ble.h"

#include <cstdio>

#ifdef ARDUINO_ARCH_ESP32

void sap6_ble_begin(const char *) {}
void sap6_ble_restart(const char *) {}
void sap6_ble_poll(void) {}
bool sap6_ble_stack_ready(void) { return false; }
bool sap6_ble_connected(void) { return false; }
void sap6_ble_get_mac_str(char *buf, size_t len)
{
    if (buf && len)
        buf[0] = '\0';
}
void sap6_ble_format_status(char *buf, size_t len)
{
    if (buf && len)
        snprintf(buf, len, "BLE: stub (no C6 yet)");
}
void sap6_ble_send_leg(float, float, float, float) {}
bool sap6_ble_try_send_leg(float, float, float, float) { return false; }
void sap6_ble_queue_reset(void) {}
void sap6_ble_ack_stall_recover(void) {}
bool sap6_ble_waiting_ack(void) { return false; }
bool sap6_ble_stream_start(int, const void *, size_t, float (*)(const void *),
                           float (*)(const void *), float (*)(const void *),
                           float (*)(const void *))
{
    return false;
}
void sap6_ble_stream_cancel(void) {}
bool sap6_ble_stream_active(void) { return false; }
void sap6_ble_stream_progress(int *q, int *t)
{
    if (q)
        *q = 0;
    if (t)
        *t = 0;
}
void sap6_ble_clear_bonds(void) {}
uint32_t sap6_ble_legs_sent(void) { return 0; }
uint32_t sap6_ble_acks_ok(void) { return 0; }
uint32_t sap6_ble_acks_wrong(void) { return 0; }
uint32_t sap6_ble_resends(void) { return 0; }
uint32_t sap6_ble_queue_depth(void) { return 0; }

#endif
