"""Calibration client: real TCP framing plus operator/monitor behavior without hardware."""
import contextlib
import io
import json
import os
from pathlib import Path
import socket
import sys
import tempfile
import time
import unittest
from unittest import mock

from test_status import peer

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import calibrate_imu as cal

HELLO = b"# HELLO MM1LAB 2 stage=unit_capture boot_id=1234567890abcdef connection_id=1\n"
META = b"# META commands=STATUS,CAPTURE,CAL_IMU capture=single cal_schema=1 cal_line_max=2048\n"


def sample(**changes):
    s = dict(action="STATUS", code="NONE", session_id="0", state="IDLE", phase="NONE", step_id="0",
             stage="0", instruction="IDLE", confirmed="0", positions="6", position="0", round="0",
             owner="TCP", allowed="START", result="NONE", detail="NONE", dcd="NOT_SAVED", restore="OK",
             remaining_ms="0", limit_ms="300000", fresh_ms="1000", op_ms="2000", rc="0",
             original_mask="-1", effective_mask="0", autosave_rc="0", autosave_ack="NOT_DEFINED",
             rv_us="20000", game_us="0", mag_us="0", accuracy_rad="0.025")
    for sensor in ("mag", "game", "rv"):
        s.update({f"{sensor}_status": "0", f"{sensor}_age_ms": "10", f"{sensor}_gen": "1", f"{sensor}_seq": "1"})
    s.update(changes)
    return s


def wire(s, ok=True):
    return ("# " + ("OK" if ok else "ERR") + " CAL_IMU " + " ".join(f"{k}={v}" for k, v in s.items()) + "\n").encode()


def read(conn):
    result = bytearray()
    while not result.endswith(b"\n"):
        data = conn.recv(1)
        if not data:
            return None
        result.extend(data)
    return result.decode().strip()


class MemoryJournal:
    def __init__(self):
        self.events = []

    def write(self, event, **values):
        self.events.append({"event": event, **values})


class CalibrationClientTests(unittest.TestCase):
    def test_retained_error_printed_once_in_ansi_and_plain_modes(self):
        # The actual bench result: rc was overwritten by successful cleanup.
        state=sample(session_id="1", result="ERROR", detail="SH2_COMMAND_FAILED",
                     rc="0", read_rc="-6", config_rc="1", verify_rc="1", save_rc="1", restore_rc="1")
        for ansi in (False, True):
            with self.subTest(ansi=ansi), contextlib.redirect_stdout(io.StringIO()) as out:
                display=cal.Display(MemoryJournal()); display.ansi=ansi
                with mock.patch.object(cal.time,"monotonic",side_effect=range(1000)):
                    for _ in range(80): display.show(state)
                    self.assertEqual(out.getvalue().count("Resultado:"),1)
                    self.assertEqual(out.getvalue().count("ler a configuração original (GET_CAL)"),1)
                    self.assertIn("timeout aguardando resposta válida (rc=-6)",out.getvalue())
                    self.assertIn("nenhum SAVE foi enviado",out.getvalue())
                    display.show(dict(state,session_id="2"))
                    self.assertEqual(out.getvalue().count("Resultado:"),2)

    def test_new_failure_diagnostics_survive_successful_cleanup(self):
        state=sample(result="ERROR", failed_op="READ_ORIGINAL", failed_rc="-6", config_rc="1",
                     cmd="7", cmd_seq="3", cmd_tx_rc="0", cmd_rx="2", cmd_matched="0",
                     cmd_control="2", cmd_unknown="0", cmd_truncated="0")
        lines="\n".join(cal.failure_lines(state))
        self.assertIn("GET_CAL",lines)
        self.assertIn("rc=-6",lines)
        self.assertIn("respostas=2, correspondentes=0",lines)
        self.assertEqual(cal.failure_lines(sample()),[])

    def test_fragmented_status_never_starts_and_logs(self):
        def server(conn):
            for byte in HELLO + META:
                conn.sendall(bytes([byte]))
            self.assertEqual(read(conn), "CAL_IMU STATUS")
            for byte in wire(sample()).replace(b"\n", b"\r\n"):
                conn.sendall(bytes([byte]))
            self.assertIsNone(read(conn))
        with peer(server) as port, socket.create_connection(("127.0.0.1", port)) as sock:
            journal = MemoryJournal()
            m = cal.Monitor(sock, 1, journal)
            m.handshake()
            self.assertEqual(m.exchange("CAL_IMU STATUS")["mag_status"], "0")
            self.assertIsNone(m.owned)
        self.assertEqual([e["event"] for e in journal.events], ["handshake", "command", "response"])

    def test_explicit_operator_actions_and_no_save_retry(self):
        requests = []
        replies = [sample(action="START", code="ACCEPTED", session_id="4", state="CALIBRATING", instruction="ENVIRONMENT", stage="1", step_id="1", allowed="CONFIRM,CANCEL"),
                   sample(action="CONFIRM", session_id="4", state="CALIBRATING", instruction="OBSERVE_MAG", stage="2", step_id="2", allowed="CONFIRM,CANCEL"),
                   sample(action="SAVE", session_id="4", state="SAVING", instruction="WAIT_SAVE", stage="6", allowed="WAIT", dcd="PENDING")]
        def server(conn):
            conn.sendall(HELLO + META)
            for s in replies:
                requests.append(read(conn))
                conn.sendall(wire(s))
            self.assertIsNone(read(conn))
        with peer(server) as port, socket.create_connection(("127.0.0.1", port)) as sock, contextlib.redirect_stdout(io.StringIO()):
            m = cal.Monitor(sock, 1, MemoryJournal()); m.handshake(); m.current = sample()
            m.operator("iniciar 4"); m.operator("")
            m.operator("salvar") # Not ready: cannot send SAVE.
            m.current = sample(session_id="4", state="READY_TO_SAVE", instruction="SAVE_DCD", allowed="SAVE,CANCEL")
            m.operator("") # Enter must not save.
            m.operator("salvar"); m.operator("salvar")
        self.assertEqual(requests, ["CAL_IMU START 4", "CAL_IMU CONFIRM 4 1", "CAL_IMU SAVE 4"])

    def test_save_precondition_refusal_allows_another_explicit_attempt(self):
        # Quality may fall between the last STATUS and processing SAVE on the P4.
        for code in ("MAG_LOW", "MAG_STALE", "NOT_READY_TO_SAVE"):
            seen = []
            ready = sample(session_id="4", state="READY_TO_SAVE", instruction="SAVE_DCD",
                           stage="6", allowed="SAVE,CANCEL", mag_status="2", rv_status="3")
            rejected = dict(ready, action="SAVE", code=code, allowed="CANCEL", mag_status="1")
            if code == "NOT_READY_TO_SAVE":  # Older firmware can still return to rotations.
                rejected.update(state="CALIBRATING", instruction="MAG_ROLL", stage="5")
            def server(conn):
                conn.sendall(HELLO + META)
                for command, state, ok in (
                    ("CAL_IMU SAVE 4", rejected, False),
                    ("CAL_IMU STATUS", ready, True),
                    ("CAL_IMU SAVE 4", dict(ready, action="SAVE", code="ACCEPTED",
                                           state="SAVING", instruction="WAIT_SAVE", dcd="PENDING", allowed="WAIT"), True),
                ):
                    seen.append(read(conn)); self.assertEqual(seen[-1], command)
                    conn.sendall(wire(state, ok))
                self.assertIsNone(read(conn))
            with self.subTest(code=code), peer(server) as port, socket.create_connection(("127.0.0.1", port)) as sock, contextlib.redirect_stdout(io.StringIO()) as out:
                m = cal.Monitor(sock, 1, MemoryJournal()); m.handshake(); m.owned = "4"; m.current = ready
                m.operator("salvar")
                self.assertFalse(m.save_attempted)
                self.assertIn("não iniciou a gravação", out.getvalue())
                m.operator("salvar")  # Still blocked locally: do not queue an automatic SAVE.
                m.exchange("CAL_IMU STATUS")
                m.operator("salvar"); self.assertTrue(m.save_attempted)
                m.operator("salvar")  # Accepted once: no resubmission.
            self.assertEqual(seen, ["CAL_IMU SAVE 4", "CAL_IMU STATUS", "CAL_IMU SAVE 4"])

    def test_save_refusal_only_unlocks_when_definitely_not_started(self):
        for patch in ({"code": "SH2_BUSY"}, {"code": "MAG_LOW", "dcd": "UNKNOWN"},
                      {"code": "MAG_LOW", "phase": "RESTORING"}):
            def server(conn):
                conn.sendall(HELLO + META)
                self.assertEqual(read(conn), "CAL_IMU SAVE 4")
                conn.sendall(wire(sample(action="SAVE", session_id="4", state="READY_TO_SAVE", **patch), False))
                self.assertIsNone(read(conn))
            with self.subTest(patch=patch), peer(server) as port, socket.create_connection(("127.0.0.1", port)) as sock, contextlib.redirect_stdout(io.StringIO()):
                m = cal.Monitor(sock, 1, MemoryJournal()); m.handshake(); m.owned = "4"
                m.exchange("CAL_IMU SAVE 4"); m.operator("salvar")
                self.assertTrue(m.save_attempted)

    def test_save_prompt_tracks_magnetic_feedback_without_changing_steps(self):
        ready = sample(session_id="4", state="READY_TO_SAVE", instruction="SAVE_DCD", stage="6",
                       mag_status="2", rv_status="3", allowed="SAVE,CANCEL")
        journal = MemoryJournal(); display = cal.Display(journal, plain=True)
        with contextlib.redirect_stdout(io.StringIO()), mock.patch.object(cal.time, "monotonic", return_value=1):
            display.show(ready)
            display.show(dict(ready, mag_status="1", allowed="CANCEL"))
            display.show(dict(ready, mag_age_ms="1000", allowed="CANCEL"))
            display.show(ready)
        messages = [e["text"] for e in journal.events if e["event"] == "instruction"]
        self.assertEqual(len(messages), 4)
        self.assertIn("digite salvar", messages[0])
        self.assertIn("Magnetic Field 2 ou 3", messages[1])
        self.assertNotIn("digite salvar", messages[1])
        self.assertIn("relatório recente", messages[2])
        self.assertEqual(messages[0], messages[3])

    def test_monitor_finishes_after_explicit_save_ack_and_restoration(self):
        reads, writes = os.pipe()
        seen = []
        ready = sample(session_id="4", state="READY_TO_SAVE", instruction="SAVE_DCD", stage="6",
                       mag_status="2", rv_status="3", allowed="SAVE,CANCEL")
        def server(conn):
            conn.sendall(HELLO + META)
            for expected, state, typed in (
                ("CAL_IMU STATUS", sample(), b"iniciar\n"),
                ("CAL_IMU START 6", dict(ready, action="START", code="ACCEPTED"), b"salvar\n"),
                ("CAL_IMU SAVE 4", dict(ready, action="SAVE", code="ACCEPTED", state="SAVING",
                                        instruction="WAIT_SAVE", dcd="PENDING", allowed="WAIT"), None),
                ("CAL_IMU STATUS", dict(ready, state="CALIBRATING", phase="RESTORING",
                                        instruction="RESTORING", dcd="SAVED", result="SAVED", restore="PENDING", allowed="WAIT"), None),
                ("CAL_IMU STATUS", sample(session_id="4", dcd="SAVED", result="SAVED"), None),
            ):
                seen.append(read(conn)); self.assertEqual(seen[-1], expected)
                conn.sendall(wire(state))
                if typed: os.write(writes, typed)
            self.assertIsNone(read(conn))
        try:
            with peer(server) as port, socket.create_connection(("127.0.0.1", port)) as sock, contextlib.redirect_stdout(io.StringIO()) as out:
                journal = MemoryJournal(); m = cal.Monitor(sock, 1, journal)
                self.assertEqual(cal.run_monitor(m, journal, plain=True, input_fd=reads), 0)
                self.assertIn("Resultado: SAVED", out.getvalue())
                self.assertEqual(m.current["restore"], "OK")
            self.assertEqual(seen.count("CAL_IMU SAVE 4"), 1)
        finally:
            os.close(reads); os.close(writes)

    def test_polls_while_operator_is_idle_and_exits_without_start(self):
        reads, writes = os.pipe()
        seen = []
        def server(conn):
            conn.sendall(HELLO + META)
            for _ in range(4):
                command = read(conn); seen.append(command)
                conn.sendall(wire(sample()))
            os.write(writes, b"sair\n")
            self.assertIsNone(read(conn))
        try:
            with peer(server) as port, socket.create_connection(("127.0.0.1", port)) as sock, contextlib.redirect_stdout(io.StringIO()):
                journal = MemoryJournal(); m = cal.Monitor(sock, 1, journal)
                self.assertEqual(cal.run_monitor(m, journal, plain=True, input_fd=reads), 0)
            self.assertEqual(seen, ["CAL_IMU STATUS"] * 4)
            self.assertTrue(any(e["event"] == "instruction" for e in journal.events))
        finally:
            os.close(reads); os.close(writes)

    def test_exit_awaits_inflight_save_and_restoration(self):
        seen = []
        def server(conn):
            conn.sendall(HELLO + META)
            for s in (sample(action="CANCEL", code="SAVE_IN_PROGRESS", session_id="2", state="SAVING", instruction="WAIT_SAVE", dcd="PENDING"),
                      sample(session_id="2", result="SAVED", dcd="SAVED")):
                seen.append(read(conn)); conn.sendall(wire(s))
        with peer(server) as port, socket.create_connection(("127.0.0.1", port)) as sock, contextlib.redirect_stdout(io.StringIO()):
            m = cal.Monitor(sock, 1, MemoryJournal()); m.handshake(); m.owned = "2"
            m.current = sample(session_id="2", state="SAVING"); m.finish_on_exit()
            self.assertEqual(m.current["dcd"], "SAVED")
        self.assertEqual(seen, ["CAL_IMU CANCEL 2", "CAL_IMU STATUS"])

    def test_interrupt_during_start_ack_consumes_it_before_cancel(self):
        def server(conn):
            conn.sendall(HELLO + META)
            self.assertEqual(read(conn), "CAL_IMU START 6")
            conn.sendall(wire(sample(action="START", code="ACCEPTED", session_id="8", state="CALIBRATING")))
            self.assertEqual(read(conn), "CAL_IMU CANCEL 8")
            conn.sendall(wire(sample(action="CANCEL", session_id="8", result="CANCELLED")))
        with peer(server) as port, socket.create_connection(("127.0.0.1", port)) as sock, contextlib.redirect_stdout(io.StringIO()):
            m = cal.Monitor(sock, 1, MemoryJournal()); m.handshake(); m.current = sample()
            m.awaiting_reply = "CAL_IMU START 6"
            sock.sendall(b"CAL_IMU START 6\n")
            m.finish_on_exit()
            self.assertEqual(m.owned, "8")
            self.assertEqual(m.current["result"], "CANCELLED")

    def test_malformed_feedback_ack_and_session_rejected(self):
        for patch in ({"action": "SAVE"}, {"mag_status": "4"}, {"accuracy_rad": "nan"},
                      {"instruction": "AUTO_SAVE"}, {"session_id": "99"}, {"rv_age_ms": "-2"}):
            def server(conn):
                conn.sendall(HELLO + META); read(conn); conn.sendall(wire(sample(**patch)))
            with self.subTest(patch=patch), peer(server) as port, socket.create_connection(("127.0.0.1", port)) as sock:
                m = cal.Monitor(sock, 1, MemoryJournal()); m.handshake(); m.owned = "0"
                with self.assertRaises(cal.ProtocolError): m.exchange("CAL_IMU STATUS")

    def test_eof_timeout_and_lost_save_ack_are_not_success(self):
        for mode in ("eof", "timeout", "long"):
            def server(conn):
                conn.sendall(HELLO + META)
                self.assertEqual(read(conn), "CAL_IMU SAVE 1")
                if mode == "timeout": time.sleep(0.2)
                elif mode == "long": conn.sendall(b"x" * 2050 + b"\n")
            with self.subTest(mode=mode), peer(server) as port, socket.create_connection(("127.0.0.1", port)) as sock:
                m = cal.Monitor(sock, 0.05, MemoryJournal()); m.handshake()
                with self.assertRaises((cal.ProtocolError, TimeoutError)): m.exchange("CAL_IMU SAVE 1")
                self.assertTrue(m.save_attempted)

    def test_log_records_clock_and_instruction_and_fails_before_send(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cal.jsonl"
            j = cal.Journal(path)
            with contextlib.redirect_stdout(io.StringIO()): cal.Display(j, plain=True).show(sample())
            j.close()
            record = json.loads(path.read_text())
            self.assertIn("pc_utc", record); self.assertEqual(record["instruction"], "IDLE")
        class BadJournal:
            def write(self, *args, **kwargs): raise OSError("disk full")
        class NoSocket:
            def sendall(self, data): self.fail("must not send")
        m = cal.Monitor(NoSocket(), 1, BadJournal())
        with self.assertRaises(OSError): m.exchange("CAL_IMU SAVE 1")
        self.assertFalse(m.save_attempted)

    def test_serial_owned_session_not_taken_over(self):
        m = cal.Monitor(None, 1, MemoryJournal())
        m.current = sample(session_id="3", owner="SERIAL", state="CALIBRATING", allowed="CONFIRM,CANCEL")
        with contextlib.redirect_stdout(io.StringIO()):
            for action in ("", "iniciar", "salvar", "cancelar"):
                m.operator(action)
        self.assertIsNone(m.owned)


if __name__ == "__main__":
    unittest.main()
