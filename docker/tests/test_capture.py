"""Single-capture client checks over actual loopback TCP, without the board."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import threading
import unittest

from test_status import peer, status
from mock_server import HELLO, STATUS, read_command

UNIT_HELLO = HELLO.replace(b"network_only", b"unit_capture")
UNIT_STATUS = STATUS.replace(b"network_only", b"unit_capture").replace(b"NOT_IMPLEMENTED", b"ENABLED")
META = b"# META capture=single commands=STATUS,CAPTURE schema=2 n_max=1 csv_header=per_capture\n"
ACK = b"# OK CAPTURE request_id=7 n=1 timeout_ms=9000\n"
HEADER = ("request_id,sample_index,distance_m,laser_valid,laser_error,qw,qx,qy,qz,"
          "azimuth_deg,inclination_deg,roll_deg,angles_valid,angle_error,accuracy_rad,"
          "imu_status,imu_seq,imu_valid,imu_error")
GOOD = dict(zip(HEADER.split(","),
                "7,1,1.234,1,NONE,1,0,0,0,90,0,0,1,NONE,0.025,0,255,1,NONE".split(",")))


def response(sample=None):
    sample = sample or GOOD
    lv, iv = int(sample["laser_valid"]), int(sample["imu_valid"])
    result = "OK" if lv and iv else "PARTIAL" if lv or iv else "ERROR"
    done = (f"# DONE request_id=7 completion=COMPLETE result={result} n_requested=1 n_rows=1 "
            f"n_valid={lv*iv} n_laser_valid={lv} n_imu_valid={iv} code=NONE state=IDLE\n")
    row = ",".join(sample[key] for key in HEADER.split(","))
    return (HEADER + "\n" + row + "\n" + done).encode()


def handshake(conn):
    conn.sendall(UNIT_HELLO + META)
    read_command(conn)  # Asserts exactly STATUS\n.
    conn.sendall(UNIT_STATUS)
    command = bytearray()
    while not command.endswith(b"\n") and len(command) < 100:
        chunk = conn.recv(1)
        if not chunk:
            break
        command.extend(chunk)
    assert command == b"CAPTURE 7 1\n", command


def exchange(conn, body=None, ack=ACK, fragmented=False):
    handshake(conn)
    data = ack + (response() if body is None else body)
    if fragmented:
        for byte in data.replace(b"\n", b"\r\n"):
            conn.sendall(bytes([byte]))
    else:
        conn.sendall(data)


class CaptureTests(unittest.TestCase):
    def test_unit_stage_status_without_capture(self):
        def server(conn):
            conn.sendall(UNIT_HELLO + META)
            read_command(conn)
            conn.sendall(UNIT_STATUS)
            self.assertEqual(conn.recv(1), b"")  # Client must not capture implicitly.
        with peer(server) as port:
            result = status.query_status("127.0.0.1", port, 1)
        self.assertNotIn("capture", result)

    def test_success_and_low_quality_preserved(self):
        with peer(exchange) as port:
            result = status.query_status("127.0.0.1", port, 1, 7)
        self.assertEqual(len(result["lines"]), 7)
        self.assertEqual(result["capture"]["sample"]["accuracy_rad"], "0.025")
        self.assertEqual(result["capture"]["sample"]["imu_status"], "0")

    def test_fragmented_capture_crlf(self):
        with peer(lambda c: exchange(c, fragmented=True)) as port:
            self.assertEqual(status.query_status("127.0.0.1", port, 1, 7)["capture"]["done"]["result"], "OK")

    def test_sensor_failures_singularities_and_raw_diagnostics(self):
        no_laser = dict(GOOD, distance_m="", laser_valid="0", laser_error="LASER_TIMEOUT")
        no_imu = dict(GOOD, **{key: "" for key in ("qw", "qx", "qy", "qz", "accuracy_rad", "imu_status",
                                                  "imu_seq", "azimuth_deg", "inclination_deg", "roll_deg")},
                      imu_valid="0", imu_error="IMU_TIMEOUT", angles_valid="0", angle_error="IMU_INVALID")
        both = dict(no_imu, distance_m="", laser_valid="0", laser_error="LASER_TIMEOUT")
        singular = dict(GOOD, azimuth_deg="", inclination_deg="-90", roll_deg="",
                        angles_valid="0", angle_error="ANGLE_SINGULARITY")
        bad_quat = dict(GOOD, qw="2", qx="", imu_valid="0", imu_error="IMU_BAD_QUAT",
                        azimuth_deg="", inclination_deg="", roll_deg="", angles_valid="0", angle_error="IMU_INVALID")
        for sample in (no_laser, no_imu, both, singular, bad_quat):
            with self.subTest(sample=sample), peer(lambda c: exchange(c, response(sample))) as port:
                result = status.query_status("127.0.0.1", port, 1, 7)
            self.assertEqual(result["capture"]["sample"], sample)

    def test_invalid_csv_or_done_rejected(self):
        cases = [response().replace(b"request_id,sample_index", b"wrong_header"),
                 response().replace(b"n_rows=1", b"n_rows=0"),
                 response().replace(b"result=OK", b"result=ERROR"),
                 response().replace(b"state=IDLE", b"state=BUSY"),
                 response().replace(b"completion=COMPLETE", b"completion=INTERRUPTED")]
        for fields in ({"request_id": "8"}, {"sample_index": "2"}, {"qw": "nan"},
                       {"qw": ""}, {"distance_m": "-1"}, {"accuracy_rad": "-1"},
                       {"imu_status": "4"}, {"imu_seq": "256"}, {"imu_seq": ""},
                       {"imu_valid": "01"}, {"laser_error": "LASER_TIMEOUT"}):
            cases.append(response(dict(GOOD, **fields)))
        for body in cases:
            with self.subTest(body=body), peer(lambda c: exchange(c, body)) as port:
                with self.assertRaises(status.ProtocolError):
                    status.query_status("127.0.0.1", port, 1, 7)

    def test_ack_id_count_and_timeout_are_checked(self):
        for ack in (ACK.replace(b"request_id=7", b"request_id=8"), ACK.replace(b"n=1", b"n=5"),
                    ACK.replace(b"9000", b"999999"), b"# ERR BUSY\n"):
            with self.subTest(ack=ack), peer(lambda c: exchange(c, ack=ack)) as port:
                with self.assertRaises(status.ProtocolError):
                    status.query_status("127.0.0.1", port, 1, 7)

    def test_eof_during_capture(self):
        lines = (ACK + response()).splitlines(keepends=True)
        for count in range(4):
            def server(conn):
                handshake(conn)
                conn.sendall(b"".join(lines[:count]))
            with self.subTest(count=count), peer(server) as port:
                with self.assertRaisesRegex(status.ProtocolError, "EOF"):
                    status.query_status("127.0.0.1", port, 1, 7)

    def test_capture_timeout_uses_ack_budget(self):
        done = threading.Event()
        def server(conn):
            handshake(conn)
            conn.sendall(ACK.replace(b"9000", b"1"))
            done.wait(1)
        with peer(server) as port:
            try:
                with self.assertRaisesRegex(TimeoutError, "CSV header"):
                    status.query_status("127.0.0.1", port, .05, 7)
            finally:
                done.set()

    def test_capture_cli_saves_measurement_failure_as_valid_exchange(self):
        sample = dict(GOOD, distance_m="", laser_valid="0", laser_error="LASER_TIMEOUT")
        with tempfile.TemporaryDirectory() as directory, peer(lambda c: exchange(c, response(sample))) as port:
            path = Path(directory) / "capture.jsonl"
            with contextlib.redirect_stdout(io.StringIO()):
                code = status.main(["--host", "127.0.0.1", "--port", str(port), "--capture", "7", "--log", str(path)])
            saved = json.loads(path.read_text())
        self.assertEqual(code, 0)
        self.assertTrue(saved["ok"])
        self.assertEqual(saved["capture_id"], 7)
        self.assertEqual(saved["capture"]["done"]["result"], "PARTIAL")

    def test_invalid_capture_id_before_connection(self):
        for value in (0, -1, 4294967296, "7", True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                status.query_status("127.0.0.1", capture_id=value)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
            status.main(["--host", "127.0.0.1", "--capture", "0"])
        self.assertEqual(raised.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
