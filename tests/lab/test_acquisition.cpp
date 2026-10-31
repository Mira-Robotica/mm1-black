#include <Arduino.h>
#include "lab/capture_service.h"
#include "lab/settings.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

bool calibration_blocked = false;
P4ImuDiagnostics diag{};
P4ImuSample snapshot{};
P4ImuGameSample game{};
P4ImuMagSample mag{};
bool p4_imu_begin(uint8_t) { diag.ready = true; return true; }
void p4_imu_poll() {}
P4ImuDiagnostics p4_imu_diagnostics() { return diag; }
bool p4_imu_snapshot(P4ImuSample *s) { *s = snapshot; return diag.ready; }

bool p4_imu_game_snapshot(P4ImuGameSample *s) { *s = game; return diag.ready; }
bool p4_imu_mag_snapshot(P4ImuMagSample *s) { *s = mag; return diag.ready; }

std::vector<uint8_t> frame()
{
    std::vector<uint8_t> f = {0xAA,0,0,0x20,0,4,0,0,0x12,0x34,0,0,0};
    for (unsigned i=1; i<12; ++i) f[12] += f[i];
    return f;
}
void checksum(std::vector<uint8_t> &f) { f[12]=0; for(unsigned i=1;i<12;++i) f[12]+=f[i]; }
void rx(const std::vector<uint8_t> &f) { fake_rx.insert(fake_rx.end(),f.begin(),f.end()); }
void tick(unsigned ms=5) { fake_ms+=ms; lab::capture_tick(); }
void reset()
{
    lab::capture_cancel(); fake_rx.clear(); fake_tx.clear(); fake_ms=0;
    diag={}; snapshot={1,0,0,0,0.025f,0,254,0}; game={1,0,0,0,0,17,0}; mag={12,-3,40,0,99,0}; lab::capture_begin();
}
void measure()
{
    assert(lab::capture_start(1));
    tick(50); tick(120); // Drain quiet period, laser warmup, then SINGLE.
    rx(frame()); tick();
}
void fresh()
{
    ++diag.empty_reads; tick();
    snapshot.generation=++diag.generation;
    game.generation=++diag.game_generation; mag.generation=++diag.mag_generation; tick();
}
int main()
{
    calibration_blocked = true; assert(!lab::capture_start(1)); calibration_blocked = false;
    // Orientation reference cases: identity, q == -q, Z rotation, singularity, invalid norm.
    auto a=lab::orientation(1,0,0,0,1,0,0);
    assert(a.angles_valid && a.azimuth==90 && a.inclination==0 && a.roll==0);
    auto b=lab::orientation(-1,0,0,0,2,0,0);
    assert(b.azimuth==a.azimuth);
    const float h=std::sqrt(0.5f);
    a=lab::orientation(h,0,0,h,1,0,0);
    assert(a.angles_valid && (a.azimuth<0.001f || a.azimuth>359.999f));
    a=lab::orientation(h,0,h,0,1,0,0);
    assert(a.quaternion_valid && !a.angles_valid);
    assert(!lab::orientation(0,0,0,0,1,0,0).quaternion_valid);
    assert(!lab::orientation(NAN,0,0,0,1,0,0).quaternion_valid);
    assert(!lab::orientation(1,0,0,0,0,0,0).angles_valid);
    // Random finite unit rotations: compare preserved application formulas independently.
    for (int i=1;i<500;++i) {
        float w=std::sin(i),x=std::cos(i*2.f),y=std::sin(i*3.f),z=std::cos(i*4.f);
        float n=std::sqrt(w*w+x*x+y*y+z*z); w/=n;x/=n;y/=n;z/=n;
        a=lab::orientation(w,x,y,z,1,0,0);
        float az=std::fmod(std::atan2(1-2*(y*y+z*z),2*(x*y+w*z))*57.295779513f+360,360);
        assert(a.angles_valid && std::fabs(a.azimuth-az)<0.0001f);
    }
    lab::LaserParser parser; lab::LaserResult result;
    auto f=frame(); bool complete=false;
    parser.push(0x12,result);
    for (size_t i=0;i<f.size();++i) { complete=parser.push(f[i],result); assert(complete==(i==12)); }
    assert(result.valid && std::fabs(result.distance-1.234f)<1e-6f);
    f[12]^=1; for(auto c:f) assert(!parser.push(c,result));
    assert(!result.valid && !strcmp(result.error,"LASER_CHECKSUM"));
    f=frame(); f[6]=0xFA; checksum(f); for(auto c:f) complete=parser.push(c,result);
    assert(complete && !result.valid && !strcmp(result.error,"LASER_BCD"));
    f=frame(); f[5]=3; checksum(f); for(auto c:f) complete=parser.push(c,result);
    assert(complete && !result.valid);
    f=frame(); f[2]=1; checksum(f); for(auto c:f) complete=parser.push(c,result);
    assert(complete && result.valid); // Undocumented header bytes are not invented error codes.
    f=frame(); f[3]=0x22; checksum(f); for(auto c:f) assert(!parser.push(c,result));
    // A cached report/new callback before the drain barrier cannot satisfy capture.
    reset(); snapshot.generation=diag.generation=42; measure();
    tick(); assert(!lab::capture_ready());
    snapshot.generation=++diag.generation; tick(); assert(!lab::capture_ready());
    fresh(); assert(lab::capture_ready());
    auto s=lab::capture_sample(); assert(s.imu_valid && s.laser.valid);
    assert(s.imu.status==0 && std::fabs(s.imu.accuracy_rad-.025f)<1e-8f);
    char out[2048]; assert(lab::format_sample(out,sizeof(out),s));
    assert(std::strstr(out,"result=OK") && std::strstr(out,"n_rows=1"));
    assert(s.grv_valid && s.mag_valid && s.grv.sequence==17 && s.mag.sequence==99);
    assert(strstr(out,"n_imu_valid=1 n_grv_valid=1 n_mag_valid=1 imu_reset=0"));
    std::string csv(out);
    const auto row_start=csv.find('\n')+1, row_end=csv.find('\n',row_start);
    std::vector<std::string> columns;
    for(size_t pos=row_start;pos<=row_end;) {
        const auto end=std::min(csv.find(',',pos),row_end);
        columns.push_back(csv.substr(pos,end-pos));pos=end+1;
    }
    assert(columns.size()==35 && columns[19]=="1" && columns[24]=="17");
    assert(columns[27]=="12" && columns[28]=="-3" && columns[29]=="40");
    assert(columns[31]=="99" && columns[32]=="1" && columns[34]=="0");
    assert(!lab::format_sample(out,10,s));
    assert(!lab::capture_start(2));
    // Laser missing/timeout still evaluates IMU; late replies cannot contaminate the next request.
    reset(); assert(lab::capture_start(1)); tick(50);tick(120);tick(3200);
    fresh(); s=lab::capture_sample();
    assert(lab::capture_ready() && !s.laser.valid && s.imu_valid);
    lab::capture_cancel(); rx(frame()); assert(lab::capture_start(2));tick();fresh();
    s=lab::capture_sample(); assert(!s.laser.valid && !strcmp(s.laser.error,"LASER_RESYNC_REQUIRED"));
    // Reset, missing IMU, I/O/decode errors and absent freshness each produce separate failures.
    for (int failure=0;failure<5;++failure) {
        reset();measure();
        if(failure==0) ++diag.resets;
        if(failure==1) diag.ready=false;
        if(failure==2) {
            for (int i=0;i<LAB_IMU_MAX_RETRIES;++i) { ++diag.io_errors; tick(); assert(!lab::capture_ready()); }
            ++diag.io_errors;
        }
        if(failure==3) ++diag.decode_errors;
        tick(failure==4 ? 1000 : 5);
        s=lab::capture_sample(); assert(lab::capture_ready() && s.laser.valid && !s.imu_valid);
    }
#if LAB_IMU_MAX_RETRIES > 0
    // Recovery preserves already acquired RV, but requires a new drain barrier.
    reset();measure();++diag.empty_reads;tick();
    snapshot.generation=++diag.generation;tick();
    ++diag.io_errors; game.generation=++diag.game_generation;tick();
    assert(!lab::capture_ready() && lab::capture_sample().imu_valid);
    assert(!lab::capture_sample().grv_received);
    ++diag.empty_reads;tick();tick();assert(!lab::capture_ready());
    game.generation=++diag.game_generation;mag.generation=++diag.mag_generation;tick();
    assert(lab::capture_ready() && lab::capture_sample().grv_valid);
    assert(lab::capture_sample().imu_retries==1 && lab::capture_sample().imu_io_errors==1);
    assert(lab::format_sample(out,sizeof(out),lab::capture_sample()));
    assert(strstr(out,"imu_retries=1") && strstr(out,"imu_io_errors=1"));
    // Retrying does not renew the original IMU deadline or hide a reset.
    reset();measure();tick(900);++diag.io_errors;tick();tick(100);
    assert(lab::capture_ready() && !lab::capture_sample().imu_valid);
    reset();measure();++diag.io_errors;tick();++diag.epoch;tick();
    assert(lab::capture_ready() && lab::capture_sample().imu_reset);
#endif
    reset();measure(); snapshot={h,0,h,0,.02f,3,255,0};fresh();
    s=lab::capture_sample(); assert(s.imu_valid && !s.angles.angles_valid);
    assert(lab::format_sample(out,sizeof(out),s)); assert(strstr(out,"ANGLE_SINGULARITY"));
    assert(std::isnan(s.angles.azimuth) && std::isfinite(s.angles.inclination));
    // A decoded but bad quaternion retains finite raw data and quality for diagnosis.
    reset();measure();snapshot.w=2;fresh();s=lab::capture_sample();
    assert(s.imu_received && !s.imu_valid && !strcmp(s.imu_error,"IMU_BAD_QUAT"));
    assert(lab::format_sample(out,sizeof(out),s)); assert(strstr(out,",2,0,0,0,"));
    reset();measure();snapshot.x=NAN;fresh();s=lab::capture_sample();
    assert(s.imu_received && !s.imu_valid);
    assert(lab::format_sample(out,sizeof(out),s)); assert(!strstr(out,"nan"));
    reset();measure();++diag.empty_reads;tick();tick(1000);
    assert(!strcmp(lab::capture_sample().imu_error,"IMU_TIMEOUT"));
    // Independent report arrival: no equal sequence, generation or packet required.
    reset();measure();++diag.empty_reads;tick();
    mag.generation=++diag.mag_generation;tick();assert(!lab::capture_ready());
    game.generation=++diag.game_generation;tick();assert(!lab::capture_ready());
    snapshot.generation=++diag.generation;tick();assert(lab::capture_ready());
    // Every missing type times out independently; other samples remain intact.
    for (int missing=0;missing<3;++missing) {
        reset();measure();++diag.empty_reads;tick();
        if(missing!=0) snapshot.generation=++diag.generation;
        if(missing!=1) game.generation=++diag.game_generation;
        if(missing!=2) mag.generation=++diag.mag_generation;
        tick(); assert(!lab::capture_ready());tick(1000);s=lab::capture_sample();
        assert(lab::capture_ready() && s.laser.valid);
        assert(s.imu_valid==(missing!=0) && s.grv_valid==(missing!=1) && s.mag_valid==(missing!=2));
        assert(!strcmp(missing==0?s.imu_error:missing==1?s.grv_error:s.mag_error,
                       missing==0?"IMU_TIMEOUT":missing==1?"GRV_TIMEOUT":"MAG_TIMEOUT"));
        assert(lab::format_sample(out,sizeof(out),s) && strstr(out,"result=PARTIAL"));
    }
    // Stale GRV/MAG reports before the common drain cannot satisfy this capture.
    reset();measure();game.generation=++diag.game_generation;mag.generation=++diag.mag_generation;
    ++diag.empty_reads;tick();snapshot.generation=++diag.generation;tick();tick(1000);
    s=lab::capture_sample();assert(s.imu_valid && !s.grv_received && !s.mag_received);
    reset();measure();game.w=2;mag.y=NAN;fresh();s=lab::capture_sample();
    assert(s.imu_valid && s.grv_received && !s.grv_valid && !strcmp(s.grv_error,"GRV_BAD_QUAT"));
    assert(s.mag_received && !s.mag_valid && !strcmp(s.mag_error,"MAG_NONFINITE"));
    assert(lab::format_sample(out,sizeof(out),s) && strstr(out,",12,,40,") && !strstr(out,"nan"));
    reset();measure();++diag.empty_reads;tick();
    snapshot.generation=++diag.generation;++diag.decode_errors;tick();
    s=lab::capture_sample();assert(lab::capture_ready() && s.imu_valid && s.laser.valid);
    assert(!s.grv_received && !strcmp(s.grv_error,"IMU_DECODE_ERROR"));
    // An epoch change preserves pre-reset RV and cannot combine it with new GRV/MAG.
    reset();diag.epoch=7;measure();++diag.empty_reads;tick();
    snapshot.generation=++diag.generation;tick();
    ++diag.epoch;game.generation=++diag.game_generation;mag.generation=++diag.mag_generation;tick();
    s=lab::capture_sample();assert(lab::capture_ready() && s.imu_valid && s.imu_epoch==7 && s.imu_reset);
    assert(!s.grv_received && !s.mag_received && !strcmp(s.grv_error,"IMU_RESET") && !strcmp(s.mag_error,"IMU_RESET"));
    assert(lab::format_sample(out,sizeof(out),s) && strstr(out,"imu_reset=1"));
    reset();assert(lab::capture_start(1));++diag.epoch;tick();
    assert(lab::capture_ready() && lab::capture_sample().imu_reset);
    assert(!strcmp(lab::capture_sample().imu_error,"IMU_RESET"));
    // Entirely missing sensors produce ERROR with zero counts.
    reset();assert(lab::capture_start(1));tick(50);tick(120);tick(3200);tick(1000);
    s=lab::capture_sample();assert(lab::capture_ready());
    assert(lab::format_sample(out,sizeof(out),s) && strstr(out,"result=ERROR"));
    assert(strstr(out,"n_valid=0 n_laser_valid=0 n_imu_valid=0 n_grv_valid=0 n_mag_valid=0"));
    reset();assert(lab::capture_start(1));tick(50);tick(120);lab::capture_cancel();
    assert(!strcmp(lab::laser_state(),"RESYNC_REQUIRED"));
    assert(fake_tx[fake_tx.size()-2]==0); // Last command turns beam off.
    // IMU-only requests work despite a sticky laser fault, without UART commands.
    const auto tx_before_imu = fake_tx.size();
    assert(lab::capture_start(2, false));
    rx(frame()); // An old distance must never enter the IMU-only sample.
    tick(); assert(!lab::capture_ready());
    fresh(); s=lab::capture_sample();
    assert(lab::capture_ready() && !s.laser_requested && !s.laser.valid);
    assert(!strcmp(s.laser.error,"LASER_DISABLED"));
    assert(s.imu_valid && s.grv_valid && s.mag_valid);
    assert(lab::format_sample(out,sizeof(out),s) && strstr(out,"result=OK"));
    assert(strstr(out,"n_valid=1 n_laser_valid=0") && strstr(out,"laser_requested=0"));
    lab::capture_cancel(); assert(fake_tx.size()==tx_before_imu);
    assert(!strcmp(lab::laser_state(),"RESYNC_REQUIRED"));
    // Switching back does not silently clear the laser fault.
    assert(lab::capture_start(3));tick();fresh();
    assert(!strcmp(lab::capture_sample().laser.error,"LASER_RESYNC_REQUIRED"));
    // Cached reports, missing reports, reset and retries retain their semantics.
    reset(); const auto tx_after_boot = fake_tx.size();
    assert(lab::capture_start(1, false));
    snapshot.generation=++diag.generation;
    game.generation=++diag.game_generation;mag.generation=++diag.mag_generation;
    ++diag.empty_reads;tick();tick();assert(!lab::capture_ready());
    snapshot.generation=++diag.generation;tick();tick(1000);
    s=lab::capture_sample();assert(s.imu_valid && !s.grv_valid && !s.mag_valid);
    assert(lab::format_sample(out,sizeof(out),s) && strstr(out,"result=PARTIAL"));
    lab::capture_cancel(); assert(fake_tx.size()==tx_after_boot);
    reset();assert(lab::capture_start(1,false));tick(1000);
    assert(lab::format_sample(out,sizeof(out),lab::capture_sample()) && strstr(out,"result=ERROR"));
    reset();assert(lab::capture_start(1,false));++diag.epoch;tick();
    assert(lab::capture_sample().imu_reset && lab::capture_ready());
    assert(!strcmp(lab::capture_sample().laser.error,"LASER_DISABLED"));
#if LAB_IMU_MAX_RETRIES > 0
    reset();assert(lab::capture_start(1,false));++diag.io_errors;tick();fresh();
    assert(lab::capture_ready() && lab::capture_sample().imu_valid);
    assert(lab::capture_sample().imu_retries==1);
#endif
    reset();assert(lab::capture_start(1,false));lab::capture_cancel();
    assert(!lab::capture_busy() && !strcmp(lab::laser_state(),"READY"));
    assert(lab::capture_start(2));tick(50);tick(120);rx(frame());tick();fresh();
    assert(lab::capture_ready() && lab::capture_sample().laser.valid);
    uint32_t id=0,count=0;
    assert(lab::parse_capture("CAPTURE 123",id,count) && id==123 && count==1);
    assert(lab::parse_capture("CAPTURE 4294967295 1",id,count));
    for (auto cmd : {"CAPTURE 0", "CAPTURE -1", "CAPTURE 4294967296", "CAPTURE 1 1 garbage", "CAPTURE 1x"})
        assert(!lab::parse_capture(cmd,id,count));
    bool use_laser=true;
    assert(lab::parse_capture("CAPTURE_IMU 123",id,count,&use_laser) && !use_laser && id==123 && count==1);
    assert(lab::parse_capture("CAPTURE_IMU 4294967295 1",id,count,&use_laser) && !use_laser);
    assert(lab::parse_capture("CAPTURE 124 1",id,count,&use_laser) && use_laser);
    for (auto cmd : {"CAPTURE_IMU", "CAPTURE_IMU 0", "CAPTURE_IMU -1", "CAPTURE_IMU 4294967296",
                     "CAPTURE_IMU 1 1 garbage", "CAPTURE_IMU 1x", "CAPTURE_IMUX 1"})
        assert(!lab::parse_capture(cmd,id,count,&use_laser));
    std::cout << "Native acquisition tests passed\n";
}

namespace lab { bool calibration_blocks_capture() { return calibration_blocked; } }
