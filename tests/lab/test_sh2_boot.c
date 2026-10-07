// Real SH-2 open, simulated ordering of reset and channel advertisements.
#include "board/p4/bno08x/sh2.c"
#include <assert.h>
#include <stdio.h>
static uint32_t clock_us, reset_at, advert_at;
static bool transport_available = true;
static unsigned closes;
static uint32_t time_us(sh2_Hal_t *h) { (void)h; return clock_us; }
void *shtp_open(sh2_Hal_t *h) { (void)h; return transport_available ? &_sh2 : NULL; }
void shtp_close(void *p) { assert(p); ++closes; }
void shtp_setEventCallback(void *p, shtp_EventCallback_t *cb, void *c) { (void)p; (void)cb; (void)c; }
int shtp_listenAdvert(void *p, uint16_t g, shtp_AdvertCallback_t *cb, void *c) { (void)p; (void)g; (void)cb; (void)c; return 0; }
int shtp_listenChan(void *p, uint16_t g, const char *n, shtp_Callback_t *cb, void *c) { (void)p; (void)g; (void)n; (void)cb; (void)c; return 0; }
uint8_t shtp_chanNo(void *p, const char *a, const char *c) { (void)p; (void)c; return !strcmp(a,"sensorhub") ? 2 : 1; }
int shtp_send(void *p, uint8_t c, const uint8_t *d, uint16_t n) { (void)p; (void)c; (void)d; (void)n; return 0; }
void shtp_service(void *p) {
    (void)p; clock_us += 1000;
    if (clock_us == reset_at) {
        uint8_t response = 1;
        executableDeviceHdlr(&_sh2, &response, 1, clock_us);
    }
    if (clock_us == advert_at) sensorhubAdvertHdlr(&_sh2, 0, 0, NULL);
}
int main(void) {
    sh2_Hal_t hal = {0}; hal.getTimeUs = time_us;
    // Reset alone is not readiness: Product ID would use the invalid channel 0xff.
    clock_us=0; reset_at=100000; advert_at=350000;
    assert(sh2_open(&hal,NULL,NULL) == SH2_OK);
    assert(clock_us >= advert_at && _sh2.controlChan == 2 && _sh2.advertDone);
    sh2_close();
    clock_us=0; reset_at=400000; advert_at=50000;
    assert(sh2_open(&hal,NULL,NULL) == SH2_OK && clock_us >= reset_at); sh2_close();
    clock_us=0; reset_at=100000; advert_at=UINT32_MAX;
    assert(sh2_open(&hal,NULL,NULL) == SH2_ERR_TIMEOUT && clock_us <= 2001000); sh2_close();
    clock_us=0; reset_at=UINT32_MAX; advert_at=50000;
    assert(sh2_open(&hal,NULL,NULL) == SH2_ERR_TIMEOUT && clock_us <= 2001000); sh2_close();
    transport_available=false;
    assert(sh2_open(&hal,NULL,NULL) != SH2_OK);
    const unsigned before=closes; sh2_close(); assert(closes == before);
    puts("SH-2 boot ordering and failure tests passed");
}
