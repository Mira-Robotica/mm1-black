#include <Arduino.h>
#include "lab/capture_service.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

P4ImuDiagnostics diag{};
P4ImuSample snapshot{};
bool p4_imu_begin(uint8_t) { diag.ready = true; return true; }
void p4_imu_poll() {}
P4ImuDiagnostics p4_imu_diagnostics() { return diag; }
bool p4_imu_snapshot(P4ImuSample *s) { *s = snapshot; return diag.ready; }

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
    diag={}; snapshot={1,0,0,0,0.025f,0,254,0}; lab::capture_begin();
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
    snapshot.generation=++diag.generation; tick();
}
int main()
{
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
        if(failure==2) ++diag.io_errors;
        if(failure==3) ++diag.decode_errors;
        tick(failure==4 ? 1000 : 5);
        s=lab::capture_sample(); assert(lab::capture_ready() && s.laser.valid && !s.imu_valid);
    }
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
    reset();assert(lab::capture_start(1));tick(50);tick(120);lab::capture_cancel();
    assert(!strcmp(lab::laser_state(),"RESYNC_REQUIRED"));
    assert(fake_tx[fake_tx.size()-2]==0); // Last command turns beam off.
    uint32_t id=0,count=0;
    assert(lab::parse_capture("CAPTURE 123",id,count) && id==123 && count==1);
    assert(lab::parse_capture("CAPTURE 4294967295 1",id,count));
    for (auto cmd : {"CAPTURE 0", "CAPTURE -1", "CAPTURE 4294967296", "CAPTURE 1 1 garbage", "CAPTURE 1x"})
        assert(!lab::parse_capture(cmd,id,count));
    std::cout << "Native acquisition tests passed\n";
}
