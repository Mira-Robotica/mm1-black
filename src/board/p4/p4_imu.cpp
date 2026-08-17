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

int g_sda = MM1_IMU_SDA;
int g_scl = MM1_IMU_SCL;
int g_idle_sda = -1;
int g_idle_scl = -1;
char g_fail_why[64] = "—";

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
    const int32_t t0 = (int32_t)micros();
    while (!bb_read(g_scl)) {
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
    (void)self;
    if (t_us) {
        *t_us = (uint32_t)(micros());
    }

    uint8_t header[4];
    if (!i2c_read_raw(header, 4)) {
        return 0;
    }

    uint16_t packet_size = (uint16_t)header[0] | ((uint16_t)header[1] << 8);
    packet_size &= ~0x8000U;
    if (packet_size == 0 || packet_size > len) {
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
    (void)self;
    const uint16_t write_size = (uint16_t)std::min(kI2cChunk, (size_t)len);
    if (!i2c_write_raw(pBuffer, write_size)) {
        return 0;
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
        g_reset = true;
    }
}

void sensor_handler(void *cookie, sh2_SensorEvent_t *event)
{
    (void)cookie;
    sh2_SensorValue_t value;
    if (sh2_decodeSensorEvent(&value, event) != SH2_OK) {
        return;
    }
    if (value.sensorId == SH2_ROTATION_VECTOR) {
        g_qw = value.un.rotationVector.real;
        g_qx = value.un.rotationVector.i;
        g_qy = value.un.rotationVector.j;
        g_qz = value.un.rotationVector.k;
        g_accuracy = (int)value.un.rotationVector.accuracy;
        g_have_quat = true;
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
    return sh2_setSensorConfig(id, &config) == SH2_OK;
}

bool imu_try_open(uint8_t addr)
{
    g_addr = addr;
    if (!imu_probe_addr(addr)) {
        return false;
    }

    g_hal.open = i2chal_open;
    g_hal.close = i2chal_close;
    g_hal.read = i2chal_read;
    g_hal.write = i2chal_write;
    g_hal.getTimeUs = hal_getTimeUs;

    if (sh2_open(&g_hal, hal_callback, nullptr) != SH2_OK) {
        Serial.printf("p4_imu: sh2_open failed @0x%02X\n", addr);
        sh2_close();
        snprintf(g_fail_why, sizeof(g_fail_why), "sh2_open @0x%02X", addr);
        return false;
    }

    sh2_ProductIds_t prod{};
    if (sh2_getProdIds(&prod) != SH2_OK) {
        Serial.printf("p4_imu: getProdIds failed @0x%02X\n", addr);
        sh2_close();
        snprintf(g_fail_why, sizeof(g_fail_why), "prodIds @0x%02X", addr);
        return false;
    }

    sh2_setSensorCallback(sensor_handler, nullptr);
    if (!enable_one(SH2_ROTATION_VECTOR, 20000) ||
        !enable_one(SH2_ACCELEROMETER, 50000)) {
        Serial.println("p4_imu: enable reports failed");
        sh2_close();
        snprintf(g_fail_why, sizeof(g_fail_why), "reports @0x%02X", addr);
        return false;
    }

    g_ok = true;
    Serial.printf("p4_imu: OK addr=0x%02X SDA=%d SCL=%d part=%u\n", addr, g_sda,
                  g_scl, (unsigned)prod.entry[0].swPartNumber);
    return true;
}

} // namespace

bool p4_imu_enable_reports(void)
{
    return enable_one(SH2_ROTATION_VECTOR, 20000) &&
           enable_one(SH2_ACCELEROMETER, 50000);
}

bool p4_imu_begin(uint8_t i2c_addr)
{
    g_ok = false;
    g_have_quat = false;
    g_have_accel = false;
    g_addr = i2c_addr;

    imu_pin_survey();

    const uint8_t addrs[2] = {i2c_addr, (uint8_t)((i2c_addr == 0x4B) ? 0x4A : 0x4B)};
    /* GPIO28/29 read stuck LOW on this board (J3 pin 20 is GND on a Pi-style
     * header). GPIO30/31 idle high — use those. */
    const int pairs[][2] = {{30, 31}, {28, 29}};

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
    }

    snprintf(g_fail_why, sizeof(g_fail_why), "noACK idle=%d/%d", g_idle_sda,
             g_idle_scl);
    return false;
}

void p4_imu_poll(void)
{
    if (!g_ok) {
        return;
    }
    sh2_service();
    if (g_reset) {
        g_reset = false;
        p4_imu_enable_reports();
    }
}

bool p4_imu_ok(void)
{
    return g_ok;
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
