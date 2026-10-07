// Real SH-2 + SHTP stack. Only the HAL and device packets are simulated.
#include "board/p4/SparkFun_BNO08x_Arduino_Library-1.0.6/src/sh2.h"
#include "board/p4/SparkFun_BNO08x_Arduino_Library-1.0.6/src/sh2_err.h"
#include "board/p4/imu_sparkfun/sh2_lab.h"
#include "board/p4/SparkFun_BNO08x_Arduino_Library-1.0.6/src/shtp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t clock_us;
static unsigned boot_packet, products_sent, products_to_send, requests, sensor_reports;
static bool requested, send_sensor, send_cal;
static unsigned advertise_lengths;
static uint8_t command_seq;
static unsigned response_size = 16;
static uint8_t advertisement[256];
static unsigned advert_len;
static void tlv(uint8_t tag, const void *value, uint8_t len) {
    advertisement[advert_len++] = tag; advertisement[advert_len++] = len;
    memcpy(advertisement+advert_len,value,len); advert_len += len;
}
static void app(uint8_t guid, const char *name, uint8_t channel, const char *channel_name) {
    const uint8_t id[] = {guid,0,0,0};
    tlv(TAG_GUID,id,4); tlv(TAG_APP_NAME,name,strlen(name)+1);
    tlv(TAG_NORMAL_CHANNEL,&channel,1); tlv(TAG_CHANNEL_NAME,channel_name,strlen(channel_name)+1);
}
static int hal_open(sh2_Hal_t *h) {
    (void)h; clock_us=0; boot_packet=products_sent=requests=sensor_reports=0;
    requested=send_sensor=send_cal=false;
    advertisement[0]=0; advert_len=1;
    app(1,"executable",1,"device"); app(2,"sensorhub",2,"control");
    uint8_t channel=3; tlv(TAG_NORMAL_CHANNEL,&channel,1); tlv(TAG_CHANNEL_NAME,"inputNormal",12);
    if (advertise_lengths) {
        const uint8_t lengths[]={0xf8,16,0xf1,16,0xfb,5,0x05,14,0x08,12,0x03,10};
        tlv(0x81,lengths+(advertise_lengths==2 ? 4 : 0),sizeof(lengths)-(advertise_lengths==2 ? 4 : 0));
    }
    return 0;
}
static void hal_close(sh2_Hal_t *h) { (void)h; }
static uint32_t time_us(sh2_Hal_t *h) { (void)h; return clock_us; }
static int packet(uint8_t *buffer, unsigned capacity, uint8_t channel, uint8_t seq,
                  const uint8_t *payload, unsigned length) {
    assert(length+4 <= capacity);
    buffer[0]=(length+4)&255; buffer[1]=(length+4)>>8; buffer[2]=channel; buffer[3]=seq;
    memcpy(buffer+4,payload,length); return length+4;
}
static int hal_read(sh2_Hal_t *h, uint8_t *buffer, unsigned capacity, uint32_t *timestamp) {
    (void)h; clock_us+=1000; *timestamp=clock_us;
    if (boot_packet++ == 0) return packet(buffer,capacity,0,0,advertisement,advert_len);
    if (boot_packet == 2) {
        uint8_t reset=1; return packet(buffer,capacity,1,0,&reset,1);
    }
    if (requested && products_sent < products_to_send) {
        uint8_t product[16]={0xf8,1,3,2,0x78,0x56,0x34,0x12,0x42,0,0,0,5,0,0,0};
        return packet(buffer,capacity,2,products_sent++,product,response_size);
    }
    if (send_cal) {
        send_cal=false;
        uint8_t response[16]={0xf1,0,7,command_seq,0,0,1,1,1};
        return packet(buffer,capacity,2,products_sent,response,sizeof(response));
    }
    if (send_sensor) {
        send_sensor=false;
        // Timestamp followed by RV, Game RV and magnetic field in one SHTP cargo.
        uint8_t reports[41]={0xfb,0,0,0,0,0x05};
        reports[19]=0x08; reports[31]=0x03;
        return packet(buffer,capacity,3,0,reports,sizeof(reports));
    }
    return 0;
}
static int hal_write(sh2_Hal_t *h, uint8_t *buffer, unsigned length) {
    (void)h;
    if (length == 16 && buffer[2] == 2 && buffer[4] == 0xf2) {
        assert(buffer[0] == 16 && buffer[1] == 0 && buffer[3] == 1);
        assert(buffer[6] == 7 && buffer[10] == 1); // GET calibration configuration.
        command_seq=buffer[5]; send_cal=true; return length;
    }
    // Validate the actual on-wire Product ID request.
    assert(length == 6 && buffer[0] == 6 && buffer[1] == 0);
    assert(buffer[2] == 2 && buffer[3] == 0 && buffer[4] == 0xf9 && buffer[5] == 0);
    requested=true; ++requests; return length;
}
static void sensor(void *cookie, sh2_SensorEvent_t *event) {
    (void)cookie;
    const uint8_t ids[]={0x05,0x08,0x03}, lengths[]={14,12,10};
    assert(sensor_reports<3 && event->reportId==ids[sensor_reports] && event->len==lengths[sensor_reports]);
    ++sensor_reports;
}
int main(void) {
    sh2_Hal_t hal={hal_open,hal_close,hal_read,hal_write,time_us};
    sh2_ProductIds_t ids;
    for (unsigned with_lengths=0; with_lengths<3; ++with_lengths) {
        advertise_lengths=with_lengths; products_to_send=4; response_size=16;
        memset(&ids,0,sizeof(ids));
        assert(sh2_open(&hal,NULL,NULL)==SH2_OK);
        assert(sh2_getProdIds(&ids)==SH2_OK);
        assert(requests==1 && ids.numEntries==4 && ids.entry[0].swPartNumber==0x12345678);
        sh2_LabBootDiagnostics d=sh2_lab_boot_diagnostics();
        assert(d.control_channel==2 && d.control_packets==4 && d.product_received==4 && d.product_expected==4);
        assert(d.advertised_product_len==(with_lengths==1 ? 16 : 0) && d.unknown_reports==0);
        assert(sh2_lab_start(SH2_LAB_GET_CAL,0,2000000)==SH2_OK);
        sh2_service(); int rc; uint8_t mask;
        assert(sh2_lab_result(&rc,&mask) && rc==SH2_OK && mask==7);
        const sh2_LabCommandDiagnostics command=sh2_lab_command_diagnostics();
        assert(command.command==7 && command.tx_rc==0 && command.control_packets==1);
        assert(command.replies==1 && command.matched==1 && !command.unknown_reports && !command.truncated_reports);
        sh2_setSensorCallback(sensor,NULL); send_sensor=true; sh2_service(); assert(sensor_reports==3);
        sh2_close();
    }
    // A partial response must not turn into successful identification.
    for (unsigned count=0; count<4; ++count) {
        products_to_send=count; memset(&ids,0,sizeof(ids));
        assert(sh2_open(&hal,NULL,NULL)==SH2_OK);
        assert(sh2_getProdIds(&ids)==SH2_ERR_TIMEOUT && clock_us<2100000);
        assert(requests==1);
        const sh2_LabBootDiagnostics d=sh2_lab_boot_diagnostics();
        assert(d.product_received==count && d.product_expected==4 && d.control_packets==count);
        sh2_close();
    }
    // Malformed F8 packets are rejected before casting to a 16-byte response.
    products_to_send=4; response_size=1;
    assert(sh2_open(&hal,NULL,NULL)==SH2_OK);
    assert(sh2_getProdIds(&ids)==SH2_ERR_TIMEOUT);
    const sh2_LabBootDiagnostics d=sh2_lab_boot_diagnostics();
    assert(d.truncated_reports==4 && d.product_received==0 && d.last_report==0xf8);
    sh2_close();
    puts("SH-2/SHTP Product ID and report parsing tests passed");
}
