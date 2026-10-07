// Exercise real command encoding, reply matching, timeout and completion in sh2.c.
#include "board/p4/bno08x/sh2.c"
#include <assert.h>
#include <stdio.h>
static uint32_t clock_us;
static unsigned sends;
static int send_rc;
static CommandReq_t last_request;
static uint32_t get_time(sh2_Hal_t *h) { (void)h; return clock_us; }
int shtp_send(void *p, uint8_t chan, const uint8_t *data, uint16_t len)
{
    (void)p; (void)chan;
    assert(len == sizeof(last_request)); memcpy(&last_request, data, len); ++sends; return send_rc;
}
void shtp_service(void *p) { (void)p; clock_us += 100; }
static void reply(uint8_t command, uint8_t seq, uint8_t status, uint8_t mask)
{
    CommandResp_t r = {0}; r.reportId = SENSORHUB_COMMAND_RESP;
    r.command = command; r.commandSeq = seq; r.r[0] = status;
    for (int i = 0; i < 5; ++i) r.r[1+i] = (mask >> i) & 1;
    opRx(&_sh2, (const uint8_t *)&r, sizeof(r));
}
int main(void)
{
    sh2_Hal_t hal = {0}; hal.getTimeUs = get_time; _sh2.pHal = &hal;
    int rc; uint8_t mask;
    assert(sh2_lab_start(SH2_LAB_GET_CAL, 0, 2000000) == SH2_OK);
    assert(last_request.command == 7 && last_request.p[3] == 1);
    assert(!sh2_lab_result(&rc, &mask));
    assert(sh2_lab_start(SH2_LAB_SAVE_DCD, 0, 2000000) == SH2_ERR_OP_IN_PROGRESS);
    reply(6, last_request.seq, 0, 0x15); assert(!sh2_lab_result(&rc, &mask));
    reply(7, last_request.seq+1, 0, 0x15); assert(!sh2_lab_result(&rc, &mask));
    reply(7, last_request.seq, 0, 0x15); assert(sh2_lab_result(&rc, &mask) && rc == 0 && mask == 0x15);
    sh2_LabCommandDiagnostics diagnostics=sh2_lab_command_diagnostics();
    assert(diagnostics.command==7 && diagnostics.sequence==last_request.seq && diagnostics.tx_rc==0);
    assert(diagnostics.replies==3 && diagnostics.matched==1);
    assert(diagnostics.last_command==7 && diagnostics.last_sequence==last_request.seq && diagnostics.last_status==0);
    assert(sh2_lab_start(SH2_LAB_SET_CAL, 0x15, 2000000) == 0);
    assert(sh2_lab_command_diagnostics().replies==0);
    assert(last_request.p[0] == 1 && last_request.p[1] == 0 && last_request.p[2] == 1 && last_request.p[5] == 1);
    reply(7, last_request.seq, 1, 0); assert(sh2_lab_result(&rc, &mask) && rc == SH2_ERR_HUB);
    assert(sh2_lab_start(SH2_LAB_SAVE_DCD, 0, 2000000) == 0);
    unsigned sent = sends; clock_us += 2000000;
    reply(6, last_request.seq, 0, 0); // Late response must not produce a false success.
    assert(sh2_lab_result(&rc, &mask) && rc == SH2_ERR_TIMEOUT && sends == sent);
    clock_us = UINT32_MAX-100;
    assert(sh2_lab_start(SH2_LAB_SAVE_DCD, 0, 2000) == 0);
    clock_us += 2000; sh2_service(); assert(sh2_lab_result(&rc, &mask) && rc == SH2_ERR_TIMEOUT);
    assert(sh2_lab_start(SH2_LAB_SAVE_DCD, 0, 2000000) == 0);
    reply(6, last_request.seq, 0, 0); assert(sh2_lab_result(&rc, &mask) && rc == 0);
    send_rc = SH2_ERR_IO;
    assert(sh2_lab_start(SH2_LAB_SAVE_DCD, 0, 2000000) == 0);
    assert(sh2_lab_result(&rc, &mask) && rc == SH2_ERR_IO);
    assert(sh2_lab_command_diagnostics().tx_rc==SH2_ERR_IO);
    send_rc = 0;
    assert(sh2_lab_start(SH2_LAB_GET_CAL, 0, 2000000) == 0); sh2_lab_abort();
    assert(!sh2_lab_busy() && !sh2_lab_result(&rc, &mask));
    assert(sh2_setDcdAutoSave(false) == 0 && last_request.command == 9 && last_request.p[0] == 1);
    const uint32_t start = clock_us;
    assert(sh2_getCalConfig(&mask) == SH2_ERR_TIMEOUT && clock_us-start == 2000000);
    puts("SH-2 cooperative command tests passed");
}
