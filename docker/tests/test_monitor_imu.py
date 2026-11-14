"""Real loopback protocol + granular data/recording/terminal contracts."""
import csv
import io
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from test_status import peer

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import monitor_imu as m

FIXTURES = Path(__file__).parent / "fixtures"
STREAM_META = (FIXTURES / "stream_meta.txt").read_text()
HELLO = "# HELLO MM1LAB 3 stage=multi_report_capture boot_id=0123456789abcdef connection_id=1\n"
META = "# META capture=single commands=STATUS,CAPTURE,CAPTURE_IMU,CAL_IMU,STREAM_IMU schema=3 stream_imu=1 stream_schema=1\n"


def command(conn):
    data = bytearray()
    while not data.endswith(b"\n"):
        chunk = conn.recv(1)
        if not chunk:
            break
        data.extend(chunk)
    return data.decode()


def begin(conn, meta=STREAM_META.replace("rate_hz=2", "rate_hz=5"), header=m.HEADER):
    # Fragment HELLO and concatenate other lines, exercising the actual parser.
    conn.sendall(HELLO[:7].encode())
    conn.sendall((HELLO[7:] + META).encode())
    assert command(conn) == "STATUS\n"
    conn.sendall(b"# OK STATUS imu=READY imu_epoch=5\n")
    assert command(conn).startswith("STREAM_IMU START rate_hz=")
    conn.sendall(("# OK STREAM_IMU START session_id=7 state=CONFIGURING\n"
                  "# OK STREAM_IMU READY session_id=7\n" + meta + header + "\n").encode())


def finish(conn, restored=1):
    assert command(conn) == "STREAM_IMU STOP session_id=7\n"
    # Data already in flight must not be mistaken for the final STOP ACK.
    conn.sendall((row(99) + "\n").encode())
    kind = "OK" if restored else "ERR"
    conn.sendall(f"# {kind} STREAM_IMU STOP session_id=7 restored={restored} code=NONE\n".encode())


def row(seq=1, new=98303, groups=31, **changes):
    cells = ["7", str(seq), "1000", str(groups), "98303", str(new)] + ["0"] * 5
    cells += ["1", "2", "3", "10", "20", "30", "40", "50", "60", "90", "-30", "0", "90", "30", "0", "", "270"]
    for key, value in changes.items():
        cells[(m.ENVELOPE + m.VALUES).index(key)] = str(value)
    return ",".join(cells)


class MonitorTests(unittest.TestCase):
    def args(self, port=5000, root="/unused", save=False):
        return m.arguments(["--host", "127.0.0.1", "--port", str(port), "--timeout", "0.3", "--rate-hz", "5",
                            "--save", str(save).lower(), "--output-dir", str(root), "--notes", "Trena fixa; ímã"])

    def test_shared_header_and_real_firmware_metadata(self):
        self.assertEqual((FIXTURES / "stream_header.csv").read_text().strip(), m.HEADER)
        meta = m.parse_stream_meta(STREAM_META.strip(), 7)
        self.assertEqual(meta["raw_mapping"], "unknown")
        self.assertTrue(meta["previous"].startswith("0/1234/15/7/42"))
        for replacement in (STREAM_META.replace("schema=1", "schema=2"),
                            STREAM_META.replace("front=+X", "front=+Y"),
                            STREAM_META.replace("angular_correction=none", "angular_correction=declination")):
            with self.assertRaises(m.ProtocolError):
                m.parse_stream_meta(replacement.strip(), 7)

    def test_magnetic_method_metadata_preserves_legacy_and_identifies_fix(self):
        legacy = m.parse_stream_meta(STREAM_META.strip(), 7)
        self.assertIn("atan2(-dot(m,l),", m.magnetic_method(legacy))
        corrected = m.parse_stream_meta(STREAM_META.strip() + " mag_azimuth_method=atan2_left_forward_v2", 7)
        self.assertIn("atan2(dot(m,l),", m.magnetic_method(corrected))
        with self.assertRaises(m.ProtocolError):
            m.parse_stream_meta(STREAM_META.strip() + " mag_azimuth_method=unknown", 7)

    def test_retention_and_sparse_csv_values(self):
        samples = m.Samples(7)
        first = samples.accept(row())
        self.assertEqual(first[10], -30)
        self.assertIsNone(first[15])
        self.assertEqual(samples.accept(row(2, new=0, groups=0)), [None] * 17)
        self.assertEqual(samples.retained, first)
        fresh = samples.accept(row(3, mag_raw_y_adc="nan", rv_azimuth_deg="nan", rv_inclination_deg=45))
        self.assertEqual(fresh[:3], [None] * 3)
        self.assertEqual(fresh[3:6], [10, 20, 30])
        self.assertIsNone(fresh[9])
        self.assertEqual(fresh[10], 45)
        self.assertEqual(samples.retained[:3], [1, 2, 3])
        self.assertEqual(samples.retained[9], 90)

    def test_structural_corruption_and_old_sessions_are_discarded(self):
        samples = m.Samples(7)
        samples.accept(row())
        for bad in (row(), row(2, session_id=8), row(2) + ",extra", row(2).rsplit(",", 1)[0], "x" * 2049,
                    row(2, updated_mask=32), row(2, valid_mask=0), row(-1)):
            with self.subTest(bad=bad):
                self.assertIsNone(samples.accept(bad))
                self.assertEqual(samples.seq, 1)

    def test_magnetic_group_validation_and_angle_ranges(self):
        samples = m.Samples(7)
        samples.accept(row())
        fresh = samples.accept(row(2, mag_uncal_z_uT="inf", rv_azimuth_deg=360, grv_roll_deg=181))
        self.assertEqual(fresh[3:6], [None] * 3)
        self.assertEqual(fresh[6:9], [40, 50, 60])
        self.assertIsNone(fresh[9])
        self.assertIsNone(fresh[14])

    def test_framing_oversized_nonascii_and_recovery(self):
        def server(conn):
            conn.sendall(b"x" * 6000 + b"\ninvalid\xff\n" + row().encode() + b"\r\n")
        with peer(server) as port, socket.create_connection(("127.0.0.1", port)) as conn:
            lines = m.Lines(conn)
            self.assertEqual(lines.read(1), "")
            self.assertEqual(lines.read(1), "")
            self.assertEqual(lines.read(1), row())
            self.assertLessEqual(len(lines.buffer), m.MAX_LINE + 1)

    def test_save_false_creates_nothing_and_stop_drains_data(self):
        stop = [False]
        class Output(io.StringIO):
            def flush(self):
                if "-30.000" in self.getvalue():
                    stop[0] = True
        def server(conn):
            begin(conn)
            conn.sendall((row() + "\n").encode())
            finish(conn)
        with tempfile.TemporaryDirectory() as root, peer(server) as port:
            out = Output()
            m.run(self.args(port, Path(root) / "absent"), lambda: stop[0], out)
            self.assertFalse((Path(root) / "absent").exists())
            self.assertNotIn("\x1b", out.getvalue())
            self.assertIn("Inclinação (pitch)", out.getvalue())

    def test_recording_metadata_once_and_csv_gaps(self):
        stop = [False]
        real_write = m.Recording.write
        mtimes = []
        def write(rec, elapsed, fresh):
            mtimes.append((rec.directory / "metadata.json").stat().st_mtime_ns)
            real_write(rec, elapsed, fresh)
            if len(mtimes) == 2:
                stop[0] = True
        def server(conn):
            begin(conn)
            conn.sendall((row() + "\n" + row(2, new=7, groups=1, mag_raw_x_adc=5) + "\n").encode())
            finish(conn)
        with tempfile.TemporaryDirectory() as root, peer(server) as port, patch.object(m.Recording, "write", write):
            m.run(self.args(port, root, True), lambda: stop[0], io.StringIO())
            directory, = Path(root).iterdir()
            meta = json.loads((directory / "metadata.json").read_text())
            self.assertEqual(meta["notes"], "Trena fixa; ímã")
            self.assertNotIn("ended", meta)
            self.assertEqual(mtimes[0], mtimes[1])
            self.assertEqual(mtimes[0], (directory / "metadata.json").stat().st_mtime_ns)
            with (directory / "samples.csv").open() as f:
                rows = list(csv.reader(f))
            self.assertEqual(rows[0], ["elapsed_s"] + m.VALUES)
            self.assertEqual(len(rows), 3)
            self.assertEqual(rows[2][1:4], ["5.0", "2.0", "3.0"])
            self.assertEqual(rows[2][4:], [""] * 14)
            # A second execution never overwrites a previous directory.
            other = m.Recording(self.args(root=root, save=True), {})
            other.close()
            self.assertEqual(len(list(Path(root).iterdir())), 2)

    def test_disk_failure_stops_and_restores_terminal(self):
        class TTY(io.StringIO):
            def isatty(self):
                return True
        def server(conn):
            begin(conn)
            conn.sendall((row() + "\n").encode())
            finish(conn)
        with tempfile.TemporaryDirectory() as root, peer(server) as port:
            out = TTY()
            with patch.object(m.Recording, "write", side_effect=OSError("disk full")), self.assertRaisesRegex(OSError, "disk full"):
                m.run(self.args(port, root, True), lambda: False, out)
            self.assertTrue(out.getvalue().endswith("\x1b[?25h\x1b[0m"))

    def test_failed_metadata_write_stops_session(self):
        def server(conn):
            begin(conn)
            finish(conn)
        with peer(server) as port, patch.object(m.json, "dump", side_effect=OSError("disk full")):
            with tempfile.TemporaryDirectory() as root, self.assertRaises(OSError):
                m.run(self.args(port, root, True), lambda: False, io.StringIO())

    def test_missing_capability_refuses_before_start(self):
        def server(conn):
            conn.sendall((HELLO + META.replace("stream_imu=1", "stream_imu=0")).encode())
            self.assertEqual(command(conn), "")
        with peer(server) as port, self.assertRaisesRegex(m.ProtocolError, "capability"):
            m.run(self.args(port), lambda: False, io.StringIO())

    def test_incompatible_schema_stops(self):
        def server(conn):
            begin(conn, header="invalid_header")
            finish(conn)
        with peer(server) as port, self.assertRaisesRegex(m.ProtocolError, "Cabeçalho"):
            m.run(self.args(port), lambda: False, io.StringIO())

    def test_absence_of_data_stops_and_failed_restore_is_not_success(self):
        for restored in (0, 1):
            def server(conn):
                begin(conn)
                finish(conn, restored)
            with self.subTest(restored=restored), peer(server) as port, self.assertRaises(TimeoutError if restored else m.ProtocolError):
                m.run(self.args(port), lambda: False, io.StringIO())

    def test_restore_failure_after_normal_exit_is_error(self):
        stop = [False]
        class Output(io.StringIO):
            def flush(self):
                stop[0] = True
        def server(conn):
            begin(conn)
            finish(conn, 0)
        with peer(server) as port, self.assertRaisesRegex(m.ProtocolError, "não confirmada"):
            m.run(self.args(port), lambda: stop[0], Output())

    def test_sigint_sigterm_send_stop(self):
        script = Path(m.__file__)
        for sig in (signal.SIGINT, signal.SIGTERM):
            process = []
            def server(conn):
                begin(conn)
                conn.sendall((row() + "\n").encode())
                # Wait for the parent to publish the Popen instance; never sleep for a session duration.
                deadline = time.monotonic() + 1
                while not process and time.monotonic() < deadline:
                    time.sleep(0.001)
                process[0].send_signal(sig)
                finish(conn)
            with self.subTest(sig=sig), peer(server) as port:
                proc = subprocess.Popen([sys.executable, str(script), "--host", "127.0.0.1", "--port", str(port),
                                         "--timeout", "1", "--rate-hz", "5"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                process.append(proc)
                stdout, stderr = proc.communicate(timeout=4)
                self.assertEqual(proc.returncode, 0, stderr)

    def test_cli_limits(self):
        for option, value in (("--rate-hz", "nan"), ("--rate-hz", "5.01"), ("--rate-hz", "0.19"), ("--timeout", "inf")):
            with patch("sys.stderr", io.StringIO()), self.assertRaises(SystemExit):
                m.arguments(["--host", "127.0.0.1", option, value])
