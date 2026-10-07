// Real SHTP must propagate a failed reset write and release the instance.
#include "board/p4/bno08x/shtp.c"
#include <assert.h>
#include <stdio.h>
static int open_rc = -1;
static unsigned closes;
static int hal_open(sh2_Hal_t *h) { (void)h; return open_rc; }
static void hal_close(sh2_Hal_t *h) { (void)h; ++closes; }
int main(void) {
    sh2_Hal_t hal = {0}; hal.open=hal_open; hal.close=hal_close;
    for (unsigned i=0; i<10; ++i) assert(!shtp_open(&hal));
    assert(closes == 10);
    open_rc=0;
    void *transport=shtp_open(&hal); assert(transport); shtp_close(transport);
    assert(closes == 11);
    puts("SHTP boot HAL error propagation tests passed");
}
