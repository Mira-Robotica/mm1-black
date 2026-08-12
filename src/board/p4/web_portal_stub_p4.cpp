/**
 * @file web_portal_stub_p4.cpp
 * @brief Wi-Fi portal stub until ESP-Hosted is available on P4.
 */
#include "web_portal.h"

#ifdef ARDUINO_ARCH_ESP32

namespace web_portal {

bool start(const char *, const char *, const Callbacks &) { return false; }
void stop() {}
bool running() { return false; }
uint8_t clients() { return 0; }
const char *ap_ip() { return "0.0.0.0"; }
void loop() {}

}  // namespace web_portal

#endif
