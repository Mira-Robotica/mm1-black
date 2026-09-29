#include "orientation.h"
#include <cmath>

namespace lab {
Orientation orientation(float w, float x, float y, float z, float bx, float by, float bz)
{
    Orientation out;
    const float norm = std::sqrt(w*w + x*x + y*y + z*z);
    if (!std::isfinite(norm) || std::fabs(norm - 1.f) > 0.02f) return out;
    out.quaternion_valid = true;
    const float axis = std::sqrt(bx*bx + by*by + bz*bz);
    out.error = "ANGLE_AXIS_INVALID";
    if (!std::isfinite(axis) || axis < 1e-6f) return out;
    bx /= axis; by /= axis; bz /= axis;
    const float wx = (1-2*(y*y+z*z))*bx + 2*(x*y-w*z)*by + 2*(x*z+w*y)*bz;
    const float wy = 2*(x*y+w*z)*bx + (1-2*(x*x+z*z))*by + 2*(y*z-w*x)*bz;
    const float wz = 2*(x*z-w*y)*bx + 2*(y*z+w*x)*by + (1-2*(x*x+y*y))*bz;
    const float roll_y = 2*(w*x+y*z), roll_x = 1-2*(x*x+y*y);
    const float horizontal = std::sqrt(wx*wx+wy*wy);
    constexpr float r2d = 57.29577951308232f;
    const bool azimuth_defined = horizontal >= 1e-5f;
    const bool roll_defined = std::hypot(roll_y, roll_x) >= 1e-5f;
    if (azimuth_defined)
        out.azimuth = std::fmod(std::atan2(wx, wy)*r2d + 360.f, 360.f);
    out.inclination = std::atan2(wz, horizontal)*r2d;
    if (roll_defined) out.roll = std::atan2(roll_y, roll_x)*r2d;
    out.angles_valid = azimuth_defined && roll_defined;
    out.error = out.angles_valid ? "NONE" : "ANGLE_SINGULARITY";
    return out;
}
}
