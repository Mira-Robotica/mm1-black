"""Exercise actual hardware adapter with ROS messages/services replaced locally."""
import importlib.util
from pathlib import Path
import sys
import threading
import types
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import experiment_robot as adapters

fixture_path = adapters.ROOT / "kinova-geomagic-noetic-demo/tests/test_spherical_sweep.py"
spec = importlib.util.spec_from_file_location("sweep_fixtures", fixture_path)
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)


class HardwareAdapterTests(unittest.TestCase):
    def setUp(self):
        self.addCleanup(patch.stopall)
        patch.dict(sys.modules, {"rospy": fixtures.rospy,
                                "kinova_spherical_sweep": fixtures.sweep}).start()
        fixtures.rospy.get_param.side_effect = lambda name, default: default
        fixtures.rospy.is_shutdown.return_value = False
        self.robot = adapters.hardware_robot()
        self.robot._stop = Mock()
        self.publish()

    def publish(self, joint=0, velocity=0, theta=90, fault=0):
        base = types.SimpleNamespace(tool_pose_x=.1, tool_pose_y=.2, tool_pose_z=.3,
                                     tool_pose_theta_x=0, tool_pose_theta_y=0, tool_pose_theta_z=theta,
                                     fault_bank_a=fault, fault_bank_b=0)
        actuators = [types.SimpleNamespace(position=joint, velocity=velocity) for _ in range(7)]
        self.robot.on_feedback(types.SimpleNamespace(base=base, actuators=actuators))

    def test_snapshot_is_measured_cartesian_feedback(self):
        self.publish(joint=.02)
        state = self.robot.snapshot()
        self.assertEqual(state["source"], "base_feedback")
        self.assertEqual(state["tcp"]["position_m"], [.1, .2, .3])
        self.assertEqual(state["tcp"]["theta_xyz_deg"], [0, 0, 90])
        self.assertAlmostEqual(state["tcp"]["quaternion_wxyz"][0], 2**-.5)
        self.assertEqual(state["joints_deg"], [.02] * 7)

    def test_transient_disturbance_latched_even_after_recovery(self):
        with self.assertRaisesRegex(RuntimeError, "stability"):
            with self.robot.capture_guard([0.] * 7):
                self.publish(joint=.2)
                self.publish()
        self.assertIsNone(self.robot.capture_target)

    def test_velocity_only_disturbance_latched(self):
        with self.assertRaisesRegex(RuntimeError, "stability"):
            with self.robot.capture_guard([0.] * 7):
                self.publish(velocity=1)
                self.publish()

    def test_watchdog_stops_on_silence_while_capture_is_blocked(self):
        self.robot.feedback_timeout = .05
        stopped = threading.Event()
        self.robot._stop.side_effect = stopped.set
        with self.assertRaisesRegex(RuntimeError, "stale"):
            with self.robot.capture_guard([0.] * 7):
                self.assertTrue(stopped.wait(1))
        self.robot._stop.assert_called_once()

    def test_feedback_gap_is_latched_even_if_watchdog_has_not_run(self):
        with self.assertRaisesRegex(RuntimeError, "gap"):
            with self.robot.capture_guard([0.] * 7):
                self.robot.previous_received -= 1
                self.publish()

    def test_fault_and_nonfinite_tcp_are_rejected(self):
        self.publish(fault=1)
        with self.assertRaisesRegex(RuntimeError, "faults"):
            self.robot.snapshot()
        self.publish(theta=float("nan"))
        with self.assertRaisesRegex(RuntimeError, "Nonfinite"):
            self.robot.snapshot()

    def test_move_reuses_action_end_and_existing_dwell(self):
        self.robot.send_pose = Mock()
        self.robot.hold_pose = Mock()
        self.robot.move_and_settle([0.] * 7, 1)
        self.robot.send_pose.assert_called_once_with([0.] * 7, track_j5=True)
        self.robot.hold_pose.assert_called_once_with([0.] * 7)


if __name__ == "__main__":
    unittest.main()
