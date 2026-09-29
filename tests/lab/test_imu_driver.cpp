// Exercise actual driver callback/reset and bit-banged HAL code. Only GPIO,
// clock and SH-2 entry points are simulated; no firmware or sensor is accessed.
#include "board/p4/p4_imu.cpp"
#include <cassert>
#include <iostream>
#include <vector>

sh2_SensorValue_t decoded{};
int decode_result = SH2_OK, config_result = SH2_OK;
std::vector<sh2_SensorId_t> enabled;
int sh2_decodeSensorEvent(sh2_SensorValue_t *v, const sh2_SensorEvent_t *) {
    *v = decoded; return decode_result;
}
int sh2_open(sh2_Hal_t *, sh2_EventCallback_t *, void *) { return SH2_OK; }
void sh2_close() {}
void sh2_service() {}
int sh2_setSensorCallback(sh2_SensorCallback_t *, void *) { return SH2_OK; }
int sh2_getProdIds(sh2_ProductIds_t *) { return SH2_OK; }
int sh2_setSensorConfig(sh2_SensorId_t id, const sh2_SensorConfig_t *c) {
    enabled.push_back(id);
    assert(c->reportInterval_us == (id == SH2_ROTATION_VECTOR ? 20000u : 50000u));
    return config_result;
}
void queue_read(const std::vector<uint8_t> &bytes) {
    fake_sda_bits.push_back(0); // I2C address ACK.
    for (auto byte : bytes)
        for (int bit=7; bit>=0; --bit) fake_sda_bits.push_back((byte >> bit) & 1);
}
int main() {
#if defined(MM1_LAB)
    // Physical lab fixture: SCL=30, SDA=31. Idle reads followed by address ACK.
    fake_scl_pin = 30;
    fake_sda_bits = {1,1,0};
#else
    // Original firmware also surveys GPIO28/29/30 before probing SDA=30.
    fake_sda_bits = {1,1,1,1,1,0};
#endif
    assert(p4_imu_begin(0x4B));
#if defined(MM1_LAB)
    assert(g_sda == 31 && g_scl == 30);
    assert(enabled == std::vector<sh2_SensorId_t>{SH2_ROTATION_VECTOR});
#else
    assert(g_sda == 30 && g_scl == 31);
    assert(enabled == (std::vector<sh2_SensorId_t>{SH2_ROTATION_VECTOR,SH2_ACCELEROMETER}));
#endif
    assert(fake_sda_bits.empty());
    decoded.sensorId=SH2_ROTATION_VECTOR; decoded.sequence=255; decoded.status=3;
    decoded.un.rotationVector = {0,0,0,1,.025f};
    g_ok=true; sensor_handler(nullptr,nullptr);
    P4ImuSample sample{};
    assert(p4_imu_snapshot(&sample) && sample.accuracy_rad == .025f && sample.status == 3);
    decoded.sequence=0; sensor_handler(nullptr,nullptr); // Natural 8-bit rollover.
    assert(g_diag.generation == 2 && g_diag.sequence_gaps == 0);
#if defined(MM1_LAB)
    sensor_handler(nullptr,nullptr); // A duplicate cannot be a fresh sample.
    assert(g_diag.generation == 2 && g_diag.sequence_gaps == 1);
#endif
    sh2_AsyncEvent_t event{}; event.eventId=SH2_RESET;
    hal_callback(nullptr,&event);
    assert(!p4_imu_snapshot(&sample) && g_diag.resets == 1);
    p4_imu_poll();
    assert(p4_imu_was_reset() && !p4_imu_was_reset());
    assert(!p4_imu_snapshot(&sample));
    sensor_handler(nullptr,nullptr); assert(p4_imu_snapshot(&sample));
    decode_result=SH2_ERR; sensor_handler(nullptr,nullptr);
    assert(g_diag.decode_errors == 1);
    config_result=SH2_ERR; hal_callback(nullptr,&event); p4_imu_poll();
    assert(!p4_imu_ok() && !p4_imu_snapshot(&sample));

    uint8_t buffer[384]{}; uint32_t timestamp=0;
    const auto errors=g_diag.io_errors, empty=g_diag.empty_reads;
    assert(i2chal_read(nullptr,buffer,sizeof(buffer),&timestamp) == 0); // NACK is not an empty FIFO.
    assert(g_diag.io_errors == errors+1 && g_diag.empty_reads == empty);
    queue_read({0,0,0,0});
    assert(i2chal_read(nullptr,buffer,sizeof(buffer),&timestamp) == 0);
    assert(g_diag.empty_reads == empty+1);
    // A large advertisement must fit the total HAL budget at software-I2C speed.
    queue_read({0x80,1,2,3});
    std::vector<uint8_t> packet(384,0x55); packet[0]=0x80;packet[1]=1;packet[2]=2;packet[3]=3;
    size_t offset=0;
    while (offset < packet.size()) {
        std::vector<uint8_t> chunk;
        if (offset) chunk={0x80,1,2,3};
        while (chunk.size()<32 && offset<packet.size()) chunk.push_back(packet[offset++]);
        queue_read(chunk);
    }
    assert(i2chal_read(nullptr,buffer,sizeof(buffer),&timestamp) == 384);
    assert(!memcmp(buffer,packet.data(),384) && fake_sda_bits.empty());
#if defined(MM1_LAB)
    assert(i2chal_write(nullptr,buffer,21) == -1); // Bus failure must not trigger infinite SHTP retry.
    assert(i2chal_write(nullptr,buffer,33) == -1); // No truncated transmission.
    fake_scl_stuck=true; const uint32_t start=micros();
    assert(i2chal_read(nullptr,buffer,sizeof(buffer),&timestamp) == 0);
    assert((uint32_t)(micros()-start) < 101000);
    assert(!g_budget_active);
#endif
    std::cout << "IMU driver tests passed\n";
}
