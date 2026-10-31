#include "capture_service.h"
#include "settings.h"
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
    char grv[200] = ",,,,,", mag[160] = ",,,,";
    if (s.grv_received) {
        char w[32] = "", x[32] = "", y[32] = "", z[32] = "";
        number(w, sizeof(w), s.grv.w); number(x, sizeof(x), s.grv.x);
        number(y, sizeof(y), s.grv.y); number(z, sizeof(z), s.grv.z);
        std::snprintf(grv, sizeof(grv), "%s,%s,%s,%s,%u,%u", w, x, y, z,
                      (unsigned)s.grv.status, (unsigned)s.grv.sequence);
    }
    if (s.mag_received) {
        char x[32] = "", y[32] = "", z[32] = "";
        number(x, sizeof(x), s.mag.x); number(y, sizeof(y), s.mag.y); number(z, sizeof(z), s.mag.z);
        std::snprintf(mag, sizeof(mag), "%s,%s,%s,%u,%u", x, y, z,
                      (unsigned)s.mag.status, (unsigned)s.mag.sequence);
    }
    const bool all = s.laser.valid && s.imu_valid && s.grv_valid && s.mag_valid;
    const bool any = s.laser.valid || s.imu_valid || s.grv_valid || s.mag_valid;
    const char *result = all ? "OK" : any ? "PARTIAL" : "ERROR";
    const int length = std::snprintf(out, size,
        "request_id,sample_index,distance_m,laser_valid,laser_error,qw,qx,qy,qz,azimuth_deg,inclination_deg,roll_deg,angles_valid,angle_error,accuracy_rad,imu_status,imu_seq,imu_valid,imu_error,"
        "grv_qw,grv_qx,grv_qy,grv_qz,grv_status,grv_seq,grv_valid,grv_error,"
        "mag_x_ut,mag_y_ut,mag_z_ut,mag_status,mag_seq,mag_valid,mag_error,imu_epoch\n"
        "%lu,1,%s,%u,%s,%s,%s,%u,%s,%s,%u,%s,%s,%u,%s,%s,%u,%s,%lu\n"
        "# DONE request_id=%lu completion=COMPLETE result=%s n_requested=1 n_rows=1 n_valid=%u n_laser_valid=%u n_imu_valid=%u n_grv_valid=%u n_mag_valid=%u imu_reset=%u code=NONE state=IDLE imu_retries=%u imu_retry_limit=%u imu_io_errors=%lu\n",
        (unsigned long)s.request_id, distance, (unsigned)s.laser.valid, s.laser.error, quat, angles,
        (unsigned)(s.imu_valid && s.angles.angles_valid), s.imu_valid ? s.angles.error : "IMU_INVALID",
        quality, (unsigned)s.imu_valid, s.imu_error,
        grv, (unsigned)s.grv_valid, s.grv_error, mag, (unsigned)s.mag_valid, s.mag_error,
        (unsigned long)s.imu_epoch, (unsigned long)s.request_id, result,
        (unsigned)all, (unsigned)s.laser.valid, (unsigned)s.imu_valid,
        (unsigned)s.grv_valid, (unsigned)s.mag_valid, (unsigned)s.imu_reset,
        s.imu_retries, (unsigned)LAB_IMU_MAX_RETRIES, (unsigned long)s.imu_io_errors);
    return length >= 0 && (size_t)length < size ? (size_t)length : 0;
}
}
