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

namespace lab {
float magnetic_azimuth(const float q[4], const float m[3], float min_horizontal)
{
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    if (!orientation(q[0], q[1], q[2], q[3], 1, 0, 0).quaternion_valid) return invalid;
    for (unsigned i = 0; i < 3; ++i) if (!std::isfinite(m[i])) return invalid;
    const float w=q[0], x=q[1], y=q[2], z=q[3];
    float u[] = {2*(x*z-w*y), 2*(y*z+w*x), 1-2*(x*x+y*y)};
    const float un = std::sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);
    if (un < 1e-5f) return invalid;
    for (float &v : u) v /= un;
    float f[] = {1-u[0]*u[0], -u[0]*u[1], -u[0]*u[2]};
    const float fn = std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if (fn < 1e-5f) return invalid;
    for (float &v : f) v /= fn;
    const float l[] = {u[1]*f[2]-u[2]*f[1], u[2]*f[0]-u[0]*f[2], u[0]*f[1]-u[1]*f[0]};
    float nf=0, nl=0;
    for (unsigned i=0; i<3; ++i) { nf += m[i]*f[i]; nl += m[i]*l[i]; }
    if (std::hypot(nf,nl) < min_horizontal) return invalid;
    // With +Y to the left, north on the left means a clockwise heading
    // of +90 degrees (front points east in ENU). Match orientation(+X).
    return std::fmod(std::atan2(nl,nf)*57.29577951308232f+360.f,360.f);
}
}
