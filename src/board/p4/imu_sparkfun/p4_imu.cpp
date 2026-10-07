// Lab-only BNO08x adapter. The production software-I2C driver stays untouched.
#include "../p4_imu.h"
#include "hooks.h"
#include "../SparkFun_BNO08x_Arduino_Library-1.0.6/src/SparkFun_BNO08x_Arduino_Library.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <driver/gpio.h>

namespace {
BNO08x imu;
// Wire1 owns HP I2C controller 1. Controller 0 belongs to the GT911 in production.
bool ready = false, initializing = false, reconfigure = false, reset_pending = false;
bool reset_seen = false, sample_valid = false, sequence_known = false;
uint8_t address = 0x4b, last_sequence = 0;
P4ImuSample sample{};
P4ImuDiagnostics diag{};
P4ImuLabInfo info{};
char failure[80] = "NOT_STARTED";
uint32_t reset_released_ms = 0;

void feedback(P4ImuFeedback &f, const sh2_SensorValue_t &v, float accuracy = 0)
{
    if (f.present && v.sequence == f.sequence) return;
    if (f.present && uint8_t(v.sequence - f.sequence) != 1) ++f.sequence_gaps;
    f.present = true;
    f.sequence = v.sequence;
    f.status = v.status & 3;
    ++f.generation;
    f.received_ms = millis();
    f.accuracy_rad = accuracy;
}
void sensor(void *, sh2_SensorEvent_t *event)
{
    if (!ready || reconfigure) return;
    sh2_SensorValue_t v{};
    if (sh2_decodeSensorEvent(&v, event) != SH2_OK) { ++diag.decode_errors; return; }
    if (v.sensorId == SH2_ROTATION_VECTOR) {
        if (sequence_known && v.sequence == last_sequence) return;
        if (sequence_known && uint8_t(v.sequence - last_sequence) != 1) ++diag.sequence_gaps;
        last_sequence = v.sequence;
        sequence_known = true;
        sample = {v.un.rotationVector.real, v.un.rotationVector.i,
                  v.un.rotationVector.j, v.un.rotationVector.k,
                  v.un.rotationVector.accuracy, uint8_t(v.status & 3),
                  v.sequence, ++diag.generation};
        sample_valid = true;
        if (std::isfinite(sample.accuracy_rad) && sample.accuracy_rad >= 0)
            feedback(info.rv, v, sample.accuracy_rad);
    } else if (v.sensorId == SH2_GAME_ROTATION_VECTOR) {
        feedback(info.game, v);
    } else if (v.sensorId == SH2_MAGNETIC_FIELD_CALIBRATED &&
               std::isfinite(v.un.magneticField.x) && std::isfinite(v.un.magneticField.y) &&
               std::isfinite(v.un.magneticField.z)) {
        feedback(info.mag, v);
    }
}
bool configure()
{
    // Both APIs only send a command; they do not wait for a sensor ACK.
    if (!p4_imu_enable_reports()) return false;
    info.autosave_rc = sh2_setDcdAutoSave(false);
    Serial.printf("[LAB] DCD autosave disable rc=%d ack=NOT_DEFINED\n", info.autosave_rc);
    if (info.autosave_rc != SH2_OK) {
        p4_sparkfun_stage("AUTOSAVE", info.autosave_rc);
        return false;
    }
    return true;
}
}

void p4_sparkfun_stage(const char *stage, int rc)
{
    diag.init_stage = stage;
    diag.init_rc = rc;
    diag.address = address;
    diag.sda = MM1_LAB_IMU_SDA;
    diag.scl = MM1_LAB_IMU_SCL;
    snprintf(failure, sizeof(failure), "%s rc=%d addr=0x%02X", stage, rc, address);
}
void p4_sparkfun_probe_result(unsigned char addr, unsigned char rc)
{
    ++diag.probe_attempts;
    diag.probe_elapsed_ms = millis() - reset_released_ms;
    if (addr == 0x4b) diag.probe_4b_rc = rc;
    if (addr == 0x4a) diag.probe_4a_rc = rc;
    // Read the pads without changing Wire's GPIO matrix/direction/pull-ups.
    diag.probe_sda = gpio_get_level((gpio_num_t)MM1_LAB_IMU_SDA);
    diag.probe_scl = gpio_get_level((gpio_num_t)MM1_LAB_IMU_SCL);
    if (rc != 0) {
        // Wire: 2 = NACK, 5 = timeout, 4 = other controller/API error.
        // Keep these failures separate from SH-2 traffic errors.
        p4_sparkfun_stage(rc == 2 ? "NO_ACK" : rc == 5 ? "I2C_PROBE_TIMEOUT" : "I2C_PROBE_ERROR", SH2_ERR_IO);
    }
}
void p4_sparkfun_io_error() { ++diag.io_errors; }
void p4_sparkfun_empty_read() { ++diag.empty_reads; }
void p4_sparkfun_reset()
{
    sh2_lab_abort();
    p4_imu_lab_invalidate();
    info.autosave_rc = SH2_ERR;
    info.rv_interval_us = info.game_interval_us = info.mag_interval_us = 0;
    reset_seen = true;
    reconfigure = true;
    // A requested hardware reset is counted when NRST is asserted.
    if (!initializing) ++diag.resets;
}
void p4_imu_lab_invalidate()
{
    sample_valid = sequence_known = false;
    info.rv.present = info.game.present = info.mag.present = false;
}

bool p4_imu_begin(uint8_t addr)
{
    sh2_lab_abort();
    sh2_close();
    ready = false;
    initializing = true;
    reconfigure = reset_pending = false;
    address = addr;
    p4_imu_lab_invalidate();
    info = {};
    info.autosave_rc = SH2_ERR;
    const uint32_t started = millis();
    diag.init_ms = 0;
    diag.probe_attempts = diag.probe_elapsed_ms = 0;
    diag.probe_4b_rc = diag.probe_4a_rc = -1;
    diag.probe_sda = diag.probe_scl = diag.rst_low = diag.rst_high = -1;
    p4_sparkfun_stage("I2C_BEGIN", SH2_ERR_IO);
    if (!Wire1.setBufferSize(MM1_LAB_IMU_I2C_BUFFER) ||
        !Wire1.begin(MM1_LAB_IMU_SDA, MM1_LAB_IMU_SCL, MM1_LAB_IMU_I2C_HZ)) {
        initializing = false;
        diag.init_ms = millis() - started;
        return false;
    }
    Wire1.setTimeOut(MM1_LAB_IMU_I2C_TIMEOUT_MS);
    // Datasheet v1.16 §6.5.3: NRST >=10 ns, startup >=90 ms + typ.4 ms.
    // INT is unwired. Start probing at 100 ms; this is not a guaranteed boot maximum.
    pinMode(MM1_LAB_IMU_RST, OUTPUT);
    gpio_input_enable((gpio_num_t)MM1_LAB_IMU_RST); // Observe NRST without disabling output.
    digitalWrite(MM1_LAB_IMU_RST, LOW);
    ++diag.resets;
    ++diag.hardware_resets;
    reset_seen = true;
    delay(10);
    diag.rst_low = gpio_get_level((gpio_num_t)MM1_LAB_IMU_RST);
    digitalWrite(MM1_LAB_IMU_RST, HIGH);
    reset_released_ms = millis();
    delay(100);
    diag.rst_high = gpio_get_level((gpio_num_t)MM1_LAB_IMU_RST);

    // Only two addresses on the confirmed wiring; no pin survey or bus swapping.
    const uint8_t addresses[] = {addr, uint8_t(addr == 0x4b ? 0x4a : 0x4b)};
    bool finished = false;
    while (!finished && millis() - reset_released_ms < MM1_LAB_IMU_PROBE_WINDOW_MS) {
        for (uint8_t candidate : addresses) {
            if (millis() - reset_released_ms >= MM1_LAB_IMU_PROBE_WINDOW_MS) break;
            address = candidate;
            p4_sparkfun_stage("PROBE", SH2_ERR_IO);
            // NRST is owned here. Passing -1 prevents a second reset in SparkFun.
            if (imu.begin(address, Wire1, -1, -1)) {
                sh2_setSensorCallback(sensor, nullptr); // Receive every report in mixed cargos.
                info.part = imu.prodIds.entry[0].swPartNumber;
                info.build = imu.prodIds.entry[0].swBuildNumber;
                info.major = imu.prodIds.entry[0].swVersionMajor;
                info.minor = imu.prodIds.entry[0].swVersionMinor;
                info.patch = imu.prodIds.entry[0].swVersionPatch;
                reconfigure = false;
                ready = configure();
                if (ready) p4_sparkfun_stage("READY", SH2_OK);
                finished = true;
                break;
            }
            // Retry only a missing ACK during readiness, not SH-2 initialization,
            // controller errors or timeouts. No reset or sensor command is retried.
            const bool nack = std::strcmp(diag.init_stage, "NO_ACK") == 0;
            sh2_close();
            if (!nack) { finished = true; break; }
        }
        const uint32_t elapsed = millis() - reset_released_ms;
        if (!finished && elapsed < MM1_LAB_IMU_PROBE_WINDOW_MS)
            delay(std::min(uint32_t(MM1_LAB_IMU_PROBE_INTERVAL_MS),
                           uint32_t(MM1_LAB_IMU_PROBE_WINDOW_MS) - elapsed));
    }
    if (!ready && std::strcmp(diag.init_stage, "NO_ACK") == 0) {
        address = addr;
        p4_sparkfun_stage("NO_ACK", SH2_ERR_IO);
    }
    initializing = false;
    diag.init_ms = millis() - started;
    Serial.printf("[LAB] IMU driver=%s revision=%s I2C%d SDA=%d SCL=%d RST=%d Hz=%d init=%s rc=%d ms=%lu part=%lu fw=%u.%u.%u build=%lu\n",
        MM1_LAB_IMU_LIBRARY, MM1_LAB_IMU_REVISION, MM1_LAB_IMU_I2C_PORT,
        MM1_LAB_IMU_SDA, MM1_LAB_IMU_SCL, MM1_LAB_IMU_RST, MM1_LAB_IMU_I2C_HZ,
        diag.init_stage, diag.init_rc, (unsigned long)diag.init_ms, (unsigned long)info.part,
        info.major, info.minor, info.patch, (unsigned long)info.build);
    Serial.printf("[LAB] IMU probe Wire rc: 0x4B=%d 0x4A=%d attempts=%lu after_release_ms=%lu SDA/SCL=%d/%d NRST_low/high=%d/%d (0=ACK 2=NACK 4=ERROR 5=TIMEOUT -1=NOT_TRIED)\n",
        diag.probe_4b_rc, diag.probe_4a_rc, (unsigned long)diag.probe_attempts,
        (unsigned long)diag.probe_elapsed_ms, diag.probe_sda, diag.probe_scl,
        diag.rst_low, diag.rst_high);
    return ready;
}
void p4_imu_request_reset() { reset_pending = true; }
void p4_imu_poll()
{
    if (reset_pending) { p4_imu_begin(address); return; }
    if (!ready) return;
    sh2_service();
    if (reconfigure) {
        reconfigure = false;
        p4_imu_lab_invalidate();
        ready = configure();
        if (ready) p4_sparkfun_stage("READY", SH2_OK);
    }
}
bool p4_imu_enable_reports()
{
    const int rc = p4_imu_lab_report(SH2_ROTATION_VECTOR, 20000);
    if (rc != SH2_OK) p4_sparkfun_stage("REPORTS", rc);
    return rc == SH2_OK;
}
int p4_imu_lab_report(uint8_t id, uint32_t interval_us)
{
    if (sh2_lab_busy()) return SH2_ERR_OP_IN_PROGRESS;
    uint32_t *interval = nullptr;
    switch (id) {
    case SH2_ROTATION_VECTOR: interval = &info.rv_interval_us; break;
    case SH2_GAME_ROTATION_VECTOR: interval = &info.game_interval_us; break;
    case SH2_MAGNETIC_FIELD_CALIBRATED: interval = &info.mag_interval_us; break;
    default: return SH2_ERR_BAD_PARAM;
    }
    sh2_SensorConfig_t config{};
    config.reportInterval_us = interval_us;
    const int rc = sh2_setSensorConfig(sh2_SensorId_t(id), &config);
    if (rc == SH2_OK) *interval = interval_us;
    return rc;
}
P4ImuLabInfo p4_imu_lab_info() { return info; }
P4ImuDiagnostics p4_imu_diagnostics() { diag.ready = ready; return diag; }
bool p4_imu_ok() { return ready; }
bool p4_imu_snapshot(P4ImuSample *out)
{
    if (!ready || !sample_valid || !out) return false;
    *out = sample;
    return true;
}
bool p4_imu_was_reset() { const bool result = reset_seen; reset_seen = false; return result; }
bool p4_imu_get_quat(float *w, float *x, float *y, float *z, int *accuracy)
{
    if (!ready || !sample_valid || !w || !x || !y || !z) return false;
    *w = sample.w; *x = sample.x; *y = sample.y; *z = sample.z;
    if (accuracy) *accuracy = sample.status;
    return true;
}
bool p4_imu_get_accel(float *, float *, float *) { return false; }
void p4_imu_scan(char *buf, size_t size) { if (buf && size) snprintf(buf, size, "%s", failure); }
