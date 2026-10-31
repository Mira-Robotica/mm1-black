#!/usr/bin/env python3
"""Increment 8: one coordinator, imported Trena client, ROS robot adapter."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

import status
from experiment_orientation import analyze_capture, rotation_error_deg
from experiment_robot import ROOT, SWEEP_SCRIPTS, SimulatedRobot


class Journal:
    """Exclusive new file; each record must reach disk before the next action."""
    def __init__(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.file = path.open("x", encoding="utf-8")

    def write(self, record):
        self.file.write(json.dumps(record, ensure_ascii=False, allow_nan=False) + "\n")
        self.file.flush()
        os.fsync(self.file.fileno())

    def close(self):
        self.file.close()


def provenance():
    def git(path, *args):
        try:
            return subprocess.check_output(
                ["git", "-c", "safe.directory=*", "-C", str(path), *args],
                stderr=subprocess.DEVNULL, text=True, timeout=5).strip()
        except (OSError, subprocess.SubprocessError):
            return None

    submodule = ROOT / "kinova-geomagic-noetic-demo"
    files = list(Path(__file__).parent.glob("experiment*.py"))
    files += [Path(status.__file__), SWEEP_SCRIPTS / "kinova_spherical_sweep.py",
              SWEEP_SCRIPTS / "spherical_sweep_sequence.py"]
    return dict(repository_revision=git(ROOT, "rev-parse", "HEAD"),
                submodule_path="kinova-geomagic-noetic-demo",
                submodule_revision=git(submodule, "rev-parse", "HEAD"),
                submodule_status=git(submodule, "status", "--porcelain"),
                source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in files})


class TcpTrena:
    def __init__(self, host, port, timeout):
        self.host, self.port, self.timeout = host, port, timeout

    def preflight(self, record):
        status.query_status(self.host, self.port, self.timeout, record=record)
        meta = record["meta"]
        if (record["hello"]["stage"] != "multi_report_capture" or
                meta.get("schema") != "3" or meta.get("n_max") != "1" or
                meta.get("csv_header") != "per_capture" or
                "CAPTURE" not in meta["commands"].split(",")):
            raise status.ProtocolError("Experiment requires schema 3, single CAPTURE")

    def capture(self, capture_id, record):
        status.query_status(self.host, self.port, self.timeout, capture_id, record)


class SimulatedTrena:
    """Synthetic fixture, no network and no physical measurement."""
    def preflight(self, record):
        record.update(source="simulated", hello={"boot_id": "0000000000000000"},
                      meta={"schema": "3", "laser_axis": "1/0/0"})

    def capture(self, capture_id, record):
        self.preflight(record)
        row = (f"{capture_id},1,1.234,1,NONE,1,0,0,0,90,0,0,1,NONE,0.025,3,1,1,NONE,"
               "1,0,0,0,3,1,1,NONE,12,-3,40,3,1,1,NONE,0")
        done_line = (f"# DONE request_id={capture_id} completion=COMPLETE result=OK n_requested=1 "
                     "n_rows=1 n_valid=1 n_laser_valid=1 n_imu_valid=1 n_grv_valid=1 n_mag_valid=1 "
                     "imu_reset=0 code=NONE state=IDLE")
        sample = status.parse_sample(status.CAPTURE_HEADER, row, capture_id)
        record["capture"] = dict(sample=sample, done=status.parse_done(done_line, sample, capture_id),
                                 complete=True, imu_reset=False,
                                 orientation_session={"boot_id": "0000000000000000", "imu_epoch": 0})


def run_experiment(robot, trena, journal, metadata, first_capture_id=1):
    """All failures stop; never replay a request or move after an unsaved result."""
    experiment_id = uuid.uuid4().hex
    started = time.monotonic()
    saved_poses = 0
    orientation_session = None

    def write(kind, **data):
        journal.write(dict(record_type=kind, experiment_schema=1, experiment_id=experiment_id,
                           pc_utc=status.utc_now(), **data))

    try:
        write("session", metadata=metadata, targets_joints_deg=robot.poses)
        preflight = {}
        try:
            trena.preflight(preflight)
        finally:
            write("preflight", trena=preflight)
        robot.prepare()
        for index, target in enumerate(robot.poses):
            capture_id = first_capture_id + index
            row = dict(pose_id=f"pose_{index:03d}", pose_index=index, capture_id=capture_id,
                       target_joints_deg=list(target), pc_started_utc=status.utc_now(),
                       phase="move", exchange_complete=False, accepted=False, trena={})
            began = time.monotonic()
            try:
                print(f"Pose {index + 1}/{len(robot.poses)}; CAPTURE {capture_id} 1", flush=True)
                robot.move_and_settle(target, index)
                row["phase"] = "capture"
                with robot.capture_guard(target):
                    row["robot_before"] = robot.snapshot()
                    row["capture_started_utc"] = status.utc_now()
                    row["capture_started_monotonic_s"] = time.monotonic()
                    try:
                        trena.capture(capture_id, row["trena"])
                    finally:
                        row["capture_finished_utc"] = status.utc_now()
                        row["capture_finished_monotonic_s"] = time.monotonic()
                        # Preserve the protocol error even if feedback also disappears.
                        try:
                            row["robot_after"] = robot.snapshot()
                        except Exception as exc:
                            row["robot_after_error"] = str(exc)
                row["phase"] = "validate"
                capture = row["trena"]["capture"]
                row["exchange_complete"] = capture["complete"]
                if not capture["complete"]:
                    raise RuntimeError("Capture ended without validated DONE")
                row["derived"] = analyze_capture(row["trena"])
                before = row["robot_before"].get("tcp")
                after = row.get("robot_after", {}).get("tcp")
                if before and after:
                    row["derived"]["robot_rotation_drift_deg"] = rotation_error_deg(
                        before["quaternion_wxyz"], after["quaternion_wxyz"])
                current_session = capture["orientation_session"]
                row["orientation_session_changed"] = (
                    current_session["boot_id"] != preflight["hello"]["boot_id"] or
                    (orientation_session is not None and current_session != orientation_session))
                orientation_session = current_session
                if row.get("robot_after_error"):
                    raise RuntimeError(row["robot_after_error"])
                if capture["imu_reset"] or row["orientation_session_changed"]:
                    raise RuntimeError("IMU reset/orientation session changed; inspect before a new experiment")
                if capture["done"]["result"] != "OK":
                    raise RuntimeError("Capture completed with sensor result " + capture["done"]["result"])
                row["accepted"] = True
                row["phase"] = "saved"
            except (Exception, KeyboardInterrupt) as exc:
                row["error"] = str(exc) or type(exc).__name__
                raise
            finally:
                row["exchange_complete"] = row["trena"].get("capture", {}).get("complete", False)
                row["pc_finished_utc"] = status.utc_now()
                row["elapsed_ms"] = (time.monotonic() - began) * 1000
                write("pose", **row)
            saved_poses += 1
        write("end", outcome="complete", saved_poses=saved_poses,
              elapsed_ms=(time.monotonic() - started) * 1000)
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        # Stop before trying to persist errors (disk itself may have failed).
        robot.stop()
        try:
            write("end", outcome="interrupted" if isinstance(exc, KeyboardInterrupt) else "failed",
                  saved_poses=saved_poses, error=str(exc) or type(exc).__name__,
                  elapsed_ms=(time.monotonic() - started) * 1000)
        except OSError as log_error:
            print(f"ERRO ao gravar: {log_error}", file=sys.stderr)
        print(f"Experimento interrompido: {exc or type(exc).__name__}", file=sys.stderr)
        return 130 if isinstance(exc, KeyboardInterrupt) else 1
    finally:
        robot.stop()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--execution-mode", choices=("simulated", "rviz", "hardware"), default="simulated")
    parser.add_argument("--trena-mode", choices=("simulated", "tcp"), default=None)
    parser.add_argument("--host", type=status.ipv4, default=os.getenv("LAB_HOST") or None)
    parser.add_argument("--port", type=status.port_number, default=os.getenv("LAB_PORT", "5000"))
    parser.add_argument("--timeout", type=status.timeout_seconds, default=os.getenv("LAB_TIMEOUT", "5"))
    parser.add_argument("--hold-sec", type=status.timeout_seconds, default=1.0)
    parser.add_argument("--first-tilt-deg", type=float, default=120.0)
    parser.add_argument("--first-capture-id", type=int, default=1)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docker/data")
    args = parser.parse_args(argv)
    args.trena_mode = args.trena_mode or ("tcp" if args.execution_mode == "hardware" else "simulated")
    if args.trena_mode == "tcp" and not args.host:
        parser.error("TCP requires --host or LAB_HOST")
    if args.execution_mode == "hardware" and args.trena_mode != "tcp":
        parser.error("Hardware experiment requires TCP Trena capture")
    try:
        robot = SimulatedRobot(args.first_tilt_deg, args.hold_sec)
    except ValueError as exc:
        parser.error(str(exc))
    if not 1 <= args.first_capture_id <= 0xFFFFFFFF - len(robot.poses) + 1:
        parser.error("Capture IDs for the whole sequence must fit uint32 and start at 1 or above")
    args.log = args.log or args.output_dir / ("experiment-" + uuid.uuid4().hex + ".jsonl")
    # Verify destination before initializing anything capable of moving hardware.
    journal = None
    try:
        journal = Journal(args.log)
        metadata = dict(configuration={k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                        provenance=provenance(), capture_n=1,
                        comparison_status="FRAME_ALIGNMENT_NOT_CONFIGURED")
        if args.execution_mode != "simulated":
            import rospy
            from experiment_robot import RvizRobot, hardware_robot
            rospy.init_node("mm1_experiment")
            rospy.set_param("~hold_sec", args.hold_sec)
            rospy.set_param("~first_tilt_deg", args.first_tilt_deg)
            robot = hardware_robot() if args.execution_mode == "hardware" else RvizRobot()
            metadata["robot_parameters"] = rospy.get_param("~", {})
            if args.execution_mode == "hardware":
                metadata["motion_parameters"] = {key: getattr(robot, key) for key in (
                    "hold_sec", "action_timeout", "settle_timeout", "feedback_timeout",
                    "position_tolerance", "velocity_tolerance", "max_duration")}
        trena = TcpTrena(args.host, args.port, args.timeout) if args.trena_mode == "tcp" else SimulatedTrena()
        print(f"Registro: {args.log}", flush=True)
        return run_experiment(robot, trena, journal, metadata, args.first_capture_id)
    except (Exception, KeyboardInterrupt) as exc:
        print(f"ERRO ao iniciar experimento: {exc}", file=sys.stderr)
        return 130 if isinstance(exc, KeyboardInterrupt) else 1
    finally:
        robot.stop()
        if journal is not None:
            journal.close()


if __name__ == "__main__":
    sys.exit(main())
