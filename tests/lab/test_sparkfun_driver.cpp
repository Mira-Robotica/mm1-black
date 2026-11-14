// Actual SparkFun Arduino wrapper + board adapter + SH-2/SHTP/decoder.
// Only Wire, GPIO, clock and the remote device's packets are simulated.
#include "board/p4/p4_imu.h"
#include "board/p4/SparkFun_BNO08x_Arduino_Library-1.0.6/src/SparkFun_BNO08x_Arduino_Library.cpp"
#include <cassert>
#include <cstdio>

static uint8_t channel_seq[6]{};
static bool boot_enabled=true, products_enabled=true, replies_enabled=true;
static uint8_t calibration_mask=0x15;
static unsigned saves=0, software_resets=0;
static void packet(uint8_t channel, const std::vector<uint8_t> &payload)
{
    const unsigned n=payload.size()+4;
    std::vector<uint8_t> p{uint8_t(n),uint8_t(n>>8),channel,channel_seq[channel]++};
    p.insert(p.end(),payload.begin(),payload.end()); fake_wire_packets.push_back(p);
}
static void tlv(std::vector<uint8_t> &p, uint8_t tag, const std::vector<uint8_t> &v)
{
    p.push_back(tag); p.push_back(v.size()); p.insert(p.end(),v.begin(),v.end());
}
static void str(std::vector<uint8_t> &p, uint8_t tag, const char *s)
{
    tlv(p,tag,std::vector<uint8_t>(s,s+strlen(s)+1));
}
static void boot()
{
    fake_wire_packets.clear(); memset(channel_seq,0,sizeof(channel_seq));
    if (!boot_enabled) return;
    std::vector<uint8_t> adv{0};
    tlv(adv,1,{1,0,0,0}); str(adv,8,"executable"); tlv(adv,6,{1}); str(adv,9,"device");
    tlv(adv,1,{2,0,0,0}); str(adv,8,"sensorhub"); tlv(adv,6,{2}); str(adv,9,"control");
    tlv(adv,6,{3}); str(adv,9,"inputNormal");
    // No report length table: BNO firmware may omit it.
    packet(0,adv); packet(1,{1});
}
static void report(uint8_t seq)
{
    std::vector<uint8_t> p(41,0); // base timestamp + RV + Game RV + calibrated mag
    p[0]=0xfb; p[5]=5; p[6]=seq; p[7]=2; p[16]=0x40; // qw = 1 (Q14)
    p[17]=0x00; p[18]=0x08; // accuracy = 0.5 rad (Q12)
    p[19]=8; p[20]=seq+17; p[21]=3; p[30]=0x40;
    p[31]=3; p[32]=seq+99; p[33]=2;
    p[35]=0xc0; p[36]=0; p[37]=0xd0; p[38]=0xff; p[39]=0x80; p[40]=2; // 12,-3,40 uT (Q4)
    packet(3,p);
}
static void write_command(const std::vector<uint8_t> &p)
{
    if (p.size()==5 && p[2]==1) ++software_resets;
    if (p[4]==0xf9 && products_enabled) {
        for (int i=0;i<4;++i) packet(2,{0xf8,4,3,2,0x78,0x56,0x34,0x12,0x42,0,0,0,5,0,0,0});
    }
    if (p[4]!=0xf2) return;
    const uint8_t cmd=p[6];
    if (cmd==9) { assert(p[7]==1); return; } // disable autosave, no ACK exists
    if (cmd==6) ++saves;
    if (cmd==7 && p[10]==0)
        calibration_mask=p[7] | (p[8]<<1) | (p[9]<<2) | (p[11]<<3) | (p[12]<<4);
    if (!replies_enabled) return;
    std::vector<uint8_t> reply(16,0); reply[0]=0xf1; reply[2]=cmd; reply[3]=p[5];
    for (unsigned i=0;i<5;++i) reply[6+i]=(calibration_mask>>i)&1;
    packet(2,reply);
}
int main()
{
    fake_pin_hook=[](int pin,int level) { if (pin==MM1_LAB_IMU_RST && level==HIGH) boot(); };
    fake_wire_on_write=write_command;
    assert(p4_imu_begin(0x4b));
    assert(Wire1.port==1 && Wire1.sda==25 && Wire1.scl==34 && Wire1.hz==100000);
    assert(Wire.sda==-1 && Wire1.timeout==50 && Wire1.buffer_size==32);
    assert(Wire1.ends==1 && Wire1.begins==1 && Wire.ends==0 && Wire.begins==0);
    assert(fake_pin_writes.size()==2 && fake_pin_writes[0].pin==49 && fake_pin_writes[0].level==LOW);
    assert(fake_pin_writes[1].ms-fake_pin_writes[0].ms==10);
    assert(p4_imu_diagnostics().probe_elapsed_ms>=150 && !software_resets);
    assert(p4_imu_diagnostics().hardware_resets==1);
    assert(p4_imu_diagnostics().probe_attempts==1 && p4_imu_diagnostics().probe_4b_rc==0);
    assert(p4_imu_diagnostics().probe_4a_rc==-1);
    assert(p4_imu_diagnostics().rst_low==0 && p4_imu_diagnostics().rst_high==1);
    assert(p4_imu_lab_info().part==0x12345678 && p4_imu_lab_info().rv_interval_us==20000);
    assert(p4_imu_lab_info().game_interval_us==20000 && p4_imu_lab_info().mag_interval_us==20000);
    assert(p4_imu_diagnostics().epoch==1);
    assert(p4_imu_lab_info().rv_confirmed_us==-1 && p4_imu_lab_info().game_confirmed_us==-1);
    // GET_FEATURE_RESP is the sensor-reported interval, distinct from our request.
    packet(2,{0xfc,5,0,0,0,0x10,0x27,0,0,0,0,0,0,0,0,0,0});p4_imu_poll();
    assert(p4_imu_lab_info().rv_interval_us==20000 && p4_imu_lab_info().rv_confirmed_us==10000);
    P4ImuSample sample{}; assert(!p4_imu_snapshot(&sample));
    P4ImuGameSample game{};P4ImuMagSample mag{};
    assert(!p4_imu_game_snapshot(&game) && !p4_imu_mag_snapshot(&mag));
    report(255); p4_imu_poll(); assert(p4_imu_snapshot(&sample));
    assert(sample.w==1 && sample.accuracy_rad==0.5f && sample.status==2);
    assert(p4_imu_lab_info().game.present && p4_imu_lab_info().mag.present);
    assert(p4_imu_game_snapshot(&game) && p4_imu_mag_snapshot(&mag));
    assert(game.w==1 && game.status==3 && game.sequence==16);
    assert(mag.x==12 && mag.y==-3 && mag.z==40 && mag.status==2 && mag.sequence==98);
    const auto game_generation=game.generation, mag_generation=mag.generation;
    const auto generation=sample.generation;
    report(255); p4_imu_poll(); assert(p4_imu_diagnostics().generation==generation);
    assert(p4_imu_diagnostics().game_generation==game_generation && p4_imu_diagnostics().mag_generation==mag_generation);
    report(0); p4_imu_poll(); assert(p4_imu_diagnostics().generation==generation+1);
    assert(p4_imu_diagnostics().game_generation==game_generation+1 && p4_imu_diagnostics().mag_generation==mag_generation+1);
    assert(!p4_imu_diagnostics().sequence_gaps);
    // Streaming caches decode native Raw counts/timestamp and uncalibrated Q4,
    // including low status. Bias is deliberately NOT subtracted.
    std::vector<uint8_t> mixed(51,0);
    mixed[0]=0xfb; mixed[5]=0x16; mixed[6]=77;
    mixed[9]=0xff; mixed[10]=0xff; mixed[11]=2; mixed[13]=3; mixed[17]=0x78; mixed[18]=0x56;
    mixed[21]=0x0f; mixed[22]=88; mixed[25]=0xa0; mixed[27]=0x40; mixed[28]=1;
    mixed[29]=0xe0; mixed[30]=1; mixed[31]=0x50; // x=10 y=20 z=30 biasX=5
    mixed[37]=5; mixed[38]=1; // invalid zero-norm RV, must retain the good cache
    const auto good_rv=p4_imu_stream_sample(3);
    packet(3,mixed);p4_imu_poll();
    const auto raw=p4_imu_stream_sample(0),uncal=p4_imu_stream_sample(1);
    assert(raw.valid && raw.v[0]==-1 && raw.v[1]==2 && raw.v[2]==3 && raw.raw_timestamp_us==0x5678);
    assert(raw.status==0 && raw.epoch==p4_imu_diagnostics().epoch);
    assert(uncal.valid && uncal.v[0]==10 && uncal.v[1]==20 && uncal.v[2]==30 && uncal.status==0);
    assert(p4_imu_stream_sample(3).generation==good_rv.generation);
    assert(p4_imu_stream_sample(2).valid && p4_imu_stream_sample(4).valid);
    packet(3,mixed);p4_imu_poll();
    assert(p4_imu_stream_sample(0).generation==raw.generation && p4_imu_stream_sample(1).generation==uncal.generation);
    mixed[6]++; mixed[9]=7; mixed[10]=0; // only Raw changes
    packet(3,mixed);p4_imu_poll();
    assert(p4_imu_stream_sample(0).v[0]==7 && p4_imu_stream_sample(1).generation==uncal.generation);
    // Full asynchronous feature snapshot, including flags/batching/specific config.
    assert(sh2_lab_get_feature(0x16)==SH2_OK);
    packet(2,{0xfc,0x16,15,7,0,0x10,0x27,0,0,0xd2,4,0,0,42,0,0,0});p4_imu_poll();
    const auto feature=p4_imu_feature(0);
    assert(feature.known && feature.generation==1 && feature.config.reportInterval_us==10000);
    assert(feature.config.batchInterval_us==1234 && feature.config.sensorSpecific==42 && feature.config.changeSensitivity==7);
    assert(feature.config.changeSensitivityEnabled && feature.config.changeSensitivityRelative && feature.config.wakeupEnabled && feature.config.alwaysOnEnabled);
    const auto before=p4_imu_diagnostics();
    p4_imu_poll(); assert(p4_imu_diagnostics().empty_reads==before.empty_reads+1);
    fake_wire_read_error=true; p4_imu_poll(); fake_wire_read_error=false;
    assert(p4_imu_diagnostics().empty_reads==before.empty_reads+1);
    assert(p4_imu_diagnostics().io_errors==before.io_errors+1);
    int rc; uint8_t mask;
    assert(sh2_lab_start(SH2_LAB_GET_CAL,0,2000000)==0);
    p4_imu_poll(); assert(sh2_lab_result(&rc,&mask) && rc==0 && mask==0x15);
    assert(sh2_lab_start(SH2_LAB_SAVE_DCD,0,2000000)==0);
    p4_imu_poll(); assert(sh2_lab_result(&rc,&mask) && rc==0 && saves==1);
    // A reset during an operation aborts it and invalidates all cached feedback.
    replies_enabled=false;
    assert(sh2_lab_start(SH2_LAB_SAVE_DCD,0,2000000)==0);
    packet(1,{1}); p4_imu_poll();
    assert(!sh2_lab_busy() && !p4_imu_snapshot(&sample) && !p4_imu_lab_info().rv.present);
    assert(p4_imu_diagnostics().resets==before.resets+1 && saves==2);
    assert(p4_imu_diagnostics().epoch==before.epoch+1);
    assert(!p4_imu_game_snapshot(&game) && !p4_imu_mag_snapshot(&mag));
    for (unsigned g=0;g<5;++g) assert(!p4_imu_stream_sample(g).valid && !p4_imu_feature(g).known);
    assert(p4_imu_lab_info().rv_confirmed_us==-1);
    assert(p4_imu_lab_info().game_interval_us==20000 && p4_imu_lab_info().mag_interval_us==20000);
    assert(p4_imu_ok() && p4_imu_lab_info().autosave_rc==0);
    report(1); p4_imu_poll(); assert(p4_imu_snapshot(&sample));
    fake_wire_write_error=true;
    assert(sh2_lab_start(SH2_LAB_SAVE_DCD,0,2000000)==0);
    assert(sh2_lab_result(&rc,&mask) && rc==SH2_ERR_IO && saves==2);
    fake_wire_write_error=false;
    // Decode a maximum-sized cargo with repeated I2C headers, using actual HAL.
    std::vector<uint8_t> large(380,0x55); packet(3,large);
    uint8_t out[384]{}; uint32_t time=0;
    assert(i2chal_read(nullptr,out,sizeof(out),&time)==384 && time!=0);
    assert(out[4]==0x55 && out[383]==0x55 && fake_wire_packets.empty());
    assert(i2chal_write(nullptr,out,129)==SH2_ERR_IO);
    // Manual recovery performs one reset, no automatic SAVE, then succeeds.
    replies_enabled=true; p4_imu_request_reset(); p4_imu_poll();
    assert(p4_imu_ok() && p4_imu_diagnostics().hardware_resets==2 && saves==2);
    fake_wire_address=0x4a; assert(p4_imu_begin(0x4b));
    assert(p4_imu_diagnostics().address==0x4a);
    products_enabled=false;
    assert(!p4_imu_begin(0x4a));
    assert(!strcmp(p4_imu_diagnostics().init_stage,"PRODUCT_IDS"));
    assert(p4_imu_diagnostics().init_rc==SH2_ERR_TIMEOUT && p4_imu_diagnostics().init_ms<2450);
    products_enabled=true; boot_enabled=false;
    assert(!p4_imu_begin(0x4a));
    assert(!strcmp(p4_imu_diagnostics().init_stage,"SH2_OPEN") && p4_imu_diagnostics().init_ms<450);
    boot_enabled=true; fake_wire_address=0;
    assert(!p4_imu_begin(0x4b) && !strcmp(p4_imu_diagnostics().init_stage,"NO_ACK"));
    auto d=p4_imu_diagnostics();
    assert(d.probe_4b_rc==2 && d.probe_4a_rc==2 && d.probe_attempts>2);
    assert(d.init_ms>=510 && d.init_ms<565); // bounded readiness, no endless probing
    fake_wire_address=0x4b;
    // A device ready at 240 ms after NRST must not be rejected at the first 150 ms probe.
    fake_wire_ready_at=fake_ms+250;
    assert(p4_imu_begin(0x4b));
    d=p4_imu_diagnostics();
    assert(d.probe_attempts>2 && d.probe_elapsed_ms>=240 && d.probe_elapsed_ms<300);
    assert(d.probe_4b_rc==0 && d.probe_4a_rc==2);
    // Controller failure and bus timeout are not NACK and must not be retried.
    fake_wire_probe_rc=4;
    assert(!p4_imu_begin(0x4b));
    d=p4_imu_diagnostics();
    assert(!strcmp(d.init_stage,"I2C_PROBE_ERROR") && d.probe_4b_rc==4 && d.probe_4a_rc==-1);
    assert(d.probe_attempts==1 && d.init_ms<170);
    fake_wire_probe_rc=5;
    assert(!p4_imu_begin(0x4b));
    d=p4_imu_diagnostics();
    assert(!strcmp(d.init_stage,"I2C_PROBE_TIMEOUT") && d.probe_4b_rc==5);
    assert(d.probe_attempts==1 && d.init_ms<220);
    fake_wire_probe_rc=0;
    assert(p4_imu_begin(0x4b)); // no leaked SHTP instance
    assert(p4_imu_diagnostics().probe_attempts==1 && p4_imu_diagnostics().probe_4a_rc==-1);
    fake_wire_write_error=true; packet(1,{1}); p4_imu_poll();
    assert(!p4_imu_ok() && !p4_imu_snapshot(&sample)); // report restore failure is visible
    assert(!strcmp(p4_imu_diagnostics().init_stage,"REPORTS"));
    puts("SparkFun/Wire1/reset/SH-2 integration tests passed");
}
