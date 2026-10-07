#include "calibration_service.h"
#include "capture_service.h"
#include "settings.h"
#include "board/p4/SparkFun_BNO08x_Arduino_Library-1.0.6/src/sh2.h"
#include "board/p4/SparkFun_BNO08x_Arduino_Library-1.0.6/src/sh2_err.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>

namespace lab {
namespace {
enum class Phase { Idle, Preparing, Movements, Ready, Saving, Restoring };
enum class Op { None, ReadOriginal, Configure, Verify, Save, Restore, VerifyRestore };
Phase phase = Phase::Idle;
Op operation = Op::None;
CalOwner owner = CalOwner::Tcp;
uint32_t session = 0, step = 0, confirmed = 0, started = 0, resets = 0;
uint32_t drain_started = 0, drain_baseline = 0;
unsigned stage = 0, position = 0, positions = 6, axis = 0, round = 0, setup = 0;
uint8_t original_mask = 0, effective_mask = 0;
bool have_original = false, restore_failed = false, stop_requested = false;
bool save_sent = false;
int last_rc = SH2_OK;
const char *failed_op = "NONE";
int failed_rc = SH2_OK;
sh2_LabCommandDiagnostics failed_command{};
const char *op_name(Op op)
{
    switch (op) {
    case Op::ReadOriginal: return "READ_ORIGINAL";
    case Op::Configure: return "CONFIGURE";
    case Op::Verify: return "VERIFY";
    case Op::Save: return "SAVE";
    case Op::Restore: return "RESTORE";
    case Op::VerifyRestore: return "VERIFY_RESTORE";
    default: return "NONE";
    }
}
void record_failure(Op op, int rc, bool sent)
{
    if (rc == SH2_OK || strcmp(failed_op, "NONE")) return;
    failed_op = op_name(op); failed_rc = rc;
    if (sent) failed_command = sh2_lab_command_diagnostics();
    else { failed_command = {}; failed_command.tx_rc = rc; }
}
int read_rc = 1, config_rc = 1, verify_rc = 1, save_rc = 1, restore_rc = 1;
const char *result = "NONE", *detail = "NONE", *dcd = "NOT_SAVED";
P4ImuLabInfo initial{};

bool fresh(const P4ImuFeedback &f)
{
    return f.present && (uint32_t)(millis()-f.received_ms) < LAB_CAL_FRESH_MS;
}
bool magnetic_ready() { const auto f = p4_imu_lab_info().mag; return fresh(f) && f.status >= 2; }
const char *instruction()
{
    if (phase == Phase::Preparing) return "PREPARING";
    if (phase == Phase::Restoring) return "RESTORING";
    if (phase == Phase::Saving) return "WAIT_SAVE";
    if (phase == Phase::Idle) return "IDLE";
    switch (stage) {
    case 1: return "ENVIRONMENT";
    case 2: return "OBSERVE_MAG";
    case 3: return "ACCEL_POSITION";
    case 4: return "GYRO_REST";
    case 5: return axis == 0 ? "MAG_ROLL" : axis == 1 ? "MAG_PITCH" : "MAG_YAW";
    case 6: return "SAVE_DCD";
    }
    return "IDLE";
}
void rotations()
{
    phase = Phase::Movements; stage = 5; axis = 0; ++round; step = confirmed+1;
}
void end_session(const char *outcome, const char *why)
{
    result = outcome; detail = why;
    phase = Phase::Restoring; setup = 0;
    p4_imu_lab_invalidate();
}
bool start_op(Op purpose, sh2_LabOp op, uint8_t mask = 0)
{
    last_rc = sh2_lab_start(op, mask, LAB_CAL_OP_TIMEOUT_MS*1000);
    if (last_rc != SH2_OK) { record_failure(purpose, last_rc, false); return false; }
    operation = purpose;
    return true;
}
void stop(const char *outcome, const char *why)
{
    // Let any configuration command finish before restoring its original value.
    // SAVE is handled separately: once sent it must be awaited, never cancelled/retried.
    result = outcome; detail = why; stop_requested = true;
    if (operation == Op::None) end_session(outcome, why);
}
void complete_operation()
{
    int rc; uint8_t mask;
    if (operation == Op::None || !sh2_lab_result(&rc, &mask)) return;
    const auto completed = operation;
    operation = Op::None; last_rc = rc;
    switch (completed) {
    case Op::ReadOriginal: read_rc = rc; break;
    case Op::Configure: config_rc = rc; break;
    case Op::Verify: verify_rc = rc; break;
    case Op::Save: save_rc = rc; break;
    case Op::Restore: case Op::VerifyRestore:
        if (restore_rc >= 0) restore_rc = rc;
        break;
    default: break;
    }
    if (completed == Op::Save) {
        record_failure(completed, rc, true);
        dcd = rc == SH2_OK ? "SAVED" : rc == SH2_ERR_HUB ? "FAILED" : "UNKNOWN";
        end_session(rc == SH2_OK ? "SAVED" : "ERROR",
                    rc == SH2_OK ? "NONE" : rc == SH2_ERR_HUB ? "SAVE_FAILED" : "SAVE_UNKNOWN");
        return;
    }
    if (completed == Op::ReadOriginal && rc == SH2_OK) {
        original_mask = mask; have_original = true;
    }
    if (completed == Op::Verify || completed == Op::VerifyRestore) {
        if (rc == SH2_OK) effective_mask = mask;
        const uint8_t expected = completed == Op::Verify ? 7 : original_mask;
        if (rc == SH2_OK && mask != expected) {
            rc = last_rc = SH2_ERR_HUB;
            if (completed == Op::Verify) verify_rc = rc;
            else restore_rc = rc;
        }
    }
    record_failure(completed, rc, true);
    if (phase == Phase::Restoring) {
        if (rc != SH2_OK) restore_failed = true;
        ++setup;
    } else if (stop_requested) {
        end_session(result, detail);
    } else if (rc != SH2_OK) {
        end_session("ERROR", "SH2_COMMAND_FAILED");
    } else ++setup;
}
void configure_tick()
{
    if (operation != Op::None) return;
    int rc = SH2_OK;
    if (phase == Phase::Preparing) {
        switch (setup) {
        case 0: if (!start_op(Op::ReadOriginal, SH2_LAB_GET_CAL)) end_session("ERROR", "SH2_BUSY"); return;
        case 1: if (!start_op(Op::Configure, SH2_LAB_SET_CAL, 7)) end_session("ERROR", "SH2_BUSY"); return;
        case 2: if (!start_op(Op::Verify, SH2_LAB_GET_CAL)) end_session("ERROR", "SH2_BUSY"); return;
        case 3: rc = p4_imu_lab_report(SH2_ROTATION_VECTOR, 100000); break;
        case 4: rc = p4_imu_lab_report(SH2_GAME_ROTATION_VECTOR, 50000); break;
        case 5: rc = p4_imu_lab_report(SH2_MAGNETIC_FIELD_CALIBRATED, 20000); break;
        case 6:
            drain_baseline = p4_imu_diagnostics().empty_reads;
            drain_started = millis(); ++setup; return;
        case 7:
            if (p4_imu_diagnostics().empty_reads != drain_baseline) {
                p4_imu_lab_invalidate(); // No pre-session/backlogged quality is evidence.
                phase = Phase::Movements; stage = 1; step = 1;
            } else if ((uint32_t)(millis()-drain_started) >= LAB_CAL_OP_TIMEOUT_MS)
                end_session("ERROR", "IMU_BACKLOG");
            return;
        }
        last_rc = rc;
        if (rc != SH2_OK) end_session("ERROR", "REPORT_CONFIG_FAILED");
        else ++setup;
    } else if (phase == Phase::Restoring) {
        // Attempt every restore operation even after one fails. A failure keeps
        // acquisition blocked until reboot/reset and a successful restoration.
        switch (setup) {
        case 0: rc = p4_imu_lab_report(SH2_GAME_ROTATION_VECTOR, 0); break;
        case 1: rc = p4_imu_lab_report(SH2_MAGNETIC_FIELD_CALIBRATED, 0); break;
        case 2:
            if (have_original && p4_imu_ok()) {
                if (start_op(Op::Restore, SH2_LAB_SET_CAL, original_mask)) return;
                rc = last_rc;
            }
            break;
        case 3:
            if (have_original && p4_imu_ok()) {
                if (start_op(Op::VerifyRestore, SH2_LAB_GET_CAL)) return;
                rc = last_rc;
            }
            break;
        case 4: rc = p4_imu_lab_report(SH2_ROTATION_VECTOR, 20000); break;
        default:
            p4_imu_lab_invalidate();
            if (!p4_imu_ok()) restore_failed = true;
            if (restore_failed) { result = "ERROR"; detail = "RESTORE_FAILED"; }
            phase = Phase::Idle;
            return;
        }
        last_rc = rc;
        if (rc != SH2_OK) restore_failed = true;
        ++setup;
    }
}
void confirm_step()
{
    confirmed = step;
    switch (stage) {
    case 1: stage = 2; break;
    case 2: stage = 3; position = 1; break;
    case 3: if (position < positions) ++position; else stage = 4; break;
    case 4: rotations(); return;
    case 5:
        if (axis < 2) ++axis;
        else if (magnetic_ready()) {
            stage = 6; phase = Phase::Ready; step = 0; // SAVE uses the session, not a confirmation ID.
            return;
        }
        else { rotations(); return; }
        break;
    }
    ++step;
}
long age(const P4ImuFeedback &f) { return f.present ? (long)(uint32_t)(millis()-f.received_ms) : -1; }
int quality(const P4ImuFeedback &f) { return f.present ? f.status : -1; }
void response(char *out, size_t size, const char *action, const char *code, bool ok)
{
    const auto i = p4_imu_lab_info();
    const auto diag = p4_imu_diagnostics();
    const uint32_t elapsed = millis()-started;
    const uint32_t remaining = calibration_busy() && elapsed < LAB_CAL_SESSION_TIMEOUT_MS ? LAB_CAL_SESSION_TIMEOUT_MS-elapsed : 0;
    const char *allowed = phase == Phase::Idle ? (restore_failed ? "NONE" : "START") :
        phase == Phase::Movements ? (stage == 2 && !fresh(i.mag) ? "CANCEL" : "CONFIRM,CANCEL") :
        phase == Phase::Ready ? (magnetic_ready() ? "SAVE,CANCEL" : "CANCEL") :
        phase == Phase::Preparing ? "CANCEL" : "WAIT";
    const int n = snprintf(out, size,
        "# %s CAL_IMU action=%s code=%s session_id=%lu state=%s phase=%s step_id=%lu stage=%u "
        "instruction=%s confirmed=%lu positions=%u position=%u round=%u owner=%s allowed=%s "
        "result=%s detail=%s dcd=%s restore=%s remaining_ms=%lu limit_ms=%lu fresh_ms=%lu op_ms=%lu rc=%d "
        "read_rc=%d config_rc=%d verify_rc=%d save_rc=%d restore_rc=%d "
        "failed_op=%s failed_rc=%d cmd=%u cmd_seq=%u cmd_tx_rc=%d cmd_rx=%lu cmd_matched=%lu "
        "cmd_last=%u cmd_last_seq=%u cmd_status=%u cmd_control=%lu cmd_unknown=%lu cmd_truncated=%lu "
        "original_mask=%d effective_mask=%u autosave_rc=%d autosave_ack=NOT_DEFINED "
        "rv_us=%lu game_us=%lu mag_us=%lu "
        "mag_status=%d mag_age_ms=%ld mag_gen=%lu mag_seq=%u "
        "game_status=%d game_age_ms=%ld game_gen=%lu game_seq=%u "
        "rv_status=%d rv_age_ms=%ld rv_gen=%lu rv_seq=%u accuracy_rad=%.9g "
        "initial_rv_status=%d initial_accuracy_rad=%.9g initial_rv_us=%lu initial_game_us=%lu initial_mag_us=%lu "
        "part=%lu build=%lu version=%u.%u.%u "
        "resets=%lu io_errors=%lu decode_errors=%lu rv_gaps=%lu game_gaps=%lu mag_gaps=%lu\n",
        ok ? "OK" : "ERR", action, code, (unsigned long)session, calibration_state(),
        phase == Phase::Preparing ? "PREPARING" : phase == Phase::Restoring ? "RESTORING" : "NONE",
        (unsigned long)step, stage, instruction(), (unsigned long)confirmed, positions, position, round,
        owner == CalOwner::Tcp ? "TCP" : "SERIAL", allowed, result, detail, dcd,
        restore_failed ? "FAILED" : phase == Phase::Restoring ? "PENDING" : "OK",
        (unsigned long)remaining, (unsigned long)LAB_CAL_SESSION_TIMEOUT_MS,
        (unsigned long)LAB_CAL_FRESH_MS, (unsigned long)LAB_CAL_OP_TIMEOUT_MS, last_rc,
        read_rc, config_rc, verify_rc, save_rc, restore_rc,
        failed_op, failed_rc, failed_command.command, failed_command.sequence, failed_command.tx_rc,
        (unsigned long)failed_command.replies, (unsigned long)failed_command.matched,
        failed_command.last_command, failed_command.last_sequence, failed_command.last_status,
        (unsigned long)failed_command.control_packets, (unsigned long)failed_command.unknown_reports,
        (unsigned long)failed_command.truncated_reports,
        have_original ? original_mask : -1, effective_mask, i.autosave_rc,
        (unsigned long)i.rv_interval_us, (unsigned long)i.game_interval_us, (unsigned long)i.mag_interval_us,
        quality(i.mag), age(i.mag), (unsigned long)i.mag.generation, i.mag.sequence,
        quality(i.game), age(i.game), (unsigned long)i.game.generation, i.game.sequence,
        quality(i.rv), age(i.rv), (unsigned long)i.rv.generation, i.rv.sequence, (double)i.rv.accuracy_rad,
        quality(initial.rv), (double)initial.rv.accuracy_rad,
        (unsigned long)initial.rv_interval_us, (unsigned long)initial.game_interval_us, (unsigned long)initial.mag_interval_us,
        (unsigned long)i.part, (unsigned long)i.build,
        i.major, i.minor, i.patch, (unsigned long)diag.resets, (unsigned long)diag.io_errors,
        (unsigned long)diag.decode_errors, (unsigned long)diag.sequence_gaps,
        (unsigned long)i.game.sequence_gaps, (unsigned long)i.mag.sequence_gaps);
    if (n < 0 || (size_t)n >= size) snprintf(out, size, "# ERR CAL_IMU action=%s code=FORMAT\n", action);
}
bool number(const char *s, uint32_t &n)
{
    if (!s || !*s) return false;
    for (const char *p = s; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0; const unsigned long v = strtoul(s, nullptr, 10);
    if (errno || v == 0 || v > UINT32_MAX) return false;
    n = (uint32_t)v; return true;
}
} // namespace

bool calibration_busy() { return phase != Phase::Idle; }
bool calibration_blocks_capture() { return calibration_busy() || restore_failed; }
const char *calibration_state()
{
    switch (phase) {
    case Phase::Idle: return "IDLE";
    case Phase::Ready: return "READY_TO_SAVE";
    case Phase::Saving: return "SAVING";
    default: return "CALIBRATING";
    }
}
void calibration_tick()
{
    const auto diag = p4_imu_diagnostics();
    if (diag.resets != resets) {
        resets = diag.resets;
        if (calibration_busy() || restore_failed) {
            sh2_lab_abort(); operation = Op::None; restore_failed = false;
            if (phase == Phase::Saving) dcd = "UNKNOWN";
            end_session("ERROR", save_sent && !strcmp(dcd, "UNKNOWN") ? "SAVE_UNKNOWN" : "IMU_RESET");
        }
    }
    complete_operation();
    if (phase == Phase::Idle) return;
    if (phase != Phase::Saving && phase != Phase::Restoring && !stop_requested) {
        if (!diag.ready) stop("ERROR", "IMU_NOT_READY");
        else if ((uint32_t)(millis()-started) >= LAB_CAL_SESSION_TIMEOUT_MS) stop("TIMEOUT", "SESSION_TIMEOUT");
    }
    // Falling quality returns to explicit movements; rising quality alone never advances.
    if (phase == Phase::Ready && p4_imu_lab_info().mag.present &&
        p4_imu_lab_info().mag.status < 2) rotations();
    configure_tick();
}
void calibration_disconnect(CalOwner source)
{
    if (source != owner || !calibration_busy() || phase == Phase::Restoring) return;
    if (phase != Phase::Saving) stop("CANCELLED", "DISCONNECTED");
}
void calibration_command(const char *line, CalOwner source, char *out, size_t size)
{
    char input[129];
    if (strlen(line) >= sizeof(input)) { response(out, size, "INVALID", "BAD_COMMAND", false); return; }
    strcpy(input, line);
    char *tokens[6]{}; unsigned count = 0; char *save = nullptr;
    for (char *p = strtok_r(input, " ", &save); p && count < 6; p = strtok_r(nullptr, " ", &save)) tokens[count++] = p;
    const char *action = count >= 2 ? tokens[1] : "INVALID";
    const char *code = "NONE"; bool ok = true;
    uint32_t sid = 0, id = 0, faces = 6;
    if (count < 2 || strcmp(tokens[0], "CAL_IMU")) code = "BAD_COMMAND";
    else if (!strcmp(action, "STATUS") && count == 2) {}
    else if (!strcmp(action, "START") && (count == 2 || count == 3)) {
        if (count == 3 && (!number(tokens[2], faces) || faces < 4 || faces > 6)) code = "POSITIONS_RANGE";
        else if (capture_busy() || calibration_busy()) code = "BUSY";
        else if (restore_failed) code = "RESTORE_FAILED";
        else if (!p4_imu_ok()) code = "IMU_NOT_READY";
        else if (p4_imu_lab_info().autosave_rc != SH2_OK) code = "AUTOSAVE_FAILED";
        else if (session == UINT32_MAX) code = "SESSION_ID_EXHAUSTED";
        else {
            ++session; owner = source; positions = faces; step = confirmed = stage = position = round = setup = 0;
            have_original = stop_requested = save_sent = false; operation = Op::None;
            result = detail = "NONE"; dcd = "NOT_SAVED"; last_rc = SH2_OK;
            read_rc = config_rc = verify_rc = save_rc = restore_rc = 1;
            failed_op = "NONE"; failed_rc = SH2_OK; failed_command = {};
            initial = p4_imu_lab_info(); effective_mask = 0;
            started = millis(); resets = p4_imu_diagnostics().resets;
            laser_cancel(); phase = Phase::Preparing;
            code = "ACCEPTED";
        }
    } else if ((!strcmp(action, "CONFIRM") && count == 4) ||
               ((!strcmp(action, "SAVE") || !strcmp(action, "CANCEL")) && count == 3)) {
        if (!number(tokens[2], sid) || (count == 4 && !number(tokens[3], id))) code = "BAD_ID";
        else if (sid != session) code = "SESSION_MISMATCH";
        else if (source != owner) code = "NOT_OWNER";
        else if (!strcmp(action, "CANCEL")) {
            if (phase == Phase::Saving) code = "SAVE_IN_PROGRESS";
            else if (!calibration_busy() || phase == Phase::Restoring) code = "ALREADY_ENDED";
            else { stop("CANCELLED", "OPERATOR"); code = "ACCEPTED"; }
        } else if (!strcmp(action, "CONFIRM")) {
            if (id <= confirmed) code = "DUPLICATE";
            else if (phase != Phase::Movements || id != step || stop_requested) code = "STEP_MISMATCH";
            else if (stage == 2 && !fresh(p4_imu_lab_info().mag)) code = "MAG_STALE";
            else { confirm_step(); code = "CONFIRMED"; }
        } else if (save_sent) code = "SAVE_ALREADY_SENT";
        else if (phase != Phase::Ready) code = "NOT_READY_TO_SAVE";
        else if (!fresh(p4_imu_lab_info().mag)) code = "MAG_STALE";
        else if (!magnetic_ready()) { rotations(); code = "MAG_LOW"; }
        else {
            save_sent = true; phase = Phase::Saving; dcd = "PENDING";
            if (!start_op(Op::Save, SH2_LAB_SAVE_DCD)) {
                dcd = "NOT_SAVED"; end_session("ERROR", "SH2_BUSY"); code = "SH2_BUSY";
            } else code = "ACCEPTED";
        }
    } else code = "BAD_COMMAND";
    ok = !strcmp(code, "NONE") || !strcmp(code, "ACCEPTED") || !strcmp(code, "CONFIRMED") ||
         !strcmp(code, "DUPLICATE") || !strcmp(code, "ALREADY_ENDED") ||
         !strcmp(code, "SAVE_IN_PROGRESS") || !strcmp(code, "SAVE_ALREADY_SENT");
    response(out, size, action, code, ok);
}
} // namespace lab
