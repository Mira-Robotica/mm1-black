#include "capture_service.h"
#include "calibration_service.h"
#include "settings.h"
#include "board/p4/mm1_p4_pins.h"
#include <Arduino.h>
#include <cmath>

namespace lab {
namespace {
enum class Phase { Idle, Laser, DrainImu, WaitImu, Ready };
Phase phase = Phase::Idle;
Sample sample;
uint32_t started = 0, imu_started = 0, baseline = 0, game_baseline = 0, mag_baseline = 0;
P4ImuDiagnostics barrier{};
void imu_finish(const char *error)
{
    if (!sample.imu_received) sample.imu_error = error;
    if (!sample.grv_received) sample.grv_error = error;
    if (!sample.mag_received) sample.mag_error = error;
    phase = Phase::Ready;
}
}
void capture_begin()
{
    laser_begin();
    const bool ready = p4_imu_begin(MM1_IMU_ADDR);
    Serial.printf("[LAB] IMU=%s RV/GRV/MAG=50Hz_nominal acceleration=OFF magnetic_offset=0 laser_trim=0\n",
                  ready ? "READY" : "NOT_READY");
    const auto init = p4_imu_diagnostics();
    Serial.printf("[LAB] IMU init=%s rc=%d addr=0x%02X SDA=%u SCL=%u\n",
                  init.init_stage, init.init_rc, init.address, init.sda, init.scl);
}
bool capture_busy() { return phase != Phase::Idle; }
bool capture_ready() { return phase == Phase::Ready; }
const Sample &capture_sample() { return sample; }
const char *capture_state() { return capture_busy() ? "BUSY" : "IDLE"; }
void capture_cancel() { laser_cancel(); phase = Phase::Idle; }
bool capture_start(uint32_t id)
{
    if (capture_busy() || calibration_blocks_capture()) return false;
    sample = {}; sample.request_id = id;
    sample.imu_epoch = p4_imu_diagnostics().epoch;
    started = millis(); phase = Phase::Laser;
    laser_start();
    return true;
}
void capture_tick()
{
    laser_tick();
    p4_imu_poll(); // Always service fusion, including idle and laser wait.
    const uint32_t now = millis();
    const auto current = p4_imu_diagnostics();
    if (phase != Phase::Idle && phase != Phase::Ready && current.epoch != sample.imu_epoch) {
        sample.imu_reset = true;
        if (phase == Phase::Laser) {
            if (laser_done()) sample.laser = laser_result();
            else sample.laser = {false, 0, "LASER_CANCELLED"};
            laser_cancel();
        }
        imu_finish("IMU_RESET"); return;
    }
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
    if (diag.resets != barrier.resets) { sample.imu_reset = true; imu_finish("IMU_RESET"); return; }
    if (now-imu_started >= LAB_IMU_TIMEOUT_MS) {
        if (phase == Phase::DrainImu) imu_finish("IMU_BACKLOG");
        else {
            if (!sample.imu_received) sample.imu_error = "IMU_TIMEOUT";
            if (!sample.grv_received) sample.grv_error = "GRV_TIMEOUT";
            if (!sample.mag_received) sample.mag_error = "MAG_TIMEOUT";
            phase = Phase::Ready;
        }
        return;
    }
    if (phase == Phase::DrainImu) {
        if (diag.io_errors != barrier.io_errors) { imu_finish("IMU_IO_ERROR"); return; }
        if (diag.decode_errors != barrier.decode_errors) { imu_finish("IMU_DECODE_ERROR"); return; }
        // A successful empty HAL read proves the pending FIFO was drained.
        if (diag.empty_reads != barrier.empty_reads) {
            baseline = diag.generation;
            game_baseline = diag.game_generation;
            mag_baseline = diag.mag_generation;
            phase = Phase::WaitImu;
        }
        return;
    }
    P4ImuSample fresh{};
    if (!sample.imu_received && p4_imu_snapshot(&fresh) && fresh.generation != baseline) {
        sample.imu = fresh;
        sample.imu_received = true;
        sample.angles = orientation(fresh.w, fresh.x, fresh.y, fresh.z,
                                    IMU_LASER_AXIS_BX, IMU_LASER_AXIS_BY, IMU_LASER_AXIS_BZ);
        if (!sample.angles.quaternion_valid) sample.imu_error = "IMU_BAD_QUAT";
        else if (!std::isfinite(fresh.accuracy_rad) || fresh.accuracy_rad < 0)
            sample.imu_error = "IMU_INVALID_ACCURACY";
        else { sample.imu_valid = true; sample.imu_error = "NONE"; }
    }
    P4ImuGameSample game{};
    if (!sample.grv_received && p4_imu_game_snapshot(&game) && game.generation != game_baseline) {
        sample.grv = game;
        sample.grv_received = true;
        sample.grv_valid = orientation(game.w, game.x, game.y, game.z, 1, 0, 0).quaternion_valid;
        sample.grv_error = sample.grv_valid ? "NONE" : "GRV_BAD_QUAT";
    }
    P4ImuMagSample mag{};
    if (!sample.mag_received && p4_imu_mag_snapshot(&mag) && mag.generation != mag_baseline) {
        sample.mag = mag;
        sample.mag_received = true;
        sample.mag_valid = std::isfinite(mag.x) && std::isfinite(mag.y) && std::isfinite(mag.z);
        sample.mag_error = sample.mag_valid ? "NONE" : "MAG_NONFINITE";
    }
    if (sample.imu_received && sample.grv_received && sample.mag_received) phase = Phase::Ready;
    // A mixed cargo can deliver valid reports before another report fails decoding.
    // Preserve those fresh reports and attribute the transport error only to missing ones.
    else if (diag.io_errors != barrier.io_errors) imu_finish("IMU_IO_ERROR");
    else if (diag.decode_errors != barrier.decode_errors) imu_finish("IMU_DECODE_ERROR");
}
}
