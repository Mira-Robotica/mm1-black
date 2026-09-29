#pragma once
#include <cstddef>
#include <cstdint>
#include "laser_poll.h"
#include "orientation.h"
#include "board/p4/p4_imu.h"

namespace lab {
struct Sample {
    uint32_t request_id = 0;
    LaserResult laser;
    P4ImuSample imu{};
    Orientation angles;
    bool imu_received = false;
    bool imu_valid = false;
    const char *imu_error = "IMU_NOT_READY";
};
void capture_begin();
void capture_tick();
bool capture_start(uint32_t id);
void capture_cancel();
bool capture_busy();
bool capture_ready();
const Sample &capture_sample();
const char *capture_state();
// Pure formatting/parser helpers allow host tests without Arduino.
size_t format_sample(char *out, size_t size, const Sample &sample);
bool parse_capture(const char *line, uint32_t &id, uint32_t &count);
}
