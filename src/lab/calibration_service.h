#pragma once
#include <stddef.h>

namespace lab {
enum class CalOwner { Tcp, Serial };
void calibration_tick(); // After p4_imu_poll; all SH-2 access stays on loop().
bool calibration_busy();
bool calibration_blocks_capture();
const char *calibration_state();
void calibration_disconnect(CalOwner owner);
void calibration_command(const char *line, CalOwner owner, char *out, size_t size);
} // namespace lab
