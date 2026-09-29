#include "capture_service.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace lab {
bool parse_capture(const char *line, uint32_t &id, uint32_t &count)
{
    if (std::strncmp(line, "CAPTURE ", 8)) return false;
    const char *p = line+8;
    auto number = [&p](uint32_t &value) {
        while (*p == ' ') ++p;
        value = 0;
        if (*p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') {
            unsigned digit = *p++ - '0';
            if (value > (UINT32_MAX-digit)/10) return false;
            value = value*10+digit;
        }
        return true;
    };
    if (!number(id) || !id) return false;
    count = 1;
    if (*p) {
        if (*p != ' ') return false;
        while (*p == ' ') ++p;
        if (*p && !number(count)) return false;
    }
    while (*p == ' ') ++p;
    return !*p;
}
size_t format_sample(char *out, size_t size, const Sample &s)
{
    auto number = [](char *out, size_t size, float value) {
        if (std::isfinite(value)) std::snprintf(out, size, "%.9g", (double)value);
    };
    char distance[32] = "", quat[160] = ",,,", angles[100] = ",,", quality[80] = ",,";
    if (s.laser.valid) std::snprintf(distance, sizeof(distance), "%.9g", (double)s.laser.distance);
    if (s.imu_received) {
        char w[32] = "", x[32] = "", y[32] = "", z[32] = "", accuracy[32] = "";
        number(w, sizeof(w), s.imu.w); number(x, sizeof(x), s.imu.x);
        number(y, sizeof(y), s.imu.y); number(z, sizeof(z), s.imu.z);
        number(accuracy, sizeof(accuracy), s.imu.accuracy_rad);
        std::snprintf(quat, sizeof(quat), "%s,%s,%s,%s", w, x, y, z);
        std::snprintf(quality, sizeof(quality), "%s,%u,%u", accuracy,
                      (unsigned)s.imu.status, (unsigned)s.imu.sequence);
    }
    if (s.imu_valid) {
        char az[32] = "", inc[32] = "", roll[32] = "";
        number(az, sizeof(az), s.angles.azimuth);
        number(inc, sizeof(inc), s.angles.inclination);
        number(roll, sizeof(roll), s.angles.roll);
        std::snprintf(angles, sizeof(angles), "%s,%s,%s", az, inc, roll);
    }
    const char *result = s.laser.valid && s.imu_valid ? "OK" :
                         s.laser.valid || s.imu_valid ? "PARTIAL" : "ERROR";
    const int length = std::snprintf(out, size,
        "request_id,sample_index,distance_m,laser_valid,laser_error,qw,qx,qy,qz,azimuth_deg,inclination_deg,roll_deg,angles_valid,angle_error,accuracy_rad,imu_status,imu_seq,imu_valid,imu_error\n"
        "%lu,1,%s,%u,%s,%s,%s,%u,%s,%s,%u,%s\n"
        "# DONE request_id=%lu completion=COMPLETE result=%s n_requested=1 n_rows=1 n_valid=%u n_laser_valid=%u n_imu_valid=%u code=NONE state=IDLE\n",
        (unsigned long)s.request_id, distance, (unsigned)s.laser.valid, s.laser.error, quat, angles,
        (unsigned)(s.imu_valid && s.angles.angles_valid), s.imu_valid ? s.angles.error : "IMU_INVALID",
        quality, (unsigned)s.imu_valid, s.imu_error, (unsigned long)s.request_id, result,
        (unsigned)(s.laser.valid && s.imu_valid), (unsigned)s.laser.valid, (unsigned)s.imu_valid);
    return length >= 0 && (size_t)length < size ? (size_t)length : 0;
}
}
