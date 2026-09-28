#pragma once

// Copy to include/lab_config.h (ignored by Git) and fill in your 2.4 GHz network.
// An empty password selects an open network. Do not put credentials in build flags.
#define LAB_WIFI_SSID ""
#define LAB_WIFI_PASSWORD ""
#define LAB_HOSTNAME "mm1-p4-lab"
#define LAB_TCP_PORT 5000
#define LAB_WIFI_CONNECT_TIMEOUT_MS 20000UL
#define LAB_WIFI_RETRY_MS 10000UL
