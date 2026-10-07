#pragma once

// Copy to include/lab_config.h (ignored by Git) and fill in your 2.4 GHz network.
// An empty password selects an open network. Do not put credentials in build flags.
#define LAB_WIFI_SSID ""
#define LAB_WIFI_PASSWORD ""
#define LAB_HOSTNAME "mm1-p4-lab"
#define LAB_TCP_PORT 5000
#define LAB_WIFI_CONNECT_TIMEOUT_MS 20000UL
#define LAB_WIFI_RETRY_MS 10000UL

// Optional acquisition settings (also defaulted for older local config files).
#define LAB_LASER_TIMEOUT_MS 3200UL
#define LAB_IMU_TIMEOUT_MS 1000UL
// Manual IMU calibration: bounded session/SH-2 commands and feedback recency.
#define LAB_CAL_SESSION_TIMEOUT_MS 300000UL
#define LAB_CAL_OP_TIMEOUT_MS 2000UL
#define LAB_CAL_FRESH_MS 1000UL
// Laser direction in the IMU body frame. No magnetic declination/heading trim.
#define IMU_LASER_AXIS_BX 1.0f
#define IMU_LASER_AXIS_BY 0.0f
#define IMU_LASER_AXIS_BZ 0.0f
