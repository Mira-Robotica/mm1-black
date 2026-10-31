"""Coordinator ordering, disk/network failures and orientation semantics, offline."""
from contextlib import contextmanager, redirect_stdout, redirect_stderr
import copy
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import experiment as exp
import experiment_orientation as orientation
from test_capture import UNIT_HELLO, UNIT_STATUS, META, ACK, GOOD, exchange, response
from test_status import peer
from mock_server import read_command


class Robot(exp.SimulatedRobot):
    def __init__(self, events):
        super().__init__()
        self.events = events
        self.poses = self.poses[:3]
        self.fail_move = False
        self.fail_guard = False
        self.fail_snapshot = False

    def prepare(self):
        self.events.append("prepare")

    def move_and_settle(self, target, index):
        self.events.append(("move", index))
        if self.fail_move:
            raise RuntimeError("motion not completed")
        self.current = target

    def snapshot(self):
        if self.fail_snapshot:
            raise RuntimeError("feedback lost")
        result = super().snapshot()
        # Measured state intentionally differs from commanded state.
        result["joints_deg"] = [q + 0.01 for q in self.current]
        return result

    @contextmanager
    def capture_guard(self, target):
        self.events.append("guard_start")
        yield
        self.events.append("guard_end")
        if self.fail_guard:
            raise RuntimeError("stability lost")

    def stop(self):
        self.events.append("stop")


class Journal:
    def __init__(self, events):
        self.events = events
        self.records = []
        self.fail = None

    def write(self, record):
        if record["record_type"] == self.fail:
            raise OSError("disk full")
        self.events.append(("save", record["record_type"]))
        self.records.append(copy.deepcopy(record))


class CoordinatorTests(unittest.TestCase):
    def setUp(self):
        self.events = []
        self.robot = Robot(self.events)
        self.journal = Journal(self.events)
        self.trena = exp.SimulatedTrena()

    def run_exp(self, first_id=1):
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            return exp.run_experiment(self.robot, self.trena, self.journal, {}, first_id)

    def poses(self):
        return [r for r in self.journal.records if r["record_type"] == "pose"]

    def test_ordering_ids_and_measured_pose(self):
        self.assertEqual(self.run_exp(7), 0)
        rows = self.poses()
        self.assertEqual([r["capture_id"] for r in rows], [7, 8, 9])
        self.assertEqual([r["pose_id"] for r in rows], ["pose_000", "pose_001", "pose_002"])
        self.assertTrue(all(r["accepted"] and r["exchange_complete"] for r in rows))
        self.assertNotEqual(rows[0]["target_joints_deg"], rows[0]["robot_before"]["joints_deg"])
        self.assertLess(self.events.index("guard_end"), self.events.index(("save", "pose")))
        self.assertLess(self.events.index(("save", "pose")), self.events.index(("move", 1)))
        self.assertEqual(self.journal.records[-1]["outcome"], "complete")

    def test_motion_failure_never_captures_or_advances(self):
        self.robot.fail_move = True
        with patch.object(self.trena, "capture") as capture:
            self.assertEqual(self.run_exp(), 1)
            capture.assert_not_called()
        self.assertNotIn(("move", 1), self.events)
        self.assertEqual(self.poses()[0]["phase"], "move")

    def test_stability_loss_after_done_keeps_capture_and_stops(self):
        self.robot.fail_guard = True
        self.assertEqual(self.run_exp(), 1)
        row = self.poses()[0]
        self.assertTrue(row["exchange_complete"])
        self.assertFalse(row["accepted"])
        self.assertEqual(row["trena"]["capture"]["done"]["result"], "OK")
        self.assertNotIn(("move", 1), self.events)

    def test_feedback_lost_after_capture_stops_even_with_noop_guard(self):
        original = self.trena.capture
        def capture(capture_id, record):
            original(capture_id, record)
            self.robot.fail_snapshot = True
        self.trena.capture = capture
        self.assertEqual(self.run_exp(), 1)
        self.assertEqual(self.poses()[0]["robot_after_error"], "feedback lost")
        self.assertNotIn(("move", 1), self.events)

    def test_disk_failure_never_advances(self):
        for fail_at in ("session", "pose"):
            with self.subTest(fail_at=fail_at):
                self.setUp()
                self.journal.fail = fail_at
                self.assertEqual(self.run_exp(), 1)
                self.assertNotIn(("move", 1), self.events)
                self.assertIn("stop", self.events)
                if fail_at == "session":
                    self.assertNotIn("prepare", self.events)

    def test_interrupt_preserves_received_data_and_does_not_retry(self):
        def capture(capture_id, record):
            record["lines"] = ["# OK CAPTURE"]
            raise KeyboardInterrupt()
        self.trena.capture = capture
        self.assertEqual(self.run_exp(), 130)
        self.assertEqual(self.poses()[0]["trena"]["lines"], ["# OK CAPTURE"])
        self.assertFalse(self.poses()[0]["exchange_complete"])
        self.assertNotIn(("move", 1), self.events)

    def test_boot_or_epoch_change_stops_and_records_session(self):
        original = self.trena.capture
        def capture(capture_id, record):
            original(capture_id, record)
            if capture_id == 2:
                record["capture"]["orientation_session"]["imu_epoch"] = 1
        self.trena.capture = capture
        self.assertEqual(self.run_exp(), 1)
        self.assertTrue(self.poses()[1]["orientation_session_changed"])
        self.assertNotIn(("move", 2), self.events)

    def test_preflight_incompatible_protocol_prevents_motion(self):
        def server(conn):
            conn.sendall(UNIT_HELLO + META.replace(b"n_max=1", b"n_max=20"))
            read_command(conn)
            conn.sendall(UNIT_STATUS)
        with peer(server) as port:
            self.trena = exp.TcpTrena("127.0.0.1", port, 1)
            self.assertEqual(self.run_exp(), 1)
        self.assertNotIn("prepare", self.events)

    def tcp_run(self, server):
        # Test captures through real TCP; preflight separately covered above.
        with peer(server) as port:
            self.trena = exp.TcpTrena("127.0.0.1", port, .2)
            self.trena.preflight = lambda r: r.update(hello={"boot_id": "0123456789abcdef"})
            return self.run_exp(7)

    def test_done_is_required_before_next_pose(self):
        ack_seen = threading.Event()
        release = threading.Event()
        def server(conn):
            from test_capture import handshake
            handshake(conn)
            conn.sendall(ACK + b"\n".join(response().split(b"\n")[:2]) + b"\n")
            ack_seen.set()
            release.wait(2)
            conn.sendall(response().splitlines(keepends=True)[2])
        self.robot.poses = self.robot.poses[:1]
        with peer(server) as port:
            self.trena = exp.TcpTrena("127.0.0.1", port, 1)
            self.trena.preflight = lambda r: r.update(hello={"boot_id": "0123456789abcdef"})
            results = []
            thread = threading.Thread(target=lambda: results.append(self.run_exp(7)))
            thread.start()
            try:
                self.assertTrue(ack_seen.wait(2))
                self.assertTrue(thread.is_alive())
                self.assertFalse(self.poses())
            finally:
                release.set()
                thread.join(3)
            self.assertFalse(thread.is_alive())
            self.assertEqual(results, [0])

    def test_tcp_eof_bad_done_and_network_timeout_stop(self):
        no_done = b"\n".join(response().split(b"\n")[:2]) + b"\n"
        for body in (no_done, response().replace(b"n_rows=1", b"n_rows=2")):
            with self.subTest(body=body):
                self.setUp()
                self.assertEqual(self.tcp_run(lambda c: exchange(c, body)), 1)
                self.assertEqual(self.poses()[0]["trena"]["capture"]["sample"], GOOD)
                self.assertFalse(self.poses()[0]["exchange_complete"])
                self.assertNotIn(("move", 1), self.events)
        self.setUp()
        def fail(capture_id, record):
            raise TimeoutError("network timeout")
        self.trena.capture = fail
        self.assertEqual(self.run_exp(), 1)
        self.assertNotIn(("move", 1), self.events)

    def test_all_invalid_reports_are_saved_and_not_retried(self):
        sample = dict(GOOD)
        for group in ("laser", "imu", "grv", "mag"):
            sample[group + "_valid"] = "0"
            sample[group + "_error"] = group.upper() + "_TIMEOUT"
        sample.update(distance_m="", azimuth_deg="", inclination_deg="", roll_deg="",
                      angles_valid="0", angle_error="IMU_INVALID")
        self.assertEqual(self.tcp_run(lambda c: exchange(c, response(sample))), 1)
        row = self.poses()[0]
        self.assertTrue(row["exchange_complete"])
        self.assertEqual(row["trena"]["capture"]["done"]["result"], "ERROR")
        self.assertEqual(row["trena"]["capture"]["sample"], sample)
        self.assertFalse(row["accepted"])
        self.assertNotIn(("move", 1), self.events)

    def test_low_quality_and_singularity_do_not_invalidate_quaternion(self):
        sample = dict(GOOD, qw=str(math.sqrt(.5)), qy=str(math.sqrt(.5)),
                      azimuth_deg="", inclination_deg="-90", roll_deg="",
                      angles_valid="0", angle_error="ANGLE_SINGULARITY")
        self.robot.poses = self.robot.poses[:1]
        self.assertEqual(self.tcp_run(lambda c: exchange(c, response(sample))), 0)
        row = self.poses()[0]
        self.assertEqual(row["trena"]["capture"]["sample"]["imu_status"], "0")
        self.assertTrue(row["derived"]["rv"]["quaternion_valid"])
        self.assertIsNone(row["derived"]["robot_imu_rotation_error_deg"])


class OrientationTests(unittest.TestCase):
    def test_quaternion_sign_and_known_rotation(self):
        q = orientation.from_kortex_euler((0, 0, 90))
        self.assertAlmostEqual(orientation.rotation_error_deg(q, [-v for v in q]), 0)
        self.assertAlmostEqual(orientation.rotation_error_deg(q, (1, 0, 0, 0)), 90)

    def test_angles_match_laser_axis_and_singularity(self):
        result = orientation.angles((1, 0, 0, 0), (1, 0, 0))
        self.assertEqual(result["azimuth_deg"], 90)
        self.assertEqual(result["inclination_deg"], 0)
        result = orientation.angles(orientation.from_kortex_euler((0, 90, 0)), (1, 0, 0))
        self.assertFalse(result["angles_valid"])
        self.assertIsNone(result["azimuth_deg"])
        self.assertIsNone(result["roll_deg"])
        self.assertAlmostEqual(result["inclination_deg"], -90)
        self.assertEqual(result["angle_error"], "ANGLE_SINGULARITY")

    def test_invalid_orientation_does_not_become_valid(self):
        for q in ((0, 0, 0, 0), (2, 0, 0, 0), (float("nan"), 0, 0, 0)):
            with self.assertRaises(ValueError):
                orientation.normalized(q)


class StorageAndCliTests(unittest.TestCase):
    def test_exclusive_log_and_durable_write(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.jsonl"
            journal = exp.Journal(path)
            try:
                with patch.object(exp.os, "fsync", wraps=exp.os.fsync) as fsync:
                    journal.write({"hello": "world"})
                    fsync.assert_called_once()
                with self.assertRaises(FileExistsError):
                    exp.Journal(path)
            finally:
                journal.close()
            self.assertEqual(json.loads(path.read_text()), {"hello": "world"})

    def test_default_cli_has_no_ros_or_network_and_runs_all_43_poses(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.jsonl"
            with patch.object(exp.status, "query_status", side_effect=AssertionError("network")), \
                    redirect_stdout(io.StringIO()):
                self.assertEqual(exp.main(["--hold-sec", ".001", "--log", str(path)]), 0)
            rows = [json.loads(s) for s in path.read_text().splitlines()]
        self.assertEqual(sum(r["record_type"] == "pose" for r in rows), 43)
        self.assertEqual(rows[-1]["outcome"], "complete")
        self.assertEqual(rows[0]["metadata"]["configuration"]["execution_mode"], "simulated")
        self.assertIn("submodule_revision", rows[0]["metadata"]["provenance"])

    def test_invalid_configuration_before_motion_or_file_creation(self):
        for args in (["--first-capture-id", "4294967295"], ["--first-tilt-deg", "nan"],
                     ["--hold-sec", "0"], ["--execution-mode", "hardware", "--trena-mode", "simulated"],
                     ["--host", "hostname"], ["--execution-mode", "typo"]):
            with self.subTest(args=args), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                exp.main(args)


if __name__ == "__main__":
    unittest.main()
