/**
 * @file p4_imu.cpp
 * @brief BNO086 SH-2 over software I2C on GPIO30/31.
 *
 * Hardware I2C_NUM_1 never ACKed on this board (2/3 or 28/29). Silk SDA/SCL
 * (GPIO7/8) is GT911 — a BNO there hangs touch. Bit-bang open-drain with clock
 * stretch, same SH-2 HAL as Adafruit_BNO08x. GPIO28/29 idle LOW on this header.
 */

#include "p4_imu.h"

#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <cmath>

#include <driver/gpio.h>

#include "bno08x/sh2.h"
#include "bno08x/sh2_SensorValue.h"
#include "bno08x/sh2_err.h"
#include "mm1_p4_pins.h"

namespace {

constexpr size_t kI2cChunk = 32;
constexpr int kHalfUs = 5;          /* ~100 kHz when idle */
constexpr int kStretchUs = 50000;   /* BNO086 stretches SCL on boot */

uint8_t g_addr = 0x4B;
bool g_ok = false;
bool g_reset = false;
sh2_Hal_t g_hal{};

float g_qw = 1.f, g_qx = 0.f, g_qy = 0.f, g_qz = 0.f;
int g_accuracy = 0;
bool g_have_quat = false;
float g_ax = 0.f, g_ay = 0.f, g_az = 0.f;
bool g_have_accel = false;
P4ImuSample g_sample{};
P4ImuDiagnostics g_diag{};
bool g_sample_valid = false, g_reconfigure = false, g_sequence_known = false;
uint8_t g_last_sequence = 0;
#if defined(MM1_LAB)
P4ImuLabInfo g_lab{};
void feedback(P4ImuFeedback &f, const sh2_SensorValue_t &v, float accuracy = 0)
{
    if (f.present && v.sequence == f.sequence) return;
    if (f.present && (uint8_t)(v.sequence-f.sequence) != 1) ++f.sequence_gaps;
    f.present = true;
    f.sequence = v.sequence;
    f.status = v.status & 3;
    ++f.generation;
    f.received_ms = millis();
    f.accuracy_rad = accuracy;
}
void autosave_off()
{
    g_lab.autosave_rc = sh2_setDcdAutoSave(false);
    Serial.printf("[LAB] DCD autosave disable rc=%d ack=NOT_DEFINED\n", g_lab.autosave_rc);
}
#endif
#if defined(MM1_LAB)
// Bound an entire HAL read/write, not each stretched clock separately.
// Full SH-2 packets (up to 384 payload bytes plus repeated I2C headers)
// need more than 30 ms on this software bus, particularly at initialization.
constexpr uint32_t kIoBudgetUs = 100000;
bool g_budget_active = false;
uint32_t g_budget_start = 0;
struct IoBudget {
    IoBudget() { g_budget_start = micros(); g_budget_active = true; }
    ~IoBudget() { g_budget_active = false; }
};
#endif

int g_sda = MM1_IMU_SDA;
int g_scl = MM1_IMU_SCL;
int g_idle_sda = -1;
int g_idle_scl = -1;
char g_fail_why[64] = "—";
bool g_any_ack = false;

void init_state(const char *stage, int rc)
{
    g_diag.init_stage = stage;
    g_diag.init_rc = rc;
    g_diag.address = g_addr;
    g_diag.sda = g_sda;
    g_diag.scl = g_scl;
    snprintf(g_fail_why, sizeof(g_fail_why), "%s rc=%d addr=0x%02X", stage, rc, g_addr);
}

int pin_idle(int pin)
{
    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
    delay(2);
    return gpio_get_level((gpio_num_t)pin);
}

void imu_pin_survey(void)
{
    const int pins[] = {28, 29, 30, 31};
    Serial.print("p4_imu: idle");
    for (int pin : pins) {
        Serial.printf(" %d=%d", pin, pin_idle(pin));
    }
    Serial.println();
}

void bb_delay(void)
{
    delayMicroseconds(kHalfUs);
}

void bb_release(int pin)
{
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
}

void bb_low(int pin)
{
    gpio_set_level((gpio_num_t)pin, 0);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
}

int bb_read(int pin)
{
    return gpio_get_level((gpio_num_t)pin);
}

bool bb_wait_scl_high(void)
{
    bb_release(g_scl);
#if defined(MM1_LAB)
    if (g_budget_active && (uint32_t)(micros()-g_budget_start) >= kIoBudgetUs) return false;
#endif
    const int32_t t0 = (int32_t)micros();
    while (!bb_read(g_scl)) {
#if defined(MM1_LAB)
        if (g_budget_active && (uint32_t)(micros()-g_budget_start) >= kIoBudgetUs) return false;
#endif
        if ((int32_t)micros() - t0 > kStretchUs) {
            return false;
        }
    }
    return true;
}

bool imu_bus_install(int sda, int scl)
{
    gpio_reset_pin((gpio_num_t)sda);
    gpio_reset_pin((gpio_num_t)scl);
    gpio_set_pull_mode((gpio_num_t)sda, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)scl, GPIO_PULLUP_ONLY);
    bb_release(sda);
    bb_release(scl);

    g_sda = sda;
    g_scl = scl;
    delay(20);
    g_idle_sda = bb_read(g_sda);
    g_idle_scl = bb_read(g_scl);
    Serial.printf("p4_imu: bitbang SDA=GPIO%d SCL=GPIO%d idle=%d/%d\n", sda, scl,
                  g_idle_sda, g_idle_scl);
    delay(50);
    return true;
}

bool bb_start(void)
{
    bb_release(g_sda);
    if (!bb_wait_scl_high()) {
        return false;
    }
    bb_delay();
    bb_low(g_sda);
    bb_delay();
    bb_low(g_scl);
    bb_delay();
    return true;
}

void bb_stop(void)
{
    bb_low(g_sda);
    bb_delay();
    (void)bb_wait_scl_high();
    bb_delay();
    bb_release(g_sda);
    bb_delay();
}

bool bb_write_bit(int bit)
{
    if (bit) {
        bb_release(g_sda);
    } else {
        bb_low(g_sda);
    }
    bb_delay();
    if (!bb_wait_scl_high()) {
        return false;
    }
    bb_delay();
    bb_low(g_scl);
    return true;
}

int bb_read_bit(void)
{
    bb_release(g_sda);
    bb_delay();
    if (!bb_wait_scl_high()) {
        return -1;
    }
    const int v = bb_read(g_sda);
    bb_delay();
    bb_low(g_scl);
    return v;
}

bool bb_write_byte(uint8_t value)
{
    for (int i = 7; i >= 0; i--) {
        if (!bb_write_bit((value >> i) & 1)) {
            return false;
        }
    }
    const int ack = bb_read_bit();
    return ack == 0;
}

int bb_read_byte(bool ack)
{
    uint8_t value = 0;
    for (int i = 7; i >= 0; i--) {
        const int b = bb_read_bit();
        if (b < 0) {
            return -1;
        }
        value = (uint8_t)((value << 1) | (b ? 1 : 0));
    }
    if (!bb_write_bit(ack ? 0 : 1)) {
        return -1;
    }
    return (int)value;
}

bool imu_probe_addr(uint8_t addr)
{
    for (int i = 0; i < 3; i++) {
        if (!bb_start()) {
            bb_stop();
            delay(20);
            continue;
        }
        const bool ack = bb_write_byte((uint8_t)(addr << 1));
        bb_stop();
        if (ack) {
            return true;
        }
        delay(20);
    }
    return false;
}

bool i2c_write_raw(const uint8_t *data, size_t len)
{
    if (data == nullptr || len == 0) {
        return false;
    }
    if (!bb_start()) {
        bb_stop();
        return false;
    }
    if (!bb_write_byte((uint8_t)(g_addr << 1))) {
        bb_stop();
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!bb_write_byte(data[i])) {
            bb_stop();
            return false;
        }
    }
    bb_stop();
    return true;
}

bool i2c_read_raw(uint8_t *data, size_t len)
{
    if (data == nullptr || len == 0) {
        return false;
    }
    if (!bb_start()) {
        bb_stop();
        return false;
    }
    if (!bb_write_byte((uint8_t)((g_addr << 1) | 1))) {
        bb_stop();
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        const int b = bb_read_byte(i + 1 < len);
        if (b < 0) {
            bb_stop();
            return false;
        }
        data[i] = (uint8_t)b;
    }
    bb_stop();
    return true;
}

int i2chal_open(sh2_Hal_t *self)
{
    (void)self;
    const uint8_t softreset_pkt[] = {5, 0, 1, 0, 1};
    if (!i2c_write_raw(softreset_pkt, sizeof(softreset_pkt))) {
        delay(50);
        if (!i2c_write_raw(softreset_pkt, sizeof(softreset_pkt))) {
            return -1;
        }
    }
    delay(500);
    return 0;
}

void i2chal_close(sh2_Hal_t *self)
{
    (void)self;
}

int i2chal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us)
{
#if defined(MM1_LAB)
    IoBudget budget;
#endif
    (void)self;
    if (t_us) {
        *t_us = (uint32_t)(micros());
    }

    uint8_t header[4];
    if (!i2c_read_raw(header, 4)) {
        ++g_diag.io_errors;
        return 0;
    }

    uint16_t packet_size = (uint16_t)header[0] | ((uint16_t)header[1] << 8);
    packet_size &= ~0x8000U;
    if (packet_size == 0) {
        ++g_diag.empty_reads;
        return 0;
    }
    if (packet_size < 4 || packet_size > len) {
        ++g_diag.io_errors;
        return 0;
    }

    uint16_t cargo_remaining = packet_size;
    uint8_t chunk[kI2cChunk];
    bool first = true;

    while (cargo_remaining > 0) {
        size_t read_size;
        if (first) {
            read_size = std::min(kI2cChunk, (size_t)cargo_remaining);
        } else {
            read_size = std::min(kI2cChunk, (size_t)cargo_remaining + 4U);
        }
        if (!i2c_read_raw(chunk, read_size)) {
            ++g_diag.io_errors;
            return 0;
        }
        uint16_t cargo_read;
        if (first) {
            cargo_read = (uint16_t)read_size;
            std::memcpy(pBuffer, chunk, cargo_read);
            first = false;
        } else {
            cargo_read = (uint16_t)(read_size - 4);
            std::memcpy(pBuffer, chunk + 4, cargo_read);
        }
        pBuffer += cargo_read;
        cargo_remaining = (uint16_t)(cargo_remaining - cargo_read);
    }
    return (int)packet_size;
}

int i2chal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len)
{
#if defined(MM1_LAB)
    IoBudget budget;
    if (len > kI2cChunk) {
        ++g_diag.io_errors;
        return -1; // Never acknowledge a truncated SHTP write as success.
    }
#endif
    (void)self;
    const uint16_t write_size = (uint16_t)std::min(kI2cChunk, (size_t)len);
    if (!i2c_write_raw(pBuffer, write_size)) {
        ++g_diag.io_errors;
#if defined(MM1_LAB)
        return -1; // SHTP retries zero forever; a bus failure is not backpressure.
#else
        return 0;
#endif
    }
    return (int)write_size;
}

uint32_t hal_getTimeUs(sh2_Hal_t *self)
{
    (void)self;
    return (uint32_t)micros();
}

void hal_callback(void *cookie, sh2_AsyncEvent_t *pEvent)
{
    (void)cookie;
    if (pEvent && pEvent->eventId == SH2_RESET) {
#if defined(MM1_LAB)
        sh2_lab_abort();
        p4_imu_lab_invalidate();
        g_lab.autosave_rc = SH2_ERR;
        g_lab.game_interval_us = g_lab.mag_interval_us = 0;
#endif
        g_reset = true;
        g_reconfigure = true;
        g_sample_valid = g_have_quat = g_have_accel = false;
        g_sequence_known = false;
        ++g_diag.resets;
    }
}

void sensor_handler(void *cookie, sh2_SensorEvent_t *event)
{
    (void)cookie;
    sh2_SensorValue_t value;
    if (sh2_decodeSensorEvent(&value, event) != SH2_OK) {
        ++g_diag.decode_errors;
        return;
    }
    if (value.sensorId == SH2_ROTATION_VECTOR) {
        if (g_sequence_known && (uint8_t)(value.sequence-g_last_sequence) != 1)
            ++g_diag.sequence_gaps;
#if defined(MM1_LAB)
        if (g_sequence_known && value.sequence == g_last_sequence) return;
#endif
        g_sequence_known = true;
        g_last_sequence = value.sequence;
        g_sample = {value.un.rotationVector.real, value.un.rotationVector.i,
                    value.un.rotationVector.j, value.un.rotationVector.k,
                    value.un.rotationVector.accuracy, (uint8_t)(value.status & 3),
                    value.sequence, ++g_diag.generation};
        g_sample_valid = true;
#if defined(MM1_LAB)
        if (std::isfinite(value.un.rotationVector.accuracy) && value.un.rotationVector.accuracy >= 0)
            feedback(g_lab.rv, value, value.un.rotationVector.accuracy);
#endif
        g_qw = value.un.rotationVector.real;
        g_qx = value.un.rotationVector.i;
        g_qy = value.un.rotationVector.j;
        g_qz = value.un.rotationVector.k;
        g_accuracy = (int)value.un.rotationVector.accuracy;
        g_have_quat = true;
#if defined(MM1_LAB)
    } else if (value.sensorId == SH2_GAME_ROTATION_VECTOR) {
        feedback(g_lab.game, value);
    } else if (value.sensorId == SH2_MAGNETIC_FIELD_CALIBRATED) {
        if (std::isfinite(value.un.magneticField.x) && std::isfinite(value.un.magneticField.y) &&
            std::isfinite(value.un.magneticField.z)) feedback(g_lab.mag, value);
#endif
    } else if (value.sensorId == SH2_ACCELEROMETER) {
        g_ax = value.un.accelerometer.x;
        g_ay = value.un.accelerometer.y;
        g_az = value.un.accelerometer.z;
        g_have_accel = true;
    }
}

bool enable_one(sh2_SensorId_t id, uint32_t interval_us)
{
    sh2_SensorConfig_t config{};
    config.reportInterval_us = interval_us;
    g_diag.init_rc = sh2_setSensorConfig(id, &config);
    return g_diag.init_rc == SH2_OK;
}

bool imu_try_open(uint8_t addr)
{
    g_addr = addr;
    if (!imu_probe_addr(addr)) {
        return false;
    }

    g_any_ack = true;
    init_state("SH2_OPEN", SH2_OK);
    g_hal.open = i2chal_open;
    g_hal.close = i2chal_close;
    g_hal.read = i2chal_read;
    g_hal.write = i2chal_write;
    g_hal.getTimeUs = hal_getTimeUs;

    int rc = sh2_open(&g_hal, hal_callback, nullptr);
    if (rc != SH2_OK) {
        init_state("SH2_OPEN", rc);
        Serial.printf("p4_imu: sh2_open failed @0x%02X rc=%d\n", addr, rc);
        sh2_close();
        return false;
    }

    sh2_ProductIds_t prod{};
    init_state("PRODUCT_IDS", SH2_OK);
    rc = sh2_getProdIds(&prod);
    if (rc != SH2_OK) {
        init_state("PRODUCT_IDS", rc);
        Serial.printf("p4_imu: getProdIds failed @0x%02X rc=%d\n", addr, rc);
#if defined(MM1_LAB)
        const auto d = sh2_lab_boot_diagnostics();
        Serial.printf("p4_imu: product replies=%u/%u control_rx=%lu unknown=%lu truncated=%lu\n",
                      d.product_received, d.product_expected, (unsigned long)d.control_packets,
                      (unsigned long)d.unknown_reports, (unsigned long)d.truncated_reports);
#endif
        sh2_close();
        return false;
    }

    sh2_setSensorCallback(sensor_handler, nullptr);
    if (!p4_imu_enable_reports()) {
        init_state("REPORTS", g_diag.init_rc);
        Serial.printf("p4_imu: enable reports failed rc=%d\n", g_diag.init_rc);
        sh2_close();
        return false;
    }

#if defined(MM1_LAB)
    g_lab.part = prod.entry[0].swPartNumber;
    g_lab.build = prod.entry[0].swBuildNumber;
    g_lab.major = prod.entry[0].swVersionMajor;
    g_lab.minor = prod.entry[0].swVersionMinor;
    g_lab.patch = prod.entry[0].swVersionPatch;
    autosave_off();
#endif
    g_ok = true;
    g_reconfigure = false;
    init_state("READY", SH2_OK);
    Serial.printf("p4_imu: OK addr=0x%02X SDA=%d SCL=%d part=%u\n", addr, g_sda,
                  g_scl, (unsigned)prod.entry[0].swPartNumber);
    return true;
}

} // namespace

bool p4_imu_enable_reports(void)
{
#if defined(MM1_LAB)
    const int rc = p4_imu_lab_report(SH2_ROTATION_VECTOR, 20000);
    g_diag.init_rc = rc;
    return rc == SH2_OK;
#else
    return enable_one(SH2_ROTATION_VECTOR, 20000) &&
           enable_one(SH2_ACCELEROMETER, 50000);
#endif
}

bool p4_imu_begin(uint8_t i2c_addr)
{
#if defined(MM1_LAB)
    sh2_lab_abort();
    g_lab = {};
    g_lab.autosave_rc = SH2_ERR;
#endif
    g_ok = false;
    g_have_quat = false;
    g_have_accel = false;
    g_sample_valid = false;
    g_reconfigure = g_reset = g_sequence_known = false;
    g_addr = i2c_addr;
    g_any_ack = false;
    init_state("PROBE", SH2_OK);

#if !defined(MM1_LAB)
    imu_pin_survey();
#endif

    const uint8_t addrs[2] = {i2c_addr, (uint8_t)((i2c_addr == 0x4B) ? 0x4A : 0x4B)};
    /* GPIO28/29 read stuck LOW on this board (J3 pin 20 is GND on a Pi-style
     * header). GPIO30/31 idle high — use those. */
    const int pairs[][2] = {
#if defined(MM1_LAB)
        // Lab wiring confirmed on the bench: SDA=GPIO31, SCL=GPIO30.
        {31, 30},
#else
        {30, 31},
        {28, 29},
#endif
    };

    for (const auto &pair : pairs) {
        const int sda = pair[0];
        const int scl = pair[1];
        if (pin_idle(sda) == 0 && pin_idle(scl) == 0) {
            Serial.printf("p4_imu: skip GPIO%d/%d (both stuck LOW)\n", sda, scl);
            continue;
        }
        if (!imu_bus_install(sda, scl)) {
            continue;
        }
        for (uint8_t a : addrs) {
            if (imu_try_open(a)) {
                return true;
            }
        }
#if !defined(MM1_LAB)
        Serial.printf("p4_imu: no ACK on %d/%d — trying SDA/SCL swapped\n", sda, scl);
        if (!imu_bus_install(scl, sda)) {
            continue;
        }
        for (uint8_t a : addrs) {
            if (imu_try_open(a)) {
                Serial.println("p4_imu: works with SDA/SCL swapped — swap the two wires");
                return true;
            }
        }
#endif
    }

    // A later NACK at the alternative address must not erase an SH-2 failure.
    if (!g_any_ack) {
        init_state("NO_ACK", SH2_ERR_IO);
        snprintf(g_fail_why, sizeof(g_fail_why), "noACK idle=%d/%d", g_idle_sda, g_idle_scl);
    }
    Serial.printf("p4_imu: init failed: %s\n", g_fail_why);
    return false;
}

void p4_imu_poll(void)
{
    if (!g_ok) {
        return;
    }
    sh2_service();
    if (g_reconfigure) {
        g_reconfigure = false;
        g_sample_valid = g_have_quat = g_have_accel = false;
        g_ok = p4_imu_enable_reports();
#if defined(MM1_LAB)
        autosave_off();
#endif
        init_state(g_ok ? "READY" : "RESET_REPORTS", g_diag.init_rc);
        if (!g_ok) Serial.printf("p4_imu: reset report re-enable failed rc=%d\n", g_diag.init_rc);
    }
}

bool p4_imu_ok(void)
{
    return g_ok;
}

bool p4_imu_snapshot(P4ImuSample *sample)
{
    if (!g_ok || !g_sample_valid || !sample) return false;
    *sample = g_sample;
    return true;
}

P4ImuDiagnostics p4_imu_diagnostics()
{
    g_diag.ready = g_ok;
    return g_diag;
}

bool p4_imu_was_reset(void)
{
    const bool r = g_reset;
    g_reset = false;
    return r;
}

bool p4_imu_get_quat(float *qw, float *qx, float *qy, float *qz, int *accuracy)
{
    if (!g_have_quat || !qw) {
        return false;
    }
    *qw = g_qw;
    *qx = g_qx;
    *qy = g_qy;
    *qz = g_qz;
    if (accuracy) {
        *accuracy = g_accuracy;
    }
    return true;
}

bool p4_imu_get_accel(float *ax, float *ay, float *az)
{
    if (!g_have_accel || !ax) {
        return false;
    }
    *ax = g_ax;
    *ay = g_ay;
    *az = g_az;
    return true;
}

void p4_imu_scan(char *buf, size_t buflen)
{
    if (buf == nullptr || buflen == 0) {
        return;
    }
    snprintf(buf, buflen, "%s", g_fail_why);
}

#if defined(MM1_LAB)
P4ImuLabInfo p4_imu_lab_info() { return g_lab; }
void p4_imu_lab_invalidate()
{
    g_sample_valid = g_have_quat = g_have_accel = false;
    g_sequence_known = false;
    // Keep counters monotonic but invalidate all evidence of recency.
    g_lab.rv.present = g_lab.game.present = g_lab.mag.present = false;
}
int p4_imu_lab_report(uint8_t id, uint32_t interval_us)
{
    if (sh2_lab_busy()) return SH2_ERR_OP_IN_PROGRESS;
    uint32_t *interval = nullptr;
    switch (id) {
    case SH2_ROTATION_VECTOR: interval = &g_lab.rv_interval_us; break;
    case SH2_GAME_ROTATION_VECTOR: interval = &g_lab.game_interval_us; break;
    case SH2_MAGNETIC_FIELD_CALIBRATED: interval = &g_lab.mag_interval_us; break;
    default: return SH2_ERR_BAD_PARAM;
    }
    sh2_SensorConfig_t config{};
    config.reportInterval_us = interval_us;
    const int rc = sh2_setSensorConfig((sh2_SensorId_t)id, &config);
    if (rc == SH2_OK) *interval = interval_us;
    return rc;
}
#endif
