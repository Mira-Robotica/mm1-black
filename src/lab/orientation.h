#pragma once
#include <limits>

namespace lab {
struct Orientation {
    bool quaternion_valid = false;
    bool angles_valid = false;
    float azimuth = std::numeric_limits<float>::quiet_NaN();
    float inclination = std::numeric_limits<float>::quiet_NaN();
    float roll = std::numeric_limits<float>::quiet_NaN();
    const char *error = "IMU_BAD_QUAT";
};
// Raw quaternion is never normalized or changed. Unit norm tolerance: 0.02.
Orientation orientation(float w, float x, float y, float z, float bx, float by, float bz);
}
