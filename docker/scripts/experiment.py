#!/usr/bin/env python3
"""Increment 8: one coordinator, imported Trena client, ROS robot adapter."""
import argparse
from datetime import datetime, timezone
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
    def __init__(self, host, port, timeout, use_laser=True):
        self.host, self.port, self.timeout = host, port, timeout
        self.use_laser = use_laser
        self.command = "CAPTURE" if use_laser else "CAPTURE_IMU"
        self.next_connection_at = 0.0

    def preflight(self, record):
        status.query_status(self.host, self.port, self.timeout, record=record)
        self.next_connection_at = time.monotonic() + 0.2
        meta = record["meta"]
        if (record["hello"]["stage"] != "multi_report_capture" or
                meta.get("schema") != "3" or meta.get("n_max") != "1" or
                meta.get("csv_header") != "per_capture" or
                self.command not in meta["commands"].split(",")):
            raise status.ProtocolError(
                f"Experimento requer schema 3 e {self.command} unitário; atualize o firmware da Trena")

    def capture(self, capture_id, record):
        # Give the single-client ESP server time to observe the previous FIN.
        time.sleep(max(0.0, self.next_connection_at - time.monotonic()))
        try:
            status.query_status(self.host, self.port, self.timeout, capture_id, record, use_laser=self.use_laser)
        finally:
            self.next_connection_at = time.monotonic() + 0.2


class SimulatedTrena:
    """Synthetic fixture, no network and no physical measurement."""
    def __init__(self, use_laser=True):
        self.use_laser = use_laser
        self.command = "CAPTURE" if use_laser else "CAPTURE_IMU"

    def preflight(self, record):
        record.update(source="simulated", hello={"boot_id": "0000000000000000"},
                      meta={"schema": "3", "laser_axis": "1/0/0"})

    def capture(self, capture_id, record):
        self.preflight(record)
        record.update(capture_command=self.command, use_laser=self.use_laser)
        laser = "1.234,1,NONE" if self.use_laser else ",0,LASER_DISABLED"
        row = (f"{capture_id},1,{laser},1,0,0,0,90,0,0,1,NONE,0.025,3,1,1,NONE,"
               "1,0,0,0,3,1,1,NONE,12,-3,40,3,1,1,NONE,0")
        done_line = (f"# DONE request_id={capture_id} completion=COMPLETE result=OK n_requested=1 "
                     f"n_rows=1 n_valid=1 n_laser_valid={int(self.use_laser)} n_imu_valid=1 n_grv_valid=1 n_mag_valid=1 "
                     f"imu_reset=0 code=NONE state=IDLE laser_requested={int(self.use_laser)}")
        sample = status.parse_sample(status.CAPTURE_HEADER, row, capture_id, self.use_laser)
        record["capture"] = dict(sample=sample, done=status.parse_done(done_line, sample, capture_id, self.use_laser),
                                 complete=True, imu_reset=False,
                                 orientation_session={"boot_id": "0000000000000000", "imu_epoch": 0})


def run_experiment(robot, trena, journal, metadata, first_capture_id=1, captures_per_pose=1):
    """Fixed acquisition attempts; motion, stability and storage failures remain fatal."""
    if type(captures_per_pose) is not int or not 1 <= captures_per_pose <= 20:
        raise ValueError("captures_per_pose must be between 1 and 20")
    if not 1 <= first_capture_id <= 0xFFFFFFFF - len(robot.poses) * captures_per_pose + 1:
        raise ValueError("Capture IDs exceed uint32 range")
    experiment_id = uuid.uuid4().hex
    started = time.monotonic()
    saved_poses = 0
    failed_captures = 0
    poses_without_valid_capture = []
    poses_with_failures = []
    orientation_session = None

    def write(kind, **data):
        journal.write(dict(record_type=kind, experiment_schema=2, experiment_id=experiment_id,
                           pc_utc=status.utc_now(), **data))

    def totals():
        return dict(saved_poses=saved_poses, failed_captures=failed_captures,
                    poses_without_valid_capture=poses_without_valid_capture,
                    poses_with_failures=poses_with_failures,
                    product_improvement_required=bool(failed_captures))

    try:
        write("session", metadata=metadata, captures_per_pose=captures_per_pose,
              use_laser=trena.use_laser, capture_command=trena.command,
              targets_joints_deg=robot.poses)
        preflight = {}
        try:
            trena.preflight(preflight)
        finally:
            write("preflight", trena=preflight)
        robot.prepare()
        for index, target in enumerate(robot.poses):
            pose_id = f"pose_{index:03d}"
            accepted_count = 0
            sensor_valid_counts = dict(laser=0, imu=0, grv=0, mag=0)
            for attempt in range(captures_per_pose):
                capture_id = first_capture_id + index * captures_per_pose + attempt
                row = dict(pose_id=pose_id, pose_index=index, capture_id=capture_id,
                           use_laser=trena.use_laser, capture_command=trena.command,
                           attempt_index=attempt + 1, attempts_requested=captures_per_pose,
                           target_joints_deg=list(target), pc_started_utc=status.utc_now(),
                           phase="move" if attempt == 0 else "capture",
                           exchange_complete=False, accepted=False, trena={})
                began = time.monotonic()
                try:
                    if attempt == 0:
                        print(f"Pose {index + 1}/{len(robot.poses)}: movendo e aguardando repouso", flush=True)
                        robot.move_and_settle(target, index)
                    row["phase"] = "capture"
                    with robot.capture_guard(target):
                        row["robot_before"] = robot.snapshot()
                        print(f"Pose {index + 1}/{len(robot.poses)}; tentativa "
                              f"{attempt + 1}/{captures_per_pose}; {trena.command} {capture_id} 1", flush=True)
                        row["capture_started_utc"] = status.utc_now()
                        row["capture_started_monotonic_s"] = time.monotonic()
                        try:
                            trena.capture(capture_id, row["trena"])
                        except (OSError, status.ProtocolError) as exc:
                            row["error"] = str(exc)
                            row["failure_kind"] = "transport_or_protocol"
                        finally:
                            row["capture_finished_utc"] = status.utc_now()
                            row["capture_finished_monotonic_s"] = time.monotonic()
                            try:
                                row["robot_after"] = robot.snapshot()
                            except Exception as exc:
                                row["robot_after_error"] = str(exc)
                        if row.get("robot_after_error"):
                            raise RuntimeError(row["robot_after_error"])
                    # Guard errors propagate: continuing would invalidate pose association.
                    row["phase"] = "validate"
                    capture = row["trena"].get("capture", {})
                    row["exchange_complete"] = capture.get("complete", False)
                    if not row.get("error"):
                        if not row["exchange_complete"]:
                            row.update(error="Capture ended without validated DONE",
                                       failure_kind="transport_or_protocol")
                        else:
                            try:
                                row["derived"] = analyze_capture(row["trena"])
                            except (ValueError, KeyError) as exc:
                                row.update(error=str(exc), failure_kind="data")
                            current_session = capture["orientation_session"]
                            row["orientation_session_changed"] = (
                                (orientation_session is None and current_session["boot_id"] != preflight["hello"]["boot_id"]) or
                                (orientation_session is not None and current_session != orientation_session))
                            orientation_session = current_session
                            if capture["imu_reset"] or row["orientation_session_changed"]:
                                row.update(error="IMU reset/orientation session changed",
                                           failure_kind="orientation_session")
                            elif capture["done"]["result"] != "OK":
                                row.update(error="Capture completed with sensor result " + capture["done"]["result"],
                                           failure_kind="sensor")
                            for sensor in sensor_valid_counts:
                                sensor_valid_counts[sensor] += int(capture["sample"][sensor + "_valid"])
                    before = row["robot_before"].get("tcp")
                    after = row.get("robot_after", {}).get("tcp")
                    if before and after:
                        row.setdefault("derived", {})["robot_rotation_drift_deg"] = rotation_error_deg(
                            before["quaternion_wxyz"], after["quaternion_wxyz"])
                    row["accepted"] = not bool(row.get("error"))
                    row["phase"] = "saved"
                except (Exception, KeyboardInterrupt) as exc:
                    row["error"] = str(exc) or type(exc).__name__
                    row["fatal"] = True
                    robot.stop()
                    raise
                finally:
                    row["exchange_complete"] = row["trena"].get("capture", {}).get("complete", False)
                    row["pc_finished_utc"] = status.utc_now()
                    row["elapsed_ms"] = (time.monotonic() - began) * 1000
                    write("pose", **row)
                if row["accepted"]:
                    accepted_count += 1
                else:
                    failed_captures += 1
                    print(f"AVISO {pose_id}, captura {capture_id}: {row['error']}; "
                          "falha registrada, seguindo tentativas planejadas", flush=True)
            write("pose_summary", pose_id=pose_id, pose_index=index,
                  use_laser=trena.use_laser, capture_command=trena.command,
                  captures_requested=captures_per_pose, captures_recorded=captures_per_pose,
                  accepted_captures=accepted_count, failed_captures=captures_per_pose-accepted_count,
                  sensor_valid_counts=sensor_valid_counts,
                  orientation_sessions_must_be_analyzed_separately=True,
                  product_improvement_required=accepted_count < captures_per_pose,
                  outcome="ok" if accepted_count == captures_per_pose else
                          "partial" if accepted_count else "no_valid_capture")
            if accepted_count < captures_per_pose:
                poses_with_failures.append(pose_id)
            if accepted_count == 0:
                poses_without_valid_capture.append(pose_id)
            saved_poses += 1
            print(f"Pose {index + 1}/{len(robot.poses)} concluída: "
                  f"{accepted_count}/{captures_per_pose} capturas aceitas", flush=True)
        write("end", outcome="complete_with_failures" if failed_captures else "complete",
              **totals(), elapsed_ms=(time.monotonic() - started) * 1000)
        robot.finish()
        print(f"Experimento concluído: {saved_poses} poses; {failed_captures} capturas com falha; "
              f"{len(poses_without_valid_capture)} poses sem captura válida. "
              "Consulte o registro end do JSONL.", flush=True)
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        robot.stop()
        try:
            write("end", outcome="interrupted" if isinstance(exc, KeyboardInterrupt) else "failed",
                  **totals(), error=str(exc) or type(exc).__name__,
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
    parser.add_argument("--use-laser", type=status.boolean, default=True, metavar="true|false",
                        help="false captura apenas RV, GRV e campo magnético (padrão: true)")
    parser.add_argument("--host", type=status.ipv4, default=os.getenv("LAB_HOST") or None)
    parser.add_argument("--port", type=status.port_number, default=os.getenv("LAB_PORT", "5000"))
    parser.add_argument("--timeout", type=status.timeout_seconds, default=os.getenv("LAB_TIMEOUT", "5"))
    parser.add_argument("--hold-sec", type=status.timeout_seconds, default=1.0)
    parser.add_argument("--first-tilt-deg", type=float, default=120.0)
    parser.add_argument("--captures-per-pose", type=int, default=5)
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
    if not 1 <= args.captures_per_pose <= 20:
        parser.error("captures-per-pose must be between 1 and 20")
    if not 1 <= args.first_capture_id <= 0xFFFFFFFF - len(robot.poses) * args.captures_per_pose + 1:
        parser.error("Capture IDs for the whole sequence must fit uint32 and start at 1 or above")
    created_utc = datetime.now(timezone.utc)
    args.log = args.log or args.output_dir / created_utc.strftime("experiment-%Y-%m-%dT%H-%M-%S.%fZ.jsonl")
    # Verify destination before initializing anything capable of moving hardware.
    journal = None
    try:
        journal = Journal(args.log)
        metadata = dict(configuration={k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                        created_utc=created_utc.isoformat(timespec="microseconds"),
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
        trena = (TcpTrena(args.host, args.port, args.timeout, args.use_laser)
                 if args.trena_mode == "tcp" else SimulatedTrena(args.use_laser))
        print(f"Registro: {args.log}", flush=True)
        return run_experiment(robot, trena, journal, metadata, args.first_capture_id, args.captures_per_pose)
    except (Exception, KeyboardInterrupt) as exc:
        print(f"ERRO ao iniciar experimento: {exc}", file=sys.stderr)
        return 130 if isinstance(exc, KeyboardInterrupt) else 1
    finally:
        robot.stop()
        if journal is not None:
            journal.close()


if __name__ == "__main__":
    sys.exit(main())
