#include "serial_cmd.h"

#ifdef ARDUINO_ARCH_ESP32

#include <Arduino.h>
#include <ctype.h>
#include "firmware_version.h"
#include "mm1_log.h"

static char g_cmd_buf[32];

extern void mm1_on_serial_wifi_ap(void);
extern void mm1_on_serial_wifi_join(void);
static uint8_t g_cmd_len = 0;

void serial_cmd_poll(void)
{
    while (Serial.available() > 0) {
        const char c = (char)Serial.read();
        if (c == '\r')
            continue;
        if (c == '\n') {
            g_cmd_buf[g_cmd_len] = '\0';
            if (g_cmd_len > 0) {
                for (uint8_t i = 0; i < g_cmd_len; i++)
                    g_cmd_buf[i] = (char)toupper((unsigned char)g_cmd_buf[i]);
                if (strcmp(g_cmd_buf, "VERSION") == 0 ||
                    strcmp(g_cmd_buf, "FW_VERSION") == 0) {
                    Serial.print("MM1_FW_VERSION=");
                    Serial.println(FW_VERSION);
                } else if (strcmp(g_cmd_buf, "WIFI_AP") == 0) {
                    Serial.println("MM1_WIFI=AP");
                    mm1_on_serial_wifi_ap();
                } else if (strcmp(g_cmd_buf, "WIFI_JOIN") == 0) {
                    Serial.println("MM1_WIFI=JOIN");
                    mm1_on_serial_wifi_join();
                } else if (strcmp(g_cmd_buf, "LOG") == 0) {
                    Serial.print("MM1_LOG=");
                    Serial.println(mm1_log_ready() ? mm1_log_path() : "off");
                }
            }
            g_cmd_len = 0;
            continue;
        }
        if (g_cmd_len < sizeof(g_cmd_buf) - 1)
            g_cmd_buf[g_cmd_len++] = c;
        else
            g_cmd_len = 0;
    }
}

#endif
