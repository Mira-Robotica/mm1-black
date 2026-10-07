// Cooperative, single-owner SH-2 operations, compiled only for the lab target.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#if defined(MM1_LAB)
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { SH2_LAB_GET_CAL, SH2_LAB_SET_CAL, SH2_LAB_SAVE_DCD } sh2_LabOp;
// start sends once; sh2_service handles replies. No retries, threads or reentry.
int sh2_lab_start(sh2_LabOp op, uint8_t mask, uint32_t timeout_us);
bool sh2_lab_result(int *status, uint8_t *mask);
bool sh2_lab_busy(void);
void sh2_lab_abort(void);
typedef struct {
    int tx_rc;
    uint32_t replies, matched, control_packets, unknown_reports, truncated_reports;
    uint8_t command, sequence, last_command, last_sequence, last_status;
} sh2_LabCommandDiagnostics;
sh2_LabCommandDiagnostics sh2_lab_command_diagnostics(void);
// Snapshot before sh2_close(), which clears the boot diagnostics.
typedef struct {
    uint32_t control_packets, unknown_reports, truncated_reports;
    uint8_t control_channel, last_report, product_received, product_expected;
    uint8_t advertised_product_len;
} sh2_LabBootDiagnostics;
sh2_LabBootDiagnostics sh2_lab_boot_diagnostics(void);
#ifdef __cplusplus
}
#endif
#endif
