#include "capture_service.h"
#include "settings.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace lab {
bool parse_capture(const char *line, uint32_t &id, uint32_t &count, bool *use_laser)
{
    const bool imu_only = std::strncmp(line, "CAPTURE_IMU ", 12) == 0;
    if (!imu_only && std::strncmp(line, "CAPTURE ", 8)) return false;
    if (use_laser) *use_laser = !imu_only;
    const char *p = line + (imu_only ? 12 : 8);
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
    const bool all = (!s.laser_requested || s.laser.valid) && s.imu_valid && s.grv_valid && s.mag_valid && s.raw.valid && s.uncal.valid;
    const bool any = (s.laser_requested && s.laser.valid) || s.imu_valid || s.grv_valid || s.mag_valid || s.raw.valid || s.uncal.valid;
    const char *result = all ? "OK" : any ? "PARTIAL" : "ERROR";
    const int length = std::snprintf(out, size,
        "request_id,sample_index,distance_m,laser_valid,laser_error,qw,qx,qy,qz,azimuth_deg,inclination_deg,roll_deg,angles_valid,angle_error,accuracy_rad,imu_status,imu_seq,imu_valid,imu_error,"
        "grv_qw,grv_qx,grv_qy,grv_qz,grv_status,grv_seq,grv_valid,grv_error,"
        "mag_x_ut,mag_y_ut,mag_z_ut,mag_status,mag_seq,mag_valid,mag_error,imu_epoch,"
        "mag_raw_x_adc,mag_raw_y_adc,mag_raw_z_adc,raw_valid,raw_error,mag_uncal_x_uT,mag_uncal_y_uT,mag_uncal_z_uT,uncal_valid,uncal_error,"
        "grv_azimuth_deg,grv_inclination_deg,grv_roll_deg,mag_cal_azimuth_deg,raw_age_ms,uncal_age_ms,cal_age_ms,rv_age_ms,grv_age_ms\n"
        "%lu,1,%s,%u,%s,%s,%s,%u,%s,%s,%u,%s,%s,%u,%s,%s,%u,%s,%lu",
        (unsigned long)s.request_id, distance, (unsigned)s.laser.valid, s.laser.error, quat, angles,
        (unsigned)(s.imu_valid && s.angles.angles_valid), s.imu_valid ? s.angles.error : "IMU_INVALID",
        quality, (unsigned)s.imu_valid, s.imu_error,
        grv, (unsigned)s.grv_valid, s.grv_error, mag, (unsigned)s.mag_valid, s.mag_error,
        (unsigned long)s.imu_epoch);
    if (length < 0 || size_t(length) >= size) return 0;
    size_t used = length;
    auto append = [&](const char *format, auto... values) {
        if (used >= size) return;
        const int n = std::snprintf(out + used, size - used, format, values...);
        used = n < 0 || size_t(n) >= size - used ? size : used + size_t(n);
    };
    auto component = [&](float value) {
        if (std::isfinite(value)) append(",%.9g", double(value));
        else append("%c", ',');
    };
    auto field = [&](const P4ImuStreamSample &report, const char *error) {
        for (unsigned i = 0; i < 3; ++i) component(report.valid ? report.v[i] : NAN);
        append(",%u,%s", unsigned(report.valid), error);
    };
    field(s.raw, s.raw_error);
    field(s.uncal, s.uncal_error);
    component(s.grv_angles.azimuth);
    component(s.grv_angles.inclination);
    component(s.grv_angles.roll);
    component(s.mag_cal_azimuth);
    auto age = [&](bool received, uint32_t timestamp) {
        if (received) append(",%lu", (unsigned long)(s.finished_ms - timestamp));
        else append("%c", ',');
    };
    age(s.raw.valid, s.raw.received_ms);
    age(s.uncal.valid, s.uncal.received_ms);
    age(s.mag_received, s.mag.received_ms);
    age(s.imu_received, s.imu.received_ms);
    age(s.grv_received, s.grv.received_ms);
    append("%c", '\n');
    append(
        "# DONE request_id=%lu completion=COMPLETE result=%s n_requested=1 n_rows=1 n_valid=%u n_laser_valid=%u n_imu_valid=%u n_grv_valid=%u n_mag_valid=%u imu_reset=%u code=NONE state=IDLE imu_retries=%u imu_retry_limit=%u imu_io_errors=%lu laser_requested=%u n_raw_valid=%u n_uncal_valid=%u\n",
        (unsigned long)s.request_id, result,
        (unsigned)all, (unsigned)s.laser.valid, (unsigned)s.imu_valid,
        (unsigned)s.grv_valid, (unsigned)s.mag_valid, (unsigned)s.imu_reset,
        s.imu_retries, (unsigned)LAB_IMU_MAX_RETRIES, (unsigned long)s.imu_io_errors,
        (unsigned)s.laser_requested, unsigned(s.raw.valid), unsigned(s.uncal.valid));
    return used < size ? used : 0;
}
}
