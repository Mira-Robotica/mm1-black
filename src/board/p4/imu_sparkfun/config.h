#pragma once

// Bench wiring, deliberately independent from the production pin map.
#define MM1_LAB_IMU_SDA 31
#define MM1_LAB_IMU_SCL 30
#define MM1_LAB_IMU_RST 32
#define MM1_LAB_IMU_I2C_PORT 1
#define MM1_LAB_IMU_I2C_HZ 100000
#define MM1_LAB_IMU_I2C_TIMEOUT_MS 50
#define MM1_LAB_IMU_I2C_BUFFER 128
// Readiness window after releasing NRST; not an unconditional boot delay.
#define MM1_LAB_IMU_PROBE_WINDOW_MS 500
#define MM1_LAB_IMU_PROBE_INTERVAL_MS 10
#define MM1_LAB_IMU_LIBRARY "SparkFun-1.0.6"
#define MM1_LAB_IMU_REVISION "fd0149c+mm1.2"

#define MM1_LAB_STRINGIFY_(value) #value
#define MM1_LAB_STRINGIFY(value) MM1_LAB_STRINGIFY_(value)
#define MM1_LAB_IMU_META \
    "imu_transport=hardware_i2c imu_i2c_port=" MM1_LAB_STRINGIFY(MM1_LAB_IMU_I2C_PORT) " " \
    "imu_sda=" MM1_LAB_STRINGIFY(MM1_LAB_IMU_SDA) " imu_scl=" MM1_LAB_STRINGIFY(MM1_LAB_IMU_SCL) " " \
    "imu_rst=" MM1_LAB_STRINGIFY(MM1_LAB_IMU_RST) " imu_hz=" MM1_LAB_STRINGIFY(MM1_LAB_IMU_I2C_HZ) " " \
    "imu_library=" MM1_LAB_IMU_LIBRARY " imu_revision=" MM1_LAB_IMU_REVISION " "
