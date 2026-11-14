// Real cooperative streaming service, simulated SH-2 and clock.
#include <Arduino.h>
#include "lab/stream_service.h"
#include "lab/orientation.h"
#include "board/p4/p4_imu.h"
#include "board/p4/SparkFun_BNO08x_Arduino_Library-1.0.6/src/sh2_err.h"
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>

P4ImuFeature features[5]{};
sh2_SensorConfig_t hardware[5]{};
P4ImuStreamSample samples[5]{};
P4ImuLabInfo info{};
P4ImuDiagnostics diag{};
bool capture=false, calibration=false, reset_pending=false, replies=true, pending=false;
int pending_feature=-1, bad_write=-1;
unsigned writes=0, cal_sets=0, saves=0;
uint8_t mask=0x15;
namespace lab { bool capture_busy() { return capture; } bool calibration_blocks_capture() { return calibration; } }
bool p4_imu_ok() { return diag.ready; }
bool p4_imu_reset_pending() { return reset_pending; }
P4ImuLabInfo p4_imu_lab_info() { return info; }
P4ImuDiagnostics p4_imu_diagnostics() { return diag; }
P4ImuFeature p4_imu_feature(unsigned g) { return features[g]; }
P4ImuStreamSample p4_imu_stream_sample(unsigned g) { return samples[g]; }
void p4_imu_lab_invalidate() { for (auto &s:samples) s.valid=false; }
int p4_imu_set_feature(unsigned g,const sh2_SensorConfig_t &c) {
    ++writes; hardware[g]=c;
    if (int(g)==bad_write) return SH2_ERR_IO; // could have applied despite a failed write
    return SH2_OK;
}
int sh2_lab_get_feature(uint8_t id) {
    for (unsigned g=0;g<5;++g) if (P4_STREAM_IDS[g]==id) pending_feature=g;
    return SH2_OK;
}
bool sh2_lab_busy() { return pending; }
int sh2_lab_start(sh2_LabOp op,uint8_t m,uint32_t) {
    assert(!pending); pending=true;
    if (op==SH2_LAB_SET_CAL) { mask=m; ++cal_sets; }
    if (op==SH2_LAB_SAVE_DCD) ++saves;
    return SH2_OK;
}
bool sh2_lab_result(int *rc,uint8_t *m) {
    if (!pending) return false;
    *rc=SH2_OK; *m=mask; pending=false; return true;
}
void sh2_lab_abort() { pending=false; }
std::string cmd(const std::string &line,unsigned owner=1) {
    char out[2048];lab::stream_command(line.c_str(),owner,out,sizeof(out));return out;
}
std::string output(bool replace=false) {
    char out[2048];return lab::stream_output(out,sizeof(out),replace) ? out : "";
}
void tick(unsigned n=1,unsigned ms=5) {
    for (unsigned t=0;t<n;++t) {
        fake_ms+=ms;
        if (pending_feature>=0 && replies) {
            auto &f=features[pending_feature]; f.config=hardware[pending_feature]; f.known=true; ++f.generation;
            pending_feature=-1;
        }
        lab::stream_tick();
    }
}
std::vector<std::string> cells(const std::string &s) {
    std::vector<std::string> result; std::stringstream in(s);std::string c;
    while (std::getline(in,c,',')) result.push_back(c=="\n" ? "" : c);
    return result;
}
unsigned start(float hz=2) {
    auto s=cmd("STREAM_IMU START rate_hz="+std::to_string(hz));
    assert(s.find("state=CONFIGURING")!=std::string::npos);
    unsigned id=std::stoul(s.substr(s.find("session_id=")+11));
    tick(25);assert(std::string(lab::stream_state())=="STREAMING");
    assert(output().find("READY")!=std::string::npos);
    const auto meta=output();
    if (const char *path=std::getenv("MM1_STREAM_META_FIXTURE")) { std::ofstream out(path); out<<meta; }
    assert(meta.find("raw_mapping=unknown")!=std::string::npos);
    assert(meta.size()<2048 && meta.find("previous=0/1234/15/7/42")!=std::string::npos);
    assert(output()==std::string(lab::STREAM_HEADER)+"\n");
    return id;
}
void stop(unsigned id) {
    assert(cmd("STREAM_IMU STOP session_id="+std::to_string(id)).empty());
    assert(std::string(lab::stream_state())=="RESTORING");tick(15);
    assert(!lab::stream_busy());assert(output().find("restored=1")!=std::string::npos);
    assert(cmd("STREAM_IMU STOP session_id="+std::to_string(id)).find("restored=1")!=std::string::npos);
    assert(hardware[0].reportInterval_us==0 && hardware[0].batchInterval_us==1234 && hardware[0].sensorSpecific==42);
    assert(hardware[3].reportInterval_us==37000 && hardware[4].reportInterval_us==41000);
    assert(!saves);
}
void sample(unsigned g,const std::vector<float> &v) {
    auto &s=samples[g];s.valid=true;s.epoch=diag.epoch;s.received_ms=fake_ms;++s.generation;
    for (unsigned i=0;i<v.size();++i) s.v[i]=v[i];
}
void near(float a,float b) { assert(std::fabs(a-b)<0.002f); }
int main() {
    // Geometry: production beam inclination, no angular offset or normalization.
    const float pi=3.14159265358979323846f;
    auto a=lab::orientation(1,0,0,0,1,0,0);near(a.azimuth,90);near(a.inclination,0);near(a.roll,0);
    a=lab::orientation(std::sqrt(0.5f),0,0,std::sqrt(0.5f),1,0,0);near(a.azimuth,0);
    for (int sign:{-1,1}) {
        a=lab::orientation(std::cos(pi/12),0,sign*std::sin(pi/12),0,1,0,0);
        near(a.inclination,-sign*30.f);
    }
    assert(!lab::orientation(1.03,0,0,0,1,0,0).quaternion_valid);
    a=lab::orientation(std::sqrt(0.5f),0,std::sqrt(0.5f),0,1,0,0);
    assert(std::isnan(a.azimuth) && std::isnan(a.roll));near(a.inclination,-90);
    float q[]={1,0,0,0};
    const float vectors[][3]={{1,0,0},{0,1,0},{-1,0,0},{0,-1,0}};
    for (unsigned i=0;i<4;++i) near(lab::magnetic_azimuth(q,vectors[i],0.01f),90.f*i);
    // Pure yaw in GRV cannot change the body-frame up used for compensation.
    float yaw[]={std::cos(pi/3),0,0,std::sin(pi/3)};
    for (const auto &v:vectors) near(lab::magnetic_azimuth(q,v,0.01f),lab::magnetic_azimuth(yaw,v,0.01f));
    // Pitch 30deg with north along horizontal forward: tilted body field +X/+Z.
    float tilted[]={std::cos(pi/12),0,std::sin(pi/12),0}, field[]={30*std::cos(pi/6),0,30*std::sin(pi/6)};
    near(lab::magnetic_azimuth(tilted,field,0.01f),0);
    float zero[]={0,0,50};assert(std::isnan(lab::magnetic_azimuth(q,zero,0.01f)));
    float vertical[]={std::sqrt(0.5f),0,std::sqrt(0.5f),0};
    assert(std::isnan(lab::magnetic_azimuth(vertical,field,0.01f)));
    // Independent ENU experiment: transform a world magnetic vector pointing
    // north into the body frame. The recovered heading must match the front
    // vector heading for every yaw, pitch and roll. This catches sign errors
    // that quadrant examples copied from a specification cannot detect.
    for (float yaw_deg : {0.f, 35.f, 90.f, 180.f, 270.f, 359.f}) {
        for (float pitch_deg : {-55.f, 0.f, 40.f}) {
            for (float roll_deg : {-65.f, 0.f, 70.f}) {
                const float cy=std::cos(yaw_deg*pi/360), sy=std::sin(yaw_deg*pi/360);
                const float cp=std::cos(pitch_deg*pi/360), sp=std::sin(pitch_deg*pi/360);
                const float cr=std::cos(roll_deg*pi/360), sr=std::sin(roll_deg*pi/360);
                const float rotation[]={cr*cp*cy+sr*sp*sy, sr*cp*cy-cr*sp*sy,
                                        cr*sp*cy+sr*cp*sy, cr*cp*sy-sr*sp*cy};
                const float w=rotation[0], x=rotation[1], y=rotation[2], z=rotation[3];
                const float north_body[]={2*(x*y+w*z),1-2*(x*x+z*z),2*(y*z-w*x)};
                const float up_body[]={2*(x*z-w*y),2*(y*z+w*x),1-2*(x*x+y*y)};
                float magnetic[3];
                for (unsigned k=0;k<3;++k) magnetic[k]=30*north_body[k]-20*up_body[k];
                const auto expected=lab::orientation(w,x,y,z,1,0,0);
                const float observed=lab::magnetic_azimuth(rotation,magnetic,0.01f);
                near(std::remainder(observed-expected.azimuth,360.f),0);
            }
        }
    }
    // Header is shared with the Python test fixture, independent of its parser.
    std::ifstream fixture("docker/tests/fixtures/stream_header.csv");std::string header;std::getline(fixture,header);
    assert(header==lab::STREAM_HEADER);
    diag.ready=true;diag.epoch=4;info.autosave_rc=0;
    hardware[0].batchInterval_us=1234;hardware[0].sensorSpecific=42;hardware[0].changeSensitivity=7;
    hardware[0].changeSensitivityEnabled=hardware[0].changeSensitivityRelative=true;
    hardware[0].wakeupEnabled=hardware[0].alwaysOnEnabled=true;
    hardware[2].reportInterval_us=23000;hardware[3].reportInterval_us=37000;hardware[4].reportInterval_us=41000;
    for (const char *s:{"nan","inf","0","5.1","2 junk","2junk"})
        assert(cmd(std::string("STREAM_IMU START rate_hz=")+s).find("ERR")!=std::string::npos);
    for (bool *busy:{&capture,&calibration,&reset_pending}) {
        *busy=true;assert(cmd("STREAM_IMU START rate_hz=2").find("BUSY")!=std::string::npos);*busy=false;
    }
    // No response to snapshot reads means refusal before ANY mutation.
    replies=false;const auto before=writes;
    cmd("STREAM_IMU START rate_hz=2");tick(45,50);
    assert(!lab::stream_busy() && writes==before);assert(output().find("SNAPSHOT_FAILED")!=std::string::npos);
    replies=true;unsigned id=start();
    assert(cmd("STREAM_IMU START rate_hz=2").find("BUSY")!=std::string::npos);
    assert(cmd("STREAM_IMU STOP session_id="+std::to_string(id),2).find("NOT_OWNER")!=std::string::npos);
    sample(0,{1,2,3});sample(1,{30,0,2});sample(2,{20,1,2});sample(3,{1,0,0,0});sample(4,{1,0,0,0});tick();
    auto row=cells(output());assert(row.size()==30);
    assert(row[3]=="31" && row[4]=="524287" && row[5]=="524287"); // all except Raw azimuth
    assert(row[20]=="90" && row[21]=="0" && row[27]=="0" && row[28]=="0");
    near(std::stof(row[26]),2.862405f);
    // Replace an entirely unsent snapshot: preserve all novelty bits but use
    // the newest Raw measurement; no growing queue of old publications.
    sample(0,{7,8,9});tick(100);row=cells(output(true));
    assert(row[3]=="31" && row[5]=="524287" && row[11]=="7");
    tick(100);row=cells(output());assert(row[3]=="0" && row[5]=="0" && row[11]=="7");
    // Only new GRV cannot update magnetic heading. Singular angle retains old azimuth/roll.
    sample(4,{vertical[0],0,vertical[2],0});tick(100);row=cells(output());
    assert(row[5]==std::to_string((1<<13)|(1<<17)));near(std::stof(row[24]),-90);assert(row[23]=="90");
    sample(2,{0,-30,0});tick(100);row=cells(output());
    assert(!(std::stoul(row[5])&(1<<15)));near(std::stof(row[26]),2.862405f); // stale GRV
    stop(id);assert(!cal_sets);
    // A queued pre-write response must not conclude configuration/restoration.
    cmd("STREAM_IMU START rate_hz=2"); tick(12);
    features[0].config=hardware[0]; features[0].config.reportInterval_us=0;
    features[0].known=true; ++features[0].generation;
    pending_feature=-1; tick();
    assert(std::string(lab::stream_state())=="CONFIGURING");
    pending_feature=0; tick(15);
    assert(std::string(lab::stream_state())=="STREAMING");
    id=lab::stream_session_id(); output(); output(); output();
    cmd("STREAM_IMU STOP session_id="+std::to_string(id)); tick();
    features[0].config.reportInterval_us=20000; ++features[0].generation;
    pending_feature=-1; tick(); assert(std::string(lab::stream_state())=="RESTORING");
    pending_feature=0; tick(15); assert(!lab::stream_busy());
    assert(output().find("restored=1")!=std::string::npos);
    id=start();lab::stream_disconnect(1);tick(15);assert(!lab::stream_busy() && output().empty());
    id=start();tick(3,5000);tick(15);assert(!lab::stream_busy());assert(output().find("HEARTBEAT_TIMEOUT")!=std::string::npos);
    id=start();
    // PING keeps the session alive beyond the old network command timeout.
    for(unsigned i=0;i<14;++i) { cmd("STREAM_IMU PING session_id="+std::to_string(id));tick(1,5000); }
    assert(std::string(lab::stream_state())=="STREAMING");
    ++diag.epoch;mask=7;tick(20);assert(!lab::stream_busy() && cal_sets==1 && mask==0x15);
    assert(output().find("IMU_RESET")!=std::string::npos);
    // Failure after partial mutation rolls back actual non-default configs.
    cmd("STREAM_IMU START rate_hz=2");tick(12);bad_write=1;tick(3);bad_write=-1;tick(20);
    assert(!lab::stream_busy());assert(output().find("restored=1")!=std::string::npos);
    assert(hardware[0].reportInterval_us==0 && hardware[3].reportInterval_us==37000);
    id=start();replies=false;cmd("STREAM_IMU STOP session_id="+std::to_string(id));tick(230,50);
    assert(lab::stream_busy() && std::string(lab::stream_state())=="RESTORE_FAILED");
    assert(output().find("restored=0")!=std::string::npos);
    assert(cmd("STREAM_IMU START rate_hz=2").find("BUSY")!=std::string::npos);
    // A disconnected failed owner can be recovered by an explicit STOP from a
    // new connection using STATUS.stream_session_id. START remains blocked.
    lab::stream_disconnect(1); replies=true;
    assert(cmd("STREAM_IMU STOP session_id="+std::to_string(id),2).empty());
    tick(15); assert(!lab::stream_busy()); assert(output().find("restored=1")!=std::string::npos);
    assert(!saves);
    std::cout<<"Streaming geometry, retention, snapshot, rollback and lifecycle tests passed\n";
}
