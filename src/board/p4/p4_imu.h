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

// Lab snapshots retain the precision and quality of the same decoded report.
struct P4ImuSample {
    float w, x, y, z, accuracy_rad;
    uint8_t status, sequence;
    uint32_t generation;
};
struct P4ImuDiagnostics {
    uint32_t generation, empty_reads, resets, io_errors, decode_errors, sequence_gaps;
    bool ready;
    const char *init_stage = "NOT_STARTED";
    int init_rc = 0;
    uint8_t address = 0, sda = 0, scl = 0;
};
bool p4_imu_snapshot(P4ImuSample *sample);
P4ImuDiagnostics p4_imu_diagnostics();

#if defined(MM1_LAB)
#include "bno08x/sh2_lab.h"
struct P4ImuFeedback {
    bool present;
    uint8_t status, sequence;
    uint32_t generation, received_ms, sequence_gaps;
    float accuracy_rad; // Only defined for magnetic Rotation Vector.
};
struct P4ImuLabInfo {
    P4ImuFeedback rv, game, mag;
    int autosave_rc; // Command 0x09 has no sensor ACK; SH2_OK means sent only.
    uint32_t part, build;
    uint8_t major, minor;
    uint16_t patch;
    uint32_t rv_interval_us, game_interval_us, mag_interval_us;
};
P4ImuLabInfo p4_imu_lab_info();
int p4_imu_lab_report(uint8_t sensor_id, uint32_t interval_us);
void p4_imu_lab_invalidate();
#endif
