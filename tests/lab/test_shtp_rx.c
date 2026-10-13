// Actual SHTP parser: malformed boot data must neither overwrite memory nor
// publish part of an advertisement. No Wire/ESP-IDF code is simulated here.
#include "board/p4/SparkFun_BNO08x_Arduino_Library-1.0.6/src/shtp.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned errors, adverts, cargos;
static void error(void *cookie, shtp_Event_t event) {
    (void)cookie;
    assert(event == SHTP_SHORT_FRAGMENT || event == SHTP_TOO_LARGE_PAYLOADS);
    ++errors;
}
static void advert(void *cookie, uint8_t tag, uint8_t len, uint8_t *value) {
    (void)cookie; (void)tag; (void)len; (void)value; ++adverts;
}
static void cargo(void *cookie, uint8_t *data, uint16_t len, uint32_t timestamp) {
    (void)cookie; (void)data; (void)len; (void)timestamp; ++cargos;
}
static void rejected(const uint8_t *data, size_t len) {
    shtp_t transport = {0};
    transport.advertPhase = ADVERT_REQUESTED;
    transport.eventCallback = error;
    addAdvertListener(&transport, 0, advert, NULL);
    shtp_t before = transport;
    // Allocate exactly the supplied length so ASan detects out-of-packet reads.
    uint8_t *packet = malloc(len); assert(packet);
    memcpy(packet, data, len);
    const unsigned count = errors, callbacks = adverts;
    processAdvertisement(&transport, packet, len);
    free(packet);
    assert(errors == count + 1 && adverts == callbacks);
    before.shortFragments++;
    assert(!memcmp(&transport, &before, sizeof(before)));
}
int main(void) {
    uint8_t long_name[67] = {RESP_ADVERTISE, TAG_APP_NAME, 64};
    memset(long_name+3, 'X', 63); long_name[66]=0;
    rejected(long_name, sizeof(long_name)); // Previously a reproducible stack overflow.
    long_name[1]=TAG_CHANNEL_NAME;
    rejected(long_name, sizeof(long_name));
    const uint8_t unterminated[] = {0, TAG_APP_NAME, 3, 'a', 'b', 'c'};
    rejected(unterminated, sizeof(unterminated));
    const uint8_t lone_tag[] = {0, TAG_GUID};
    rejected(lone_tag, sizeof(lone_tag));
    const uint8_t truncated[] = {0, TAG_GUID, 255, 0xff};
    rejected(truncated, sizeof(truncated));
    const uint8_t small_guid[] = {0, TAG_GUID, 1, 1};
    rejected(small_guid, sizeof(small_guid));
    const uint8_t small_limit[] = {0, TAG_MAX_TRANSFER_WRITE, 1, 32};
    rejected(small_limit, sizeof(small_limit));
    const uint8_t zero_limit[] = {0, TAG_MAX_TRANSFER_WRITE, 2, 0, 0};
    rejected(zero_limit, sizeof(zero_limit));
    const uint8_t zero_channel[] = {0, TAG_NORMAL_CHANNEL, 0};
    rejected(zero_channel, sizeof(zero_channel));
    const uint8_t null_tag[] = {0, TAG_NULL, 0};
    rejected(null_tag, sizeof(null_tag));
    // A good prefix followed by an invalid TLV must not register the prefix.
    const uint8_t partial[] = {0, TAG_GUID, 4, 2,0,0,0, TAG_APP_NAME, 8, 'x'};
    rejected(partial, sizeof(partial));

    shtp_t transport = {0};
    uint8_t valid[] = {0, TAG_GUID,4,2,0,0,0, TAG_APP_NAME,2,'a',0,
                      TAG_NORMAL_CHANNEL,1,2, TAG_CHANNEL_NAME,2,'c',0, 0x90,0};
    processAdvertisement(&transport, valid, sizeof(valid));
    assert(shtp_chanNo(&transport,"a","c")==2 && transport.advertPhase==ADVERT_IDLE);
    uint8_t boundary[35]={0,TAG_APP_NAME,32}; memset(boundary+3,'A',31); boundary[34]=0;
    assert(validAdvertisement(boundary,sizeof(boundary)));
    boundary[34]='A'; rejected(boundary,sizeof(boundary));
    uint8_t version[8]; memset(version,'V',sizeof(version));
    shtpAdvertHdlr(&transport,TAG_SHTP_VERSION,sizeof(version),version);
    assert(!transport.shtpVersion[0]);
    version[7]=0; shtpAdvertHdlr(&transport,TAG_SHTP_VERSION,sizeof(version),version);
    assert(!strcmp(transport.shtpVersion,"VVVVVVV"));
    transport.outMaxTransfer=4;
    assert(txProcess(&transport,0,version,1)==SH2_ERR_BAD_PARAM);

    // Consecutive fragments must not overflow the assembled cargo buffer.
    memset(&transport,0,sizeof(transport)); transport.eventCallback=error;
    transport.nextChanListener=1; transport.chan[0].callback=cargo;
    uint8_t fragment[304]={0x80,1,0,0}; // total cargo+header=384, only 304 bytes supplied
    rxAssemble(&transport,fragment,sizeof(fragment),1);
    assert(transport.inCursor==300 && transport.inRemaining==80 && !cargos);
    fragment[1]|=0x80; fragment[3]=1;
    unsigned count=errors;
    rxAssemble(&transport,fragment,204,2); // malicious continuation exceeds capacity
    assert(errors==count+1 && !cargos && !transport.inRemaining);

    // Deterministic corrupt advertisements: bounded TLVs and strings under sanitizers.
    uint32_t random=0x5ac39e57;
    for (unsigned n=0;n<4096;++n) {
        size_t len=1+n%380;
        uint8_t *data=malloc(len); assert(data); data[0]=0;
        for (size_t i=1;i<len;++i) { random=random*1664525u+1013904223u; data[i]=random>>24; }
        memset(&transport,0,sizeof(transport));
        processAdvertisement(&transport,data,len);
        free(data);
    }
    puts("SHTP malformed advertisement and fragment tests passed");
}
