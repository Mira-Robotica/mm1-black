// Included by the vendored sh2.c after its operation definitions.
int sh2_lab_start(sh2_LabOp op, uint8_t mask, uint32_t timeout_us)
{
    sh2_t *pSh2 = &_sh2;
    // Check before touching opData: rejected calls must not corrupt an active op.
    if (labActive || pSh2->pOp) return SH2_ERR_OP_IN_PROGRESS;
    if (!pSh2->pHal || !timeout_us || timeout_us > 10000000) return SH2_ERR_BAD_PARAM;
    const sh2_Op_t *operation;
    switch (op) {
    case SH2_LAB_GET_CAL:
        pSh2->opData.getCalConfig.pSensors = &labMask;
        operation = &getCalConfigOp;
        break;
    case SH2_LAB_SET_CAL:
        pSh2->opData.calConfig.sensors = mask;
        operation = &setCalConfigOp;
        break;
    case SH2_LAB_SAVE_DCD:
        operation = &saveDcdNowOp;
        break;
    default: return SH2_ERR_BAD_PARAM;
    }
    memset(&labCommand, 0, sizeof(labCommand));
    labControlBaseline = labBoot.control_packets;
    labUnknownBaseline = pSh2->unknownReportIds;
    labTruncatedBaseline = labBoot.truncated_reports;
    labStarted = pSh2->pHal->getTimeUs(pSh2->pHal);
    labTimeout = timeout_us;
    labActive = true;
    // Even a send error is retained as a result; SAVE must never be retried.
    labCommand.tx_rc = opStart(pSh2, operation);
    labCommand.command = pSh2->lastCmdId;
    labCommand.sequence = pSh2->cmdSeq;
    return SH2_OK;
}
sh2_LabCommandDiagnostics sh2_lab_command_diagnostics(void)
{
    sh2_LabCommandDiagnostics result = labCommand;
    result.control_packets = labBoot.control_packets - labControlBaseline;
    result.unknown_reports = _sh2.unknownReportIds - labUnknownBaseline;
    result.truncated_reports = labBoot.truncated_reports - labTruncatedBaseline;
    return result;
}
sh2_LabBootDiagnostics sh2_lab_boot_diagnostics(void)
{
    sh2_LabBootDiagnostics result = labBoot;
    result.control_channel = _sh2.controlChan;
    result.unknown_reports = _sh2.unknownReportIds;
    for (int n = 0; n < SH2_MAX_REPORT_IDS; ++n)
        if (_sh2.report[n].id == SENSORHUB_PROD_ID_RESP) {
            result.advertised_product_len = _sh2.report[n].len;
            break;
        }
    return result;
}
bool sh2_lab_busy(void) { return labActive; }
bool sh2_lab_result(int *status, uint8_t *mask)
{
    if (!labActive) return false;
    labExpire(&_sh2);
    if (_sh2.pOp) return false;
    *status = _sh2.opStatus;
    *mask = labMask;
    labActive = false;
    return true;
}
void sh2_lab_abort(void)
{
    if (labActive) {
        _sh2.pOp = 0;
        _sh2.opStatus = SH2_ERR_IO;
        labActive = false;
    }
}
