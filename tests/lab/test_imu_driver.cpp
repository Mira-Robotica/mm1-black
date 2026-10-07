// Exercise actual driver callback/reset and bit-banged HAL code. Only GPIO,
// clock and SH-2 entry points are simulated; no firmware or sensor is accessed.
#include "board/p4/p4_imu.cpp"
#include <cassert>
#include <iostream>
#include <vector>

sh2_SensorValue_t decoded{};
int decode_result = SH2_OK, config_result = SH2_OK;
int open_result = SH2_OK, product_result = SH2_OK;
std::vector<sh2_SensorId_t> enabled;
int sh2_decodeSensorEvent(sh2_SensorValue_t *v, const sh2_SensorEvent_t *) {
    *v = decoded; return decode_result;
}
int sh2_open(sh2_Hal_t *, sh2_EventCallback_t *, void *) { return open_result; }
void sh2_close() {}
void sh2_service() {}
#if defined(MM1_LAB)
int autosave_result = SH2_OK;
int autosave_calls = 0;
int sh2_setDcdAutoSave(bool enabled) { assert(!enabled); ++autosave_calls; return autosave_result; }
bool sh2_lab_busy() { return false; }
void sh2_lab_abort() {}
sh2_LabBootDiagnostics sh2_lab_boot_diagnostics() { return {}; }
#endif
int sh2_setSensorCallback(sh2_SensorCallback_t *, void *) { return SH2_OK; }
int sh2_getProdIds(sh2_ProductIds_t *) { return product_result; }
int sh2_setSensorConfig(sh2_SensorId_t id, const sh2_SensorConfig_t *c) {
    enabled.push_back(id);
#if !defined(MM1_LAB)
    assert(c->reportInterval_us == (id == SH2_ROTATION_VECTOR ? 20000u : 50000u));
#else
    assert(c->reportInterval_us <= 100000);
#endif
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
    assert(autosave_calls == 1 && p4_imu_lab_info().autosave_rc == SH2_OK);
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
#if defined(MM1_LAB)
    assert(autosave_calls == 2);
    assert(p4_imu_lab_report(SH2_MAGNETIC_FIELD_CALIBRATED,20000) == SH2_OK);
    assert(p4_imu_lab_report(SH2_GAME_ROTATION_VECTOR,50000) == SH2_OK);
    assert(p4_imu_lab_report(SH2_ROTATION_VECTOR,100000) == SH2_OK);
    decoded.sensorId=SH2_MAGNETIC_FIELD_CALIBRATED; decoded.sequence=255; decoded.status=2;
    decoded.un.magneticField = {10,20,30};
    const auto rv_generation=g_diag.generation;
    sensor_handler(nullptr,nullptr); auto feedback1=p4_imu_lab_info();
    assert(feedback1.mag.present && feedback1.mag.status == 2 && feedback1.mag.generation == 1);
    fake_ms+=100; sensor_handler(nullptr,nullptr);
    assert(p4_imu_lab_info().mag.received_ms == feedback1.mag.received_ms); // Duplicate is not recent.
    decoded.sequence=0; sensor_handler(nullptr,nullptr);
    assert(p4_imu_lab_info().mag.generation == 2 && p4_imu_lab_info().mag.sequence_gaps == 0);
    decoded.sensorId=SH2_GAME_ROTATION_VECTOR; sensor_handler(nullptr,nullptr);
    assert(p4_imu_lab_info().game.present && g_diag.generation == rv_generation);
    p4_imu_lab_invalidate();
    assert(!p4_imu_lab_info().mag.present && !p4_imu_lab_info().game.present && !p4_imu_snapshot(&sample));
    autosave_result=SH2_ERR_IO; hal_callback(nullptr,&event); p4_imu_poll();
    assert(p4_imu_ok() && p4_imu_lab_info().autosave_rc == SH2_ERR_IO);
#endif
    decode_result=SH2_ERR; sensor_handler(nullptr,nullptr);
    assert(g_diag.decode_errors == 1);
    config_result=SH2_ERR; hal_callback(nullptr,&event); p4_imu_poll();
    assert(!p4_imu_ok() && !p4_imu_snapshot(&sample));

#if defined(MM1_LAB)
    char reason[64];
    // Preferred address ACKs; the absent alternate address must not replace the error.
    open_result = SH2_ERR_TIMEOUT; fake_sda_bits = {1,1,0};
    assert(!p4_imu_begin(0x4B)); p4_imu_scan(reason,sizeof(reason));
    assert(strstr(reason,"SH2_OPEN") && !strstr(reason,"noACK"));
    assert(g_diag.init_rc == SH2_ERR_TIMEOUT && g_diag.address == 0x4B);
    assert(g_diag.sda == 31 && g_diag.scl == 30);
    open_result = SH2_OK; product_result = SH2_ERR_TIMEOUT; fake_sda_bits = {1,1,0};
    assert(!p4_imu_begin(0x4B)); p4_imu_scan(reason,sizeof(reason));
    assert(strstr(reason,"PRODUCT_IDS") && g_diag.init_rc == SH2_ERR_TIMEOUT);
    product_result = SH2_OK; fake_sda_bits = {1,1,0};
    assert(!p4_imu_begin(0x4B));
    assert(!strcmp(g_diag.init_stage,"REPORTS") && g_diag.init_rc == SH2_ERR);
    config_result = SH2_OK; fake_sda_bits.clear();
    assert(!p4_imu_begin(0x4B)); p4_imu_scan(reason,sizeof(reason));
    assert(!strcmp(g_diag.init_stage,"NO_ACK") && strstr(reason,"noACK"));
    fake_sda_bits = {1,1,0}; assert(p4_imu_begin(0x4B));
    assert(!strcmp(g_diag.init_stage,"READY") && g_diag.init_rc == 0);
#endif

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
