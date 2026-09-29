#include "capture_service.h"
#include "settings.h"
#include "board/p4/mm1_p4_pins.h"
#include <Arduino.h>
#include <cmath>

namespace lab {
namespace {
enum class Phase { Idle, Laser, DrainImu, WaitImu, Ready };
Phase phase = Phase::Idle;
Sample sample;
uint32_t started = 0, imu_started = 0, baseline = 0;
P4ImuDiagnostics barrier{};
void imu_finish(const char *error) { sample.imu_error = error; phase = Phase::Ready; }
}
void capture_begin()
{
    laser_begin();
    const bool ready = p4_imu_begin(MM1_IMU_ADDR);
    Serial.printf("[LAB] IMU=%s RV=50Hz acceleration=OFF magnetic_offset=0 laser_trim=0\n",
                  ready ? "READY" : "NOT_READY");
}
bool capture_busy() { return phase != Phase::Idle; }
bool capture_ready() { return phase == Phase::Ready; }
const Sample &capture_sample() { return sample; }
const char *capture_state() { return capture_busy() ? "BUSY" : "IDLE"; }
void capture_cancel() { laser_cancel(); phase = Phase::Idle; }
bool capture_start(uint32_t id)
{
    if (capture_busy()) return false;
    sample = {}; sample.request_id = id;
    started = millis(); phase = Phase::Laser;
    laser_start();
    return true;
}
void capture_tick()
{
    laser_tick();
    p4_imu_poll(); // Always service fusion, including idle and laser wait.
    const uint32_t now = millis();
    if (phase == Phase::Laser) {
        if (now-started >= 4500 && !laser_done()) {
            laser_cancel();
            sample.laser = {false, 0, "LASER_DEADLINE"};
        } else if (laser_done()) sample.laser = laser_result();
        else return;
        // Begin after this tick's polling. Cached data cannot satisfy this request.
        barrier = p4_imu_diagnostics();
        imu_started = now;
        phase = Phase::DrainImu;
    }
    if (phase != Phase::DrainImu && phase != Phase::WaitImu) return;
    const auto diag = p4_imu_diagnostics();
    if (!diag.ready) { imu_finish("IMU_NOT_READY"); return; }
    if (diag.resets != barrier.resets) { imu_finish("IMU_RESET"); return; }
    if (diag.io_errors != barrier.io_errors) { imu_finish("IMU_IO_ERROR"); return; }
    if (diag.decode_errors != barrier.decode_errors) { imu_finish("IMU_DECODE_ERROR"); return; }
    if (now-imu_started >= LAB_IMU_TIMEOUT_MS) {
        imu_finish(phase == Phase::DrainImu ? "IMU_BACKLOG" : "IMU_TIMEOUT"); return;
    }
    if (phase == Phase::DrainImu) {
        // A successful empty HAL read proves the pending FIFO was drained.
        if (diag.empty_reads != barrier.empty_reads) {
            baseline = diag.generation;
            phase = Phase::WaitImu;
        }
        return;
    }
    P4ImuSample fresh{};
    if (!p4_imu_snapshot(&fresh) || fresh.generation == baseline) return;
    sample.imu = fresh;
    sample.imu_received = true;
    sample.angles = orientation(fresh.w, fresh.x, fresh.y, fresh.z,
                                IMU_LASER_AXIS_BX, IMU_LASER_AXIS_BY, IMU_LASER_AXIS_BZ);
    if (!sample.angles.quaternion_valid) { imu_finish("IMU_BAD_QUAT"); return; }
    if (!std::isfinite(fresh.accuracy_rad) || fresh.accuracy_rad < 0) {
        imu_finish("IMU_INVALID_ACCURACY"); return;
    }
    sample.imu_valid = true; // status=0 remains valid data with explicitly low confidence.
    imu_finish("NONE");
}
}
