#include "stream_service.h"
#include "capture_service.h"
#include "calibration_service.h"
#include "orientation.h"
#include "firmware_version.h"
#include "board/p4/SparkFun_BNO08x_Arduino_Library-1.0.6/src/sh2_err.h"
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <cstdarg>

namespace lab {
const char STREAM_HEADER[] = "session_id,seq,device_ms,updated_mask,valid_mask,value_updated_mask,raw_age_ms,uncal_age_ms,cal_age_ms,rv_age_ms,grv_age_ms,mag_raw_x_adc,mag_raw_y_adc,mag_raw_z_adc,mag_uncal_x_uT,mag_uncal_y_uT,mag_uncal_z_uT,mag_cal_x_uT,mag_cal_y_uT,mag_cal_z_uT,rv_azimuth_deg,rv_inclination_deg,rv_roll_deg,grv_azimuth_deg,grv_inclination_deg,grv_roll_deg,mag_cal_azimuth_deg,rv_status,grv_status,rv_accuracy_rad";
namespace {
enum class Phase { Idle, ReadCal, ReadFeatures, Configuring, Streaming, Restoring, Failed };
Phase phase = Phase::Idle;
uint32_t sid=0, owner=0, epoch=0, heartbeat=0, deadline=0, next_publish=0, period=500, seq=0;
uint32_t baseline=0, feature_query_ms=0;
unsigned index=0, step=0, preamble=0;
bool waiting=false, cal_wait=false, changed=false, snapshot=false, restore_error=false;
bool reset_during_session=false, attached=false;
uint8_t cal_mask=0;
float rate=2;
const char *failure=nullptr;
const char *feature_operation="NONE", *initial_operation="NONE", *restore_operation="NONE";
int feature_rc=0, initial_rc=0, restore_rc=0;
unsigned initial_sensor=0, restore_sensor=0;
void record_feature_failure(const char *operation, int rc)
{
    if (phase==Phase::Restoring) {
        if (!restore_sensor) { restore_sensor=P4_STREAM_IDS[index]; restore_operation=operation; restore_rc=rc; }
    } else if (!initial_sensor) {
        initial_sensor=P4_STREAM_IDS[index]; initial_operation=operation; initial_rc=rc;
    }
}
char notice[256]{};
sh2_SensorConfig_t original[5]{}, requested[5]{}, confirmed[5]{};
P4ImuStreamSample cached[5]{};
float values[19]{};
uint32_t valid_mask=0, value_updated=0, updated=0;
uint32_t queued_updated=0, queued_values=0;
constexpr uint32_t OP_MS=2000, HEARTBEAT_MS=15000;

bool equal(const sh2_SensorConfig_t &a, const sh2_SensorConfig_t &b)
{
    return a.changeSensitivityEnabled==b.changeSensitivityEnabled &&
        a.changeSensitivityRelative==b.changeSensitivityRelative && a.wakeupEnabled==b.wakeupEnabled &&
        a.alwaysOnEnabled==b.alwaysOnEnabled && a.changeSensitivity==b.changeSensitivity &&
        a.reportInterval_us==b.reportInterval_us && a.batchInterval_us==b.batchInterval_us && a.sensorSpecific==b.sensorSpecific;
}
void finish()
{
    phase=restore_error ? Phase::Failed : Phase::Idle;
    if (attached) snprintf(notice,sizeof(notice),"# %s STREAM_IMU %s session_id=%lu restored=%u code=%s cause=%s initial_sensor=%u initial_op=%s initial_rc=%d restore_sensor=%u restore_op=%s restore_rc=%d\n",
        failure || restore_error ? "ERR" : "OK", "STOP", (unsigned long)sid,
        unsigned(!restore_error), restore_error ? "RESTORE_FAILED" : failure ? failure : "NONE",
        failure ? failure : "NONE", initial_sensor, initial_operation, initial_rc,
        restore_sensor, restore_operation, restore_rc);
}
void restore(const char *why)
{
    if (why && !failure) failure=why;
    if (phase==Phase::Restoring) return;
    preamble=0; waiting=false; index=step=0; restore_error=false;
    restore_sensor=0; restore_operation="NONE"; restore_rc=0;
    // No writes have occurred while reading the original configuration.
    if (!changed) { if (cal_wait) sh2_lab_abort(); cal_wait=false; finish(); return; }
    phase=Phase::Restoring;
}
bool expired() { return uint32_t(millis()-deadline) >= OP_MS; }
// A request is send-only; generation changes exclusively on Get Feature Response.
int read_feature(bool write, const sh2_SensorConfig_t &config)
{
    if (!waiting) {
        baseline=p4_imu_feature(index).generation;
        deadline=millis(); waiting=true;
        if (write) {
            feature_operation="SET_FEATURE";
            feature_rc=p4_imu_set_feature(index,config);
            if (feature_rc!=SH2_OK) {
                record_feature_failure(feature_operation,feature_rc); waiting=false; return -1;
            }
        }
        feature_operation="GET_FEATURE";
        feature_query_ms=millis();
        feature_rc=sh2_lab_get_feature(P4_STREAM_IDS[index]);
        if (feature_rc!=SH2_OK) {
            record_feature_failure(feature_operation,feature_rc); waiting=false; return -1;
        }
        return 0;
    }
    const auto f=p4_imu_feature(index);
    if (f.known && f.generation!=baseline) {
        const bool matches = !write || (phase==Phase::Restoring
            ? equal(f.config,config) : f.config.reportInterval_us!=0);
        if (matches) { waiting=false; return 1; }
        // Get Feature and unsolicited Set Feature responses share the same
        // report ID. An earlier state may arrive after our write. Keep waiting
        // for the effective state, with a fixed deadline and read-only queries.
    }
    if (write && uint32_t(millis()-feature_query_ms)>=200 && !expired()) {
        feature_query_ms=millis();
        const int rc=sh2_lab_get_feature(P4_STREAM_IDS[index]);
        if (rc!=SH2_OK) {
            record_feature_failure("GET_FEATURE",rc); waiting=false; return -1;
        }
    }
    if (expired()) { record_feature_failure("CONFIRM_TIMEOUT",SH2_ERR_TIMEOUT); waiting=false; return -1; }
    return 0;
}
void set_value(unsigned i, float v)
{
    if (!std::isfinite(v)) return;
    values[i]=v; valid_mask |= 1UL<<i; value_updated |= 1UL<<i;
}
void samples()
{
    // Read all groups first, so mag can use this poll's newest GRV.
    P4ImuStreamSample incoming[5];
    for (unsigned g=0; g<5; ++g) incoming[g]=p4_imu_stream_sample(g);
    for (unsigned g=0; g<5; ++g) {
        const auto &s=incoming[g];
        if (!s.valid || s.epoch!=epoch || (cached[g].valid && s.generation==cached[g].generation)) continue;
        cached[g]=s; updated |= 1UL<<g;
        if (g<3) for (unsigned k=0; k<3; ++k) set_value(3*g+k,s.v[k]);
        else {
            const auto a=orientation(s.v[0],s.v[1],s.v[2],s.v[3],1,0,0);
            set_value(g==3 ? 16 : 17,s.status);
            if (g==3) set_value(18,s.accuracy_rad);
            const unsigned base=g==3 ? 9 : 12;
            set_value(base,a.azimuth); set_value(base+1,a.inclination); set_value(base+2,a.roll);
        }
        // Raw axes/scales are unverified: never fabricate a Raw heading.
        if (g==2) {
            const auto &q=incoming[4];
            const int32_t separation=int32_t(s.received_ms-q.received_ms);
            if (q.valid && q.epoch==epoch && separation>=-200 && separation<=200 &&
                uint32_t(millis()-q.received_ms)<=200 && uint32_t(millis()-s.received_ms)<=200)
                set_value(15,magnetic_azimuth(q.v,s.v,0.01f));
        }
    }
}
struct Writer {
    char *out; size_t size, used=0; bool ok=true;
    void add(const char *fmt, ...) {
        if (!ok) return;
        va_list args; va_start(args,fmt); int n=vsnprintf(out+used,size-used,fmt,args); va_end(args);
        if (n<0 || size_t(n)>=size-used) { ok=false; out[0]=0; } else used+=n;
    }
};
void config_text(Writer &w, const char *name, const sh2_SensorConfig_t configs[5])
{
    w.add(" %s=",name);
    for (unsigned i=0;i<5;++i) {
        const auto &c=configs[i];
        unsigned flags=c.changeSensitivityRelative | (c.changeSensitivityEnabled<<1) | (c.wakeupEnabled<<2) | (c.alwaysOnEnabled<<3);
        w.add("%s%lu/%lu/%u/%u/%lu",i ? ";" : "",(unsigned long)c.reportInterval_us,
              (unsigned long)c.batchInterval_us,flags,c.changeSensitivity,(unsigned long)c.sensorSpecific);
    }
}
}
uint32_t stream_session_id() { return sid; }
bool stream_busy() { return phase!=Phase::Idle; }
const char *stream_state()
{
    switch(phase) {
    case Phase::Idle:return "IDLE";
    case Phase::Streaming:return "STREAMING";
    case Phase::Restoring:return "RESTORING";
    case Phase::Failed:return "RESTORE_FAILED";
    default:return "CONFIGURING";
    }
}
void stream_disconnect(uint32_t connection)
{
    if (connection!=owner) return;
    attached=false; notice[0]=0;
    if (stream_busy() && phase!=Phase::Failed) restore("DISCONNECTED");
}
void stream_command(const char *line,uint32_t connection,char *out,size_t size)
{
    char action[16]{}, arg[64]{}, extra; unsigned n=0;
    const int count=strncmp(line,"STREAM_IMU ",11) ? 0 : sscanf(line,"STREAM_IMU %15s %63s %c",action,arg,&extra);
    const char *error="BAD_COMMAND";
    out[0]=0;
    if (count==2 && !strcmp(action,"START") && !strncmp(arg,"rate_hz=",8)) {
        char *end=nullptr; errno=0; const float hz=strtof(arg+8,&end);
        if (errno || end==arg+8 || *end || !std::isfinite(hz) || hz<0.2f || hz>5.f) error="RATE_RANGE";
        else if (stream_busy() || capture_busy() || calibration_blocks_capture() || p4_imu_reset_pending() || sh2_lab_busy()) error="BUSY";
        else if (!p4_imu_ok() || p4_imu_lab_info().autosave_rc!=SH2_OK) error="IMU_NOT_READY";
        else if (sid==UINT32_MAX) error="SESSION_ID_EXHAUSTED";
        else {
            ++sid; owner=connection; attached=true; rate=hz; period=uint32_t(1000.f/hz);
            epoch=p4_imu_diagnostics().epoch; heartbeat=millis(); phase=Phase::ReadCal;
            changed=snapshot=waiting=cal_wait=restore_error=reset_during_session=false;
            failure=nullptr; notice[0]=0;
            initial_sensor=restore_sensor=0; initial_rc=restore_rc=0;
            initial_operation=restore_operation="NONE"; index=step=preamble=seq=0;
            valid_mask=value_updated=updated=0;
            for (auto &s:cached) s={};
            snprintf(out,size,"# OK STREAM_IMU START session_id=%lu state=CONFIGURING\n",(unsigned long)sid); return;
        }
    } else if (count==2 && (!strcmp(action,"STOP") || !strcmp(action,"PING")) &&
               !strncmp(arg,"session_id=",11)) {
        char *end=nullptr; errno=0; const unsigned long id=strtoul(arg+11,&end,10);
        for (const char *p=arg+11; *p; ++p) if (*p>='0' && *p<='9') ++n;
        if (errno || !n || n!=strlen(arg+11) || *end || id!=sid || id==0 || id>UINT32_MAX) error="SESSION_MISMATCH";
        else if (owner!=connection && !(phase==Phase::Failed && !attached && !strcmp(action,"STOP"))) error="NOT_OWNER";
        else if (!strcmp(action,"PING")) {
            heartbeat=millis(); snprintf(out,size,"# OK STREAM_IMU PING session_id=%lu\n",(unsigned long)sid); return;
        } else if (phase==Phase::Idle) {
            notice[0]=0;
            snprintf(out,size,"# OK STREAM_IMU STOP session_id=%lu restored=1 code=NONE\n",(unsigned long)sid); return;
        } else {
            if (phase==Phase::Failed) { changed=snapshot; failure=nullptr; owner=connection; attached=true; }
            restore(nullptr); return; // Final ACK is emitted only after confirmed restoration.
        }
    }
    snprintf(out,size,"# ERR STREAM_IMU code=%s\n",error);
}
void stream_tick()
{
    if (phase==Phase::Idle || phase==Phase::Failed) return;
    if (p4_imu_diagnostics().epoch!=epoch) {
        epoch=p4_imu_diagnostics().epoch; reset_during_session=true;
        sh2_lab_abort(); cal_wait=false;
        // Restart restoration after the adapter's reset recovery, never the stream.
        if (phase==Phase::Restoring) phase=Phase::Configuring;
        restore("IMU_RESET");
    }
    if (phase==Phase::Idle) return;
    if (phase!=Phase::Restoring && uint32_t(millis()-heartbeat)>=HEARTBEAT_MS) restore("HEARTBEAT_TIMEOUT");
    if (phase==Phase::ReadCal) {
        if (!cal_wait) {
            if (sh2_lab_start(SH2_LAB_GET_CAL,0,OP_MS*1000)!=SH2_OK) { restore("CAL_READ_FAILED"); return; }
            cal_wait=true;
        }
        int rc; uint8_t mask;
        if (sh2_lab_result(&rc,&mask)) {
            cal_wait=false;
            if (rc!=SH2_OK) restore("CAL_READ_FAILED");
            else { cal_mask=mask; phase=Phase::ReadFeatures; index=0; }
        }
    } else if (phase==Phase::ReadFeatures) {
        int rc=read_feature(false,{});
        if (rc<0) restore("SNAPSHOT_FAILED");
        else if (rc>0) {
            original[index]=p4_imu_feature(index).config;
            if (++index==5) {
                snapshot=true; index=0; phase=Phase::Configuring;
                for (unsigned g=0;g<5;++g) {
                    requested[g]=original[g];
                    if (!requested[g].reportInterval_us) requested[g].reportInterval_us=20000;
                }
            }
        }
    } else if (phase==Phase::Configuring) {
        changed=true; // Even a transport failure could have partially reached the hub.
        const int rc=read_feature(true,requested[index]);
        if (rc<0) restore("CONFIG_FAILED");
        else if (rc>0) {
            confirmed[index]=p4_imu_feature(index).config;
            if (!confirmed[index].reportInterval_us) { restore("REPORT_DISABLED"); return; }
            if (++index==5) {
                p4_imu_lab_invalidate(); phase=Phase::Streaming; preamble=1; next_publish=millis();
            }
        }
    } else if (phase==Phase::Streaming) samples();
    else if (phase==Phase::Restoring) {
        if (index<5) {
            const int rc=read_feature(true,original[index]);
            if (rc) {
                if (rc<0 || !equal(p4_imu_feature(index).config,original[index])) {
                    restore_error=true;
                    if (rc>0) record_feature_failure("CONFIG_MISMATCH",SH2_ERR_HUB);
                }
                ++index;
            }
        } else {
            // Normally read-only. Only a spontaneous sensor reset can require
            // reinstating the saved calibration control; never save/erase DCD.
            if (!cal_wait) {
                const bool set=reset_during_session && step==0;
                if (sh2_lab_start(set ? SH2_LAB_SET_CAL : SH2_LAB_GET_CAL,cal_mask,OP_MS*1000)!=SH2_OK) {
                    restore_error=true; finish(); return;
                }
                cal_wait=true;
            }
            int rc; uint8_t mask;
            if (sh2_lab_result(&rc,&mask)) {
                cal_wait=false;
                if (rc!=SH2_OK || ((!reset_during_session || step>0) && mask!=cal_mask)) restore_error=true;
                if (reset_during_session && step++==0) return;
                p4_imu_lab_invalidate(); finish();
            }
        }
    }
}
bool stream_output(char *out,size_t size,bool replace_snapshot)
{
    if (notice[0]) { snprintf(out,size,"%s",notice); notice[0]=0; return true; }
    if (!attached || phase!=Phase::Streaming) return false;
    Writer w{out,size};
    if (preamble==1) {
        w.add("# OK STREAM_IMU READY session_id=%lu\n",(unsigned long)sid); ++preamble;
    } else if (preamble==2) {
        const auto i=p4_imu_lab_info();
        w.add("# STREAM_META schema=2 firmware=" FW_VERSION " session_id=%lu epoch=%lu rate_hz=%.9g part=%lu build=%lu fw=%u.%u.%u cal_mask=%u cal_source=GET_CAL dcd_autosave=disabled_sent_unacknowledged reports=22,15,3,5,8 config_order=raw,uncal,cal,rv,grv config_fields=interval_us/batch_us/flags/change/specific",
            (unsigned long)sid,(unsigned long)epoch,(double)rate,(unsigned long)i.part,(unsigned long)i.build,i.major,i.minor,i.patch,cal_mask);
        config_text(w,"previous",original); config_text(w,"requested",requested); config_text(w,"confirmed",confirmed);
        w.add(" quaternion=wxyz front=+X rv_world=ENU rv_reference=magnetic grv_reference=arbitrary inclination=beam_production_pitch angular_correction=none raw_mapping=unknown raw_azimuth=unavailable tilt_source=GRV mag_azimuth_method=atan2_left_forward_v2 max_pair_ms=200 norm_tolerance=0.02 singularity_epsilon=1e-5 cal_horizontal_min_uT=0.01 raw_horizontal_min_adc=unknown heartbeat_ms=15000\n");
        ++preamble;
    } else if (preamble==3) { w.add("%s\n",STREAM_HEADER); preamble=0; }
    else {
        if (int32_t(millis()-next_publish)<0) return false;
        next_publish=millis()+period;
        if (replace_snapshot) { updated |= queued_updated; value_updated |= queued_values; }
        queued_updated=updated; queued_values=value_updated;
        w.add("%lu,%lu,%lu,%lu,%lu,%lu",(unsigned long)sid,(unsigned long)++seq,(unsigned long)millis(),
              (unsigned long)updated,(unsigned long)valid_mask,(unsigned long)value_updated);
        for (const auto &s:cached) {
            w.add(","); if (s.valid) w.add("%lu",(unsigned long)(millis()-s.received_ms));
        }
        for (unsigned i=0;i<19;++i) { w.add(","); if (valid_mask & (1UL<<i)) w.add("%.9g",(double)values[i]); }
        w.add("\n"); updated=value_updated=0;
    }
    if (!w.ok) { restore("FORMAT_FAILED"); return false; }
    return true;
}
}
