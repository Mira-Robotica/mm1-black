/**
 * @file p4_imu.h
 * @brief BNO08x API: SparkFun/hardware I2C in lab; legacy software I2C otherwise.
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
struct P4ImuGameSample {
    float w, x, y, z;
    uint8_t status, sequence;
    uint32_t generation;
};
struct P4ImuMagSample {
    float x, y, z; // microteslas, in the configured sensor output axes.
    uint8_t status, sequence;
    uint32_t generation;
};
struct P4ImuDiagnostics {
    uint32_t generation, empty_reads, resets, io_errors, decode_errors, sequence_gaps;
    bool ready;
    const char *init_stage = "NOT_STARTED";
    int init_rc = 0;
    uint8_t address = 0, sda = 0, scl = 0;
    uint32_t hardware_resets = 0, init_ms = 0;
    // Current initialization only. -1 means no measurement/probe was made.
    uint32_t probe_attempts = 0, probe_elapsed_ms = 0;
    int8_t probe_4b_rc = -1, probe_4a_rc = -1;
    int8_t probe_sda = -1, probe_scl = -1, rst_low = -1, rst_high = -1;
    uint32_t game_generation = 0, mag_generation = 0, epoch = 0;
};
bool p4_imu_snapshot(P4ImuSample *sample);
bool p4_imu_game_snapshot(P4ImuGameSample *sample);
bool p4_imu_mag_snapshot(P4ImuMagSample *sample);
P4ImuDiagnostics p4_imu_diagnostics();

#if defined(MM1_LAB)
#include "imu_sparkfun/sh2_lab.h"
#include "imu_sparkfun/config.h"
// Schedule one physical reset/reinitialization; caller must exclude active requests.
void p4_imu_request_reset();
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
    // Last Get Feature Response; cleared to -1 on each configuration/reset.
    int32_t rv_confirmed_us = -1, game_confirmed_us = -1, mag_confirmed_us = -1;
};
// Streaming order is a wire contract: Raw, Uncalibrated, Calibrated, RV, GRV.
#include "SparkFun_BNO08x_Arduino_Library-1.0.6/src/sh2.h"
constexpr uint8_t P4_STREAM_IDS[5] = {0x16, 0x0f, 0x03, 0x05, 0x08};
struct P4ImuStreamSample {
    float v[4]{}; // magnetic xyz or quaternion wxyz, never normalized
    uint32_t generation = 0, received_ms = 0, epoch = 0, raw_timestamp_us = 0;
    uint8_t sequence = 0, status = 0;
    bool valid = false;
};
struct P4ImuFeature {
    sh2_SensorConfig_t config{};
    uint32_t generation = 0;
    bool known = false;
};
bool p4_imu_reset_pending();
P4ImuStreamSample p4_imu_stream_sample(unsigned group);
P4ImuFeature p4_imu_feature(unsigned group);
int p4_imu_set_feature(unsigned group, const sh2_SensorConfig_t &config);
P4ImuLabInfo p4_imu_lab_info();
int p4_imu_lab_report(uint8_t sensor_id, uint32_t interval_us);
void p4_imu_lab_invalidate();
#endif
