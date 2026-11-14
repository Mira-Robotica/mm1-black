#pragma once
// Narrow hooks used only by the vendored Arduino wrapper in MM1_LAB.
void p4_sparkfun_stage(const char *stage, int rc);
void p4_sparkfun_io_error();
void p4_sparkfun_protocol_error(unsigned char event);
void p4_sparkfun_rx_error(const char *stage, const unsigned char *header, unsigned expected);
void p4_sparkfun_empty_read();
void p4_sparkfun_reset();
void p4_sparkfun_probe_result(unsigned char address, unsigned char rc);
void p4_sparkfun_feature(unsigned char sensor_id, unsigned long interval_us);

#include "../SparkFun_BNO08x_Arduino_Library-1.0.6/src/sh2.h"
void p4_sparkfun_feature_config(unsigned char sensor_id, const sh2_SensorConfig_t &config);
