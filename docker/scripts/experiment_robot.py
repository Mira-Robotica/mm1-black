"""Robot adapters. Import ROS only when explicitly selecting rviz/hardware."""
from contextlib import contextmanager
from pathlib import Path
import sys
import threading
import time
import math

from experiment_orientation import from_kortex_euler

ROOT = Path(__file__).resolve().parents[2]
SWEEP_SCRIPTS = ROOT / "kinova-geomagic-noetic-demo/catkin_ws/src/kinova_geomagic_demo/scripts"
sys.path.insert(0, str(SWEEP_SCRIPTS))
from spherical_sweep_sequence import build_poses


class SimulatedRobot:
    """Instantaneous test double; explicitly does not model Cartesian kinematics."""
    def __init__(self, first_tilt_deg=120, hold_sec=1):
        self.poses = build_poses(first_tilt_deg)
        self.hold_sec = hold_sec
        self.current = [0.] * 7

    def prepare(self):
        pass

    def move_and_settle(self, target, index):
        self.current = list(target)
        time.sleep(self.hold_sec)

    def snapshot(self):
        return dict(source="simulated", received_monotonic_s=time.monotonic(),
                    joints_deg=list(self.current), velocities_deg_s=[0.] * 7,
                    tcp=None)

    @contextmanager
    def capture_guard(self, target):
        yield

    def finish(self):
        pass

    def stop(self):
        pass


class RvizRobot(SimulatedRobot):
    def __init__(self):
        from kinova_spherical_sweep import RvizSphericalSweep
        self.preview = RvizSphericalSweep()
        self.poses = self.preview.poses
        self.current = [0.] * 7

    def prepare(self):
        import rospy
        deadline = time.monotonic() + 10
        while not rospy.is_shutdown():
            self.preview.publish(self.current, [0.] * 7)
            if self.preview.publisher.get_num_connections():
                return
            if time.monotonic() >= deadline:
                break
            time.sleep(.05)
        raise RuntimeError("Missing RViz joint-state subscriber or ROS shutdown")

    def move_and_settle(self, target, index):
        if index:
            self.current = self.preview.animate(self.current, target, self.preview.duration, True)
            if self.current is None:
                raise RuntimeError("RViz execution interrupted")
        if self.preview.animate(self.current, self.current, self.preview.hold_sec, False) is None:
            raise RuntimeError("RViz execution interrupted")

    def snapshot(self):
        state = super().snapshot()
        state["source"] = "rviz_commanded"
        return state


def hardware_robot():
    import rospy
    from kinova_spherical_sweep import KinovaSphericalSweep

    class HardwareRobot(KinovaSphericalSweep):
        def __init__(self):
            # Set before subscriptions, which can invoke callbacks immediately.
            self.experiment_complete = False
            self.stop_lock = threading.Lock()
            self.stop_requested = False
            self.measured = None
            self.capture_target = None
            self.capture_error = None
            self.previous_received = None
            super().__init__()

        def finish(self):
            # Only called after all settled captures and the final journal write.
            self.experiment_complete = True

        def stop(self):
            # Watchdog, exception handlers and ROS shutdown share one stop attempt.
            # Mark before the service call: concurrent cleanup must not resend it,
            # including when the controller rejects a session without control.
            with self.stop_lock:
                if self.experiment_complete or self.stop_requested:
                    return
                self.stop_requested = True
            super().stop()

        def on_action(self, notification):
            super().on_action(notification)
            with self.lock:
                if (self.motion_error and notification.handle.identifier == self.expected_action
                        and notification.abort_details == 55):
                    # Kortex SubErrorCodes.CONTROL_MANUAL_STOP; this does not
                    # identify the operator/session that originated the stop.
                    detail = "CONTROL_MANUAL_STOP: controlador reportou parada; origem não identificada"
                    if detail not in self.motion_error:
                        self.motion_error += "; " + detail

        def on_feedback(self, message):
            super().on_feedback(message)
            base = message.base
            xyz = [base.tool_pose_x, base.tool_pose_y, base.tool_pose_z]
            theta = [base.tool_pose_theta_x, base.tool_pose_theta_y, base.tool_pose_theta_z]
            with self.lock:
                if self.motion_error or self.feedback is None:
                    return
                received, positions, velocities = self.feedback
                if not all(math.isfinite(v) for v in xyz + theta):
                    self.motion_error = "Nonfinite measured TCP pose"
                    return
                self.measured = dict(source="base_feedback", received_monotonic_s=received,
                                     joints_deg=list(positions), velocities_deg_s=list(velocities),
                                     tcp=dict(position_m=xyz, theta_xyz_deg=theta,
                                              quaternion_wxyz=from_kortex_euler(theta),
                                              frame=self.robot_name + "/base_link",
                                              rotation_convention="Rz(theta_z) Ry(theta_y) Rx(theta_x)"),
                                     fault_banks=[base.fault_bank_a, base.fault_bank_b])
                if self.capture_target is not None:
                    if (self.previous_received is not None and
                            received - self.previous_received > self.feedback_timeout):
                        self.capture_error = "Feedback gap during capture"
                    if not self.at_rest(positions, velocities, self.capture_target):
                        self.capture_error = "Pose lost stability during capture"
                    if base.fault_bank_a or base.fault_bank_b:
                        self.capture_error = "Robot fault during capture"
                self.previous_received = received

        def snapshot(self):
            self.state()
            with self.lock:
                if self.capture_error:
                    raise RuntimeError(self.capture_error)
                if self.measured is None:
                    raise RuntimeError("Missing measured TCP pose")
                result = self.measured
            if time.monotonic() - result["received_monotonic_s"] > self.feedback_timeout:
                raise RuntimeError("Stale measured TCP pose")
            if any(result["fault_banks"]):
                raise RuntimeError("Robot reports active faults")
            if rospy.is_shutdown():
                raise RuntimeError("ROS execution interrupted")
            return result

        def move_and_settle(self, target, index):
            if rospy.is_shutdown():
                raise RuntimeError("ROS execution interrupted")
            self.send_pose(target, track_j5=index > 0)
            self.hold_pose(target)
            self.snapshot()

        @contextmanager
        def capture_guard(self, target):
            """Latch even transient disturbances; watchdog also detects silence."""
            finished = threading.Event()
            with self.lock:
                self.capture_target = tuple(target)
                self.capture_error = None

            def check():
                state = self.snapshot()
                if not self.at_rest(state["joints_deg"], state["velocities_deg_s"], target):
                    raise RuntimeError("Pose lost stability during capture")

            def watch():
                while not finished.wait(0.02):
                    try:
                        check()
                    except Exception as exc:
                        with self.lock:
                            self.capture_error = str(exc)
                        self.stop()
                        return

            worker = threading.Thread(target=watch, daemon=True)
            try:
                check()
                worker.start()
                yield
                check()
                with self.lock:
                    error = self.capture_error
                    self.capture_target = None
                if error:
                    raise RuntimeError(error)
            finally:
                finished.set()
                if worker.ident is not None:
                    worker.join(timeout=1)
                with self.lock:
                    self.capture_target = None

    return HardwareRobot()
