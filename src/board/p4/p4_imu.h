/**
 * @file p4_imu.h
 * @brief BNO086 SH-2 over software I2C (GPIO30/31). No Wire.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool p4_imu_begin(uint8_t i2c_addr);
void p4_imu_poll(void);
bool p4_imu_ok(void);

/** Latest rotation-vector quaternion (valid when p4_imu_ok). */
bool p4_imu_get_quat(float *qw, float *qx, float *qy, float *qz, int *accuracy);
bool p4_imu_get_accel(float *ax, float *ay, float *az);
bool p4_imu_was_reset(void);
bool p4_imu_enable_reports(void);

/** After a failed begin: "noACK idle=SDA/SCL" or an SH-2 stage name. */
void p4_imu_scan(char *buf, size_t buflen);
