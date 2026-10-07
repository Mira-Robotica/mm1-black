// Real session/protocol implementation; only sensor and acquisition I/O is fake.
#include <Arduino.h>
#include "lab/calibration_service.h"
#include "board/p4/p4_imu.h"
#include "board/p4/bno08x/sh2.h"
#include "board/p4/bno08x/sh2_err.h"
#include <cassert>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

P4ImuLabInfo info{};
P4ImuDiagnostics diag{};
bool capture_active = false, pending = false, hold = false, drain = true;
uint8_t mask = 0x15, requested_mask = 0;
int next_rc = 0, saves = 0, laser_off = 0, report_rc = 0;
sh2_LabOp pending_op{};
uint32_t op_started = 0, op_timeout = 0;
std::vector<std::pair<unsigned, unsigned>> reports;
std::vector<sh2_LabOp> commands;
namespace lab { bool capture_busy() { return capture_active; } void laser_cancel() { ++laser_off; } }
bool p4_imu_ok() { return diag.ready; }
P4ImuDiagnostics p4_imu_diagnostics() { return diag; }
P4ImuLabInfo p4_imu_lab_info() { return info; }
void p4_imu_lab_invalidate() { info.rv.present = info.mag.present = info.game.present = false; }
int p4_imu_lab_report(uint8_t id, uint32_t interval) {
    assert(!pending);
    reports.emplace_back(id, interval);
    if (report_rc) return report_rc;
    if (id == SH2_ROTATION_VECTOR) info.rv_interval_us = interval;
    if (id == SH2_GAME_ROTATION_VECTOR) info.game_interval_us = interval;
    if (id == SH2_MAGNETIC_FIELD_CALIBRATED) info.mag_interval_us = interval;
    return 0;
}
int sh2_lab_start(sh2_LabOp op, uint8_t m, uint32_t timeout) {
    commands.push_back(op);
    assert(!pending); pending = true; pending_op = op; requested_mask = m;
    op_started = fake_ms; op_timeout = timeout/1000;
    if (op == SH2_LAB_SAVE_DCD) ++saves;
    return 0;
}
bool sh2_lab_result(int *rc, uint8_t *out) {
    if (!pending || (hold && fake_ms-op_started < op_timeout)) return false;
    pending = false; *rc = hold ? SH2_ERR_TIMEOUT : next_rc; next_rc = 0;
    if (!*rc && pending_op == SH2_LAB_SET_CAL) mask = requested_mask;
    *out = mask; return true;
}
void sh2_lab_abort() { pending = false; }
sh2_LabCommandDiagnostics sh2_lab_command_diagnostics() {
    sh2_LabCommandDiagnostics d{}; d.command=7; d.sequence=42; return d;
}
using Fields = std::map<std::string, std::string>;
Fields cmd(const std::string &line = "CAL_IMU STATUS", lab::CalOwner owner = lab::CalOwner::Tcp) {
    char out[2048]; lab::calibration_command(line.c_str(), owner, out, sizeof(out));
    assert(std::string(out).find("code=FORMAT") == std::string::npos);
    std::istringstream stream(out); std::string t; Fields f;
    while (stream >> t) { auto p = t.find('='); if (p != std::string::npos) f[t.substr(0,p)] = t.substr(p+1); }
    assert(f.count("dcd")); return f;
}
void tick(unsigned n = 1) {
    for (unsigned i = 0; i < n; ++i) {
        fake_ms += 5; if (drain) ++diag.empty_reads; lab::calibration_tick();
    }
}
void magnetic(int status = 3) {
    info.mag.present = true; info.mag.status = status; info.mag.received_ms = fake_ms; ++info.mag.generation;
}
Fields confirm() {
    auto f = cmd(); return cmd("CAL_IMU CONFIRM " + f["session_id"] + " " + f["step_id"]);
}
std::string start(unsigned faces = 6, lab::CalOwner owner = lab::CalOwner::Tcp) {
    mask = 0x15; info.autosave_rc = 0; diag.ready = true;
    auto f = cmd("CAL_IMU START " + std::to_string(faces), owner);
    assert(f["code"] == "ACCEPTED"); const auto sid = f["session_id"];
    assert(lab::calibration_blocks_capture()); tick(12);
    assert(cmd()["instruction"] == "ENVIRONMENT");
    assert(mask == 7 && info.mag_interval_us == 20000 && info.game_interval_us == 50000 && info.rv_interval_us == 100000);
    assert(!info.mag.present); return sid;
}
void movements(unsigned faces = 6) {
    magnetic(3); confirm(); confirm();
    for (unsigned p = 1; p <= faces; ++p) { assert(cmd()["position"] == std::to_string(p)); confirm(); }
    assert(cmd()["instruction"] == "GYRO_REST"); confirm();
    assert(cmd()["instruction"] == "MAG_ROLL"); confirm(); confirm(); confirm();
    assert(cmd()["state"] == "READY_TO_SAVE");
}
void restored(const char *result) {
    tick(15); auto f = cmd();
    assert(f["state"] == "IDLE" && f["result"] == result);
    assert(!lab::calibration_blocks_capture() && mask == 0x15);
    assert(info.mag_interval_us == 0 && info.game_interval_us == 0 && info.rv_interval_us == 20000);
    assert(!info.mag.present && !info.rv.present);
}
int main() {
    diag.ready = false;
    assert(cmd("CAL_IMU START")["code"] == "IMU_NOT_READY");
    diag.ready = true; info.autosave_rc = -4;
    assert(cmd("CAL_IMU START")["code"] == "AUTOSAVE_FAILED");
    info.autosave_rc = 0; capture_active = true;
    assert(cmd("CAL_IMU START")["code"] == "BUSY"); capture_active = false;
    for (const char *bad : {"3", "7", "-4", "4294967296", "4 garbage"})
        assert(cmd(std::string("CAL_IMU START ")+bad)["code"] != "ACCEPTED");
    // Regression from the bench JSONL: original GET times out, cleanup sets rc=0.
    // Preserve the failed operation and never configure/save an unknown mask.
    mask=0x15;
    const auto command_count=commands.size();
    cmd("CAL_IMU START"); tick(); next_rc=SH2_ERR_TIMEOUT; tick(15);
    auto failed=cmd();
    assert(failed["state"]=="IDLE" && failed["result"]=="ERROR" && failed["rc"]=="0");
    assert(failed["failed_op"]=="READ_ORIGINAL" && failed["failed_rc"]=="-6");
    assert(failed["read_rc"]=="-6" && failed["config_rc"]=="1" && failed["save_rc"]=="1");
    assert(failed["cmd"]=="7" && failed["cmd_seq"]=="42" && failed["original_mask"]=="-1");
    assert(mask==0x15 && commands.size()==command_count+1 && commands.back()==SH2_LAB_GET_CAL);
    assert(failed["restore"]=="OK" && !lab::calibration_blocks_capture() && saves==0);
    for (unsigned faces : {4,5,6}) {
        auto sid = start(faces); const auto before = cmd();
        assert(before.at("failed_op")=="NONE" && before.at("failed_rc")=="0");
        magnetic(); tick(100); assert(cmd()["step_id"] == before.at("step_id")); // No auto-confirm.
        assert(cmd("CAL_IMU START")["code"] == "BUSY");
        assert(cmd("CAL_IMU SAVE " + sid)["code"] == "NOT_READY_TO_SAVE");
        assert(cmd("CAL_IMU CONFIRM 4294967295 1")["code"] == "SESSION_MISMATCH");
        assert(cmd("CAL_IMU CONFIRM " + sid + " 2")["code"] == "STEP_MISMATCH");
        assert(cmd("CAL_IMU CONFIRM " + sid + " 1", lab::CalOwner::Serial)["code"] == "NOT_OWNER");
        confirm(); auto dup = cmd("CAL_IMU CONFIRM " + sid + " 1");
        assert(dup["code"] == "DUPLICATE" && dup["step_id"] == "2");
        info.mag.present = false; assert(confirm()["code"] == "MAG_STALE");
        magnetic(0); confirm(); // Observation needs fresh report, not high quality.
        for (unsigned i = 0; i < faces; ++i) confirm();
        confirm(); confirm(); confirm(); confirm(); // Rest + roll/pitch/yaw, still low.
        assert(cmd()["round"] == "2" && cmd()["instruction"] == "MAG_ROLL");
        magnetic(3); tick(); assert(cmd()["instruction"] == "MAG_ROLL");
        confirm(); confirm(); confirm(); assert(cmd()["state"] == "READY_TO_SAVE");
        fake_ms += 1000; tick();
        assert(cmd("CAL_IMU SAVE " + sid)["code"] == "MAG_STALE");
        magnetic(1); tick(); assert(cmd()["instruction"] == "MAG_ROLL");
        magnetic(2); confirm(); confirm(); confirm();
        const int previous_saves = saves;
        assert(cmd("CAL_IMU SAVE " + sid)["state"] == "SAVING");
        assert(cmd("CAL_IMU CANCEL " + sid)["code"] == "SAVE_IN_PROGRESS");
        assert(cmd("CAL_IMU SAVE " + sid)["code"] == "SAVE_ALREADY_SENT");
        lab::calibration_disconnect(lab::CalOwner::Tcp);
        restored("SAVED"); assert(saves == previous_saves+1 && cmd()["dcd"] == "SAVED");
        cmd("CAL_IMU SAVE " + sid); assert(saves == previous_saves+1);
    }
    auto sid = start(); movements(); hold = true;
    cmd("CAL_IMU SAVE " + sid); tick(450); hold = false; tick(450);
    assert(cmd()["dcd"] == "UNKNOWN" && saves == 4);
    // Holding every operation also fails restore; a reset performs bounded recovery.
    ++diag.resets; sh2_lab_abort(); tick(15); assert(!lab::calibration_blocks_capture());
    sid = start(); movements(); next_rc = SH2_ERR_HUB; cmd("CAL_IMU SAVE " + sid);
    restored("ERROR"); assert(cmd()["dcd"] == "FAILED");
    sid = start(); lab::calibration_disconnect(lab::CalOwner::Serial); assert(lab::calibration_busy());
    lab::calibration_disconnect(lab::CalOwner::Tcp); restored("CANCELLED");
    sid = start(); fake_ms += 300000; tick(); restored("TIMEOUT");
    sid = start(); ++diag.resets; sh2_lab_abort(); tick(); restored("ERROR");
    assert(cmd()["detail"] == "IMU_RESET");
    sid = start(); movements(); cmd("CAL_IMU SAVE " + sid);
    ++diag.resets; sh2_lab_abort(); tick(); restored("ERROR"); assert(cmd()["dcd"] == "UNKNOWN");
    sid = start(); report_rc = SH2_ERR_IO; cmd("CAL_IMU CANCEL " + sid); tick(15);
    assert(cmd()["detail"] == "RESTORE_FAILED" && lab::calibration_blocks_capture());
    assert(cmd("CAL_IMU START")["code"] == "RESTORE_FAILED");
    report_rc = 0; ++diag.resets; tick(15); assert(!lab::calibration_blocks_capture());
    sid = start(4, lab::CalOwner::Serial);
    assert(cmd("CAL_IMU CANCEL " + sid)["code"] == "NOT_OWNER");
    cmd("CAL_IMU CANCEL " + sid, lab::CalOwner::Serial); restored("CANCELLED");
    // Cancel while waiting for the original mask: do not lose it or overlap SH-2 operations.
    mask = 0x15; auto f = cmd("CAL_IMU START"); tick(); assert(pending);
    cmd("CAL_IMU CANCEL " + f["session_id"]); restored("CANCELLED");
    drain = false; f = cmd("CAL_IMU START"); tick(450); drain = true; tick(15);
    assert(cmd()["detail"] == "IMU_BACKLOG");
    assert(laser_off > 0);
    std::cout << "Calibration session/protocol tests passed\n";
}
