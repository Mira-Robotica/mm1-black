"""Persistent IMU monitor. Firmware owns the geometry; the PC never recalculates it."""
import argparse
from collections import deque
import csv
from datetime import datetime, timezone
import ipaddress
import json
import math
import os
from pathlib import Path
import re
import select
import signal
import socket
import subprocess
import sys
import time
import uuid

from status import ProtocolError, fields, parse_hello

VALUES = (
    "mag_raw_x_adc,mag_raw_y_adc,mag_raw_z_adc,mag_uncal_x_uT,mag_uncal_y_uT,mag_uncal_z_uT,"
    "mag_cal_x_uT,mag_cal_y_uT,mag_cal_z_uT,rv_azimuth_deg,rv_inclination_deg,rv_roll_deg,"
    "grv_azimuth_deg,grv_inclination_deg,grv_roll_deg,mag_cal_azimuth_deg,rv_status,grv_status,rv_accuracy_rad"
).split(",")
ENVELOPE = "session_id,seq,device_ms,updated_mask,valid_mask,value_updated_mask,raw_age_ms,uncal_age_ms,cal_age_ms,rv_age_ms,grv_age_ms".split(",")
HEADER = ",".join(ENVELOPE + VALUES)
MAX_LINE = 2048


class Lines:
    """Bounded framing; oversized/non-ASCII rows are discarded through their LF."""
    def __init__(self, sock):
        self.sock = sock
        self.buffer = bytearray()
        self.discarding = False
        self.lines = deque()

    def read(self, timeout):
        deadline = time.monotonic() + timeout
        while not self.lines:
            left = deadline - time.monotonic()
            if left <= 0 or not select.select([self.sock], [], [], left)[0]:
                raise TimeoutError("Timeout esperando linha TCP")
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ProtocolError("Conexão encerrada (EOF)")
            for b in chunk:
                if b == 10:
                    raw = bytes(self.buffer).removesuffix(b"\r")
                    self.lines.append("" if self.discarding or len(raw) > MAX_LINE or any(c < 32 or c > 126 for c in raw)
                                      else raw.decode("ascii"))
                    self.buffer.clear()
                    self.discarding = False
                elif not self.discarding:
                    self.buffer.append(b)
                    if len(self.buffer) > MAX_LINE + 1:
                        self.buffer.clear()
                        self.discarding = True
        return self.lines.popleft()


def uint(text, maximum=0xFFFFFFFF):
    if not re.fullmatch(r"[0-9]+", text) or int(text) > maximum:
        raise ValueError("inteiro inválido")
    return int(text)


class Samples:
    def __init__(self, session):
        self.session = session
        self.seq = 0
        self.retained = [None] * 19
        self.ages = [None] * 5

    def accept(self, line):
        # Structural corruption discards the whole row; a bad scientific group
        # affects only that group. Finite angles are already computed on device.
        cells = line.split(",")
        if len(line) > MAX_LINE or len(cells) != len(ENVELOPE) + 19:
            return None
        try:
            sid, seq, device, groups, valid, new = (uint(cells[i]) for i in range(6))
            if sid != self.session or seq <= self.seq or groups > 31 or valid > 0x7FFFF or new & ~valid:
                return None
            ages = [None if not c else uint(c) for c in cells[6:11]]
        except ValueError:
            return None
        fresh = [None] * 19
        parsed = [None] * 19
        for i, c in enumerate(cells[11:]):
            try:
                v = float(c)
                if not math.isfinite(v) or not valid & (1 << i):
                    continue
                if i in (16, 17) and (v != int(v) or not 0 <= v <= 3):
                    continue
                if i == 18 and v < 0:
                    continue
                if i < 3 and (v != int(v) or not -32768 <= v <= 32767):
                    continue
                if i in (9, 12, 15) and not 0 <= v < 360:
                    continue
                if i in (10, 13) and not -90.001 <= v <= 90.001:
                    continue
                if i in (11, 14) and not -180.001 <= v <= 180.001:
                    continue
                parsed[i] = v
            except ValueError:
                pass
        # Magnetic xyz are one validated measurement, angles are independent.
        for start in (0, 3, 6):
            if any(parsed[i] is None for i in range(start, start + 3)):
                parsed[start:start + 3] = [None] * 3
        for i, v in enumerate(parsed):
            group = i // 3 if i < 9 else 3 if i < 12 else 4 if i < 15 else 2 if i == 15 else 4 if i == 17 else 3
            if v is not None and new & (1 << i) and groups & (1 << group) and ages[group] is not None:
                fresh[i] = v
                self.retained[i] = v
        self.seq = seq
        self.ages = ages
        return fresh


def parse_stream_meta(line, session):
    meta = fields(line, "# STREAM_META", (
        "schema", "firmware", "session_id", "epoch", "rate_hz", "part", "build", "fw", "cal_mask", "cal_source",
        "dcd_autosave", "reports", "config_order", "config_fields", "previous", "requested", "confirmed",
        "quaternion", "front", "rv_world", "rv_reference", "grv_reference", "inclination", "angular_correction",
        "raw_mapping", "raw_azimuth", "tilt_source", "max_pair_ms", "norm_tolerance", "singularity_epsilon",
        "cal_horizontal_min_uT", "raw_horizontal_min_adc", "heartbeat_ms"))
    expected = {"schema": "2", "session_id": str(session), "reports": "22,15,3,5,8",
                "config_order": "raw,uncal,cal,rv,grv", "quaternion": "wxyz", "front": "+X", "rv_world": "ENU",
                "rv_reference": "magnetic", "grv_reference": "arbitrary", "inclination": "beam_production_pitch",
                "angular_correction": "none", "raw_mapping": "unknown", "raw_azimuth": "unavailable", "tilt_source": "GRV", "max_pair_ms": "200",
                "norm_tolerance": "0.02", "singularity_epsilon": "1e-5", "cal_horizontal_min_uT": "0.01",
                "raw_horizontal_min_adc": "unknown", "heartbeat_ms": "15000",
                "cal_source": "GET_CAL", "dcd_autosave": "disabled_sent_unacknowledged",
                "config_fields": "interval_us/batch_us/flags/change/specific"}
    if any(meta[k] != v for k, v in expected.items()):
        raise ProtocolError("STREAM_META: schema ou convenções incompatíveis")
    if meta.get("mag_azimuth_method", "atan2_minus_left_forward_v1") not in {
            "atan2_minus_left_forward_v1", "atan2_left_forward_v2"}:
        raise ProtocolError("STREAM_META: método de azimute magnético incompatível")
    try:
        for k in ("epoch", "part", "build", "cal_mask", "max_pair_ms", "heartbeat_ms"):
            uint(meta[k])
        rate = float(meta["rate_hz"])
        if not math.isfinite(rate) or not 0.2 <= rate <= 5:
            raise ValueError()
        for key in ("previous", "requested", "confirmed"):
            configs = [entry.split("/") for entry in meta[key].split(";")]
            if len(configs) != 5 or any(len(c) != 5 for c in configs):
                raise ValueError()
            for config in configs:
                for value in config:
                    uint(value)
                if key != "previous" and not int(config[0]):
                    raise ValueError()
    except ValueError as exc:
        raise ProtocolError("STREAM_META: configuração inválida") from exc
    return meta


def magnetic_method(meta):
    method = meta.get("mag_azimuth_method", "atan2_minus_left_forward_v1")
    lateral = "dot(m,l)" if method == "atan2_left_forward_v2" else "-dot(m,l)"
    return "up=normalize(R_grv^T*Z); f=normalize(X-dot(X,up)*up); l=cross(up,f); atan2(" + lateral + ",dot(m,f))"


def client_commit():
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=Path(__file__).resolve().parent,
                                       stderr=subprocess.DEVNULL, timeout=1, text=True).strip()
    except (OSError, subprocess.SubprocessError):
        return None


class Recording:
    def __init__(self, args, metadata):
        self.file = None
        self.directory = None
        self.last_flush = time.monotonic()
        if not args.save:
            return
        root = Path(args.output_dir)
        root.mkdir(parents=True, exist_ok=True)
        self.directory = root / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ_") + uuid.uuid4().hex)
        self.directory.mkdir()
        # Exclusive creation, exactly once; this file is never touched at shutdown.
        with (self.directory / "metadata.json").open("x", encoding="utf-8") as out:
            json.dump(metadata, out, indent=2, ensure_ascii=False, allow_nan=False)
            out.write("\n")
        try:
            self.file = (self.directory / "samples.csv").open("x", newline="", encoding="utf-8")
            self.writer = csv.writer(self.file)
            self.writer.writerow(["elapsed_s"] + VALUES)
            self.file.flush()
        except BaseException:
            self.close()
            raise

    def write(self, elapsed, fresh):
        if self.file:
            self.writer.writerow([f"{elapsed:.6f}"] + ["" if v is None else v for v in fresh])
            if time.monotonic() - self.last_flush >= 1:
                self.file.flush()
                self.last_flush = time.monotonic()

    def close(self):
        if self.file:
            f, self.file = self.file, None
            f.close()


class Panel:
    def __init__(self, output, args):
        self.output, self.args = output, args
        self.tty = output.isatty()
        self.started = False

    def show(self, sample):
        def val(i):
            v = sample.retained[i]
            return "—" if v is None else f"{v:.3f}"
        rows = [f"MM1 IMU | {self.args.rate_hz:g} Hz | gravação: {'sim' if self.args.save else 'não'} | Ctrl+C para encerrar",
                "Campo                  X           Y           Z       Unidade"]
        for g, name in enumerate(("Raw", "Uncalibrated", "Calibrated")):
            rows.append(f"{name:20} {val(3*g):>11} {val(3*g+1):>11} {val(3*g+2):>11}  {'ADC' if g == 0 else 'µT'}")
        rows += ["", "Orientação           Azimute  Inclinação (pitch)        Roll  graus",
                 f"RV (magnético)       {val(9):>11} {val(10):>17} {val(11):>11}  qualidade={val(16)}/3 | precisão={val(18)} rad",
                 f"GRV (arbitrário)      {val(12):>11} {val(13):>17} {val(14):>11}  qualidade={val(17)}/3",
                 f"Azimute Calibrated: {val(15)}° | qualidade: 0=não confiável, 1=baixa, 2=média, 3=alta",
                 "Idade Raw/Uncal/Cal/RV/GRV (ms): " + "/".join("—" if a is None else str(a) for a in sample.ages)]
        if self.tty:
            if not self.started:
                self.output.write("\x1b[?25l")
            else:
                self.output.write(f"\x1b[{len(rows)}A")
            self.output.write("".join("\r\x1b[2K" + row + "\n" for row in rows))
        else:
            self.output.write("\n".join(rows) + "\n\n")
        self.started = True
        self.output.flush()

    def close(self):
        if self.tty and self.started:
            self.output.write("\x1b[?25h\x1b[0m")
            self.output.flush()


def run(args, stop, output=sys.stdout):
    sock = None
    session = None
    recording = None
    panel = Panel(output, args)
    cleanup_error = None
    primary_error = None
    try:
        sock = socket.create_connection((args.host, args.port), args.timeout)
        reader = Lines(sock)
        def send(command):
            sock.settimeout(args.timeout)
            sock.sendall((command + "\n").encode("ascii"))
        hello = parse_hello(reader.read(args.timeout))
        meta = fields(reader.read(args.timeout), "# META", ("commands",))
        if meta.get("stream_imu") != "1" or meta.get("stream_schema") != "2":
            raise ProtocolError("Firmware sem capability stream_imu=1/stream_schema=2; atualize mm1_p4_lab")
        send("STATUS")
        status = fields(reader.read(args.timeout), "# OK STATUS", ())
        if stop():
            return
        send(f"STREAM_IMU START rate_hz={args.rate_hz:g}")
        ack = fields(reader.read(args.timeout), "# OK STREAM_IMU START", ("session_id", "state"))
        session = uint(ack["session_id"])
        if not session or ack["state"] != "CONFIGURING":
            raise ProtocolError("START inválido")
        ping = time.monotonic()
        def receive(deadline):
            nonlocal ping
            while time.monotonic() < deadline:
                if stop():
                    raise InterruptedError()
                if time.monotonic() - ping >= 5:
                    send(f"STREAM_IMU PING session_id={session}")
                    ping = time.monotonic()
                try:
                    line = reader.read(min(0.2, max(0.001, deadline - time.monotonic())))
                except TimeoutError:
                    continue
                if line.startswith("# ERR"):
                    raise ProtocolError(line)
                if line.startswith("# OK STREAM_IMU PING"):
                    pong = fields(line, "# OK STREAM_IMU PING", ("session_id",))
                    if pong["session_id"] != str(session):
                        raise ProtocolError("PING de outra sessão")
                    continue
                return line
            raise TimeoutError("Timeout sem resposta/dados da sessão IMU")
        ready = fields(receive(time.monotonic() + 24 + args.timeout), "# OK STREAM_IMU READY", ("session_id",))
        if ready["session_id"] != str(session):
            raise ProtocolError("READY de outra sessão")
        stream_meta = parse_stream_meta(receive(time.monotonic() + args.timeout), session)
        if abs(float(stream_meta["rate_hz"]) - args.rate_hz) > 1e-5:
            raise ProtocolError("STREAM_META: taxa diferente da solicitada")
        if receive(time.monotonic() + args.timeout) != HEADER:
            raise ProtocolError("Cabeçalho incompatível com stream_schema=2")
        started = time.monotonic()
        metadata = {
            "session_id": session, "started_utc": datetime.now(timezone.utc).isoformat(),
            "client": {"version": "2", "commit": client_commit(), "configuration": vars(args)},
            "notes": args.notes, "hello": hello, "meta": meta, "status_before": status, "stream": stream_meta,
            "reports": [{"id": i, "name": n, "unit": u} for i, n, u in (
                (22, "Raw Magnetometer", "ADC"), (15, "Magnetic Field Uncalibrated", "uT"),
                (3, "Magnetic Field Calibrated", "uT"), (5, "Rotation Vector", "quaternion wxyz"),
                (8, "Game Rotation Vector", "quaternion wxyz"))],
            "retention": "terminal: last valid per field; CSV: only valid updates in each accepted snapshot, otherwise empty",
            "time": "elapsed_s: PC monotonic since metadata/start of recording; asynchronous reports, not synchronized acquisition",
            "magnetic_source": "calibrated",
            "magnetic_method": magnetic_method(stream_meta),
            "raw_limitation": "Native ADC values retained. Axis/scale mapping unverified; Raw azimuth unavailable.",
            "uncalibrated": "Soft iron corrected by sensor; hard iron bias is not subtracted by monitor",
            "restoration_limit": "Configuration only; does not roll back internal fusion/dynamic calibration evolution",
        }
        recording = Recording(args, metadata)
        samples = Samples(session)
        panel.show(samples)
        data_deadline = time.monotonic() + args.timeout + 1 / args.rate_hz
        while not stop():
            line = receive(data_deadline)
            fresh = samples.accept(line)
            if fresh is None:
                continue
            data_deadline = time.monotonic() + args.timeout + 1 / args.rate_hz
            recording.write(time.monotonic() - started, fresh)
            panel.show(samples)
    except InterruptedError:
        pass
    except BaseException as exc:
        primary_error = exc
        raise
    finally:
        # SIGINT/SIGTERM merely set stop; they cannot interrupt this bounded cleanup.
        if sock is not None and session is not None:
            try:
                sock.settimeout(args.timeout)
                sock.sendall(f"STREAM_IMU STOP session_id={session}\n".encode("ascii"))
                deadline = time.monotonic() + 14 + args.timeout
                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError("STOP: restauração sem ACK")
                    line = reader.read(remaining)
                    if line.startswith(("# OK STREAM_IMU STOP", "# ERR STREAM_IMU STOP")):
                        ack = fields(line, "# OK STREAM_IMU STOP" if line.startswith("# OK") else "# ERR STREAM_IMU STOP",
                                     ("session_id", "restored"))
                        if ack["session_id"] != str(session) or ack["restored"] != "1":
                            raise ProtocolError("STOP: restauração não confirmada: " + line)
                        break
            except (OSError, ProtocolError) as exc:
                cleanup_error = exc
        try:
            if recording:
                recording.close()
        except OSError as exc:
            cleanup_error = cleanup_error or exc
        finally:
            if sock:
                sock.close()
            panel.close()
        if cleanup_error:
            if primary_error is not None:
                raise ProtocolError(f"{primary_error}; encerramento: {cleanup_error}") from primary_error
            raise cleanup_error


def arguments(argv=None):
    p = argparse.ArgumentParser(description="Monitor contínuo da IMU por TCP (sem ROS/laser)")
    p.add_argument("--host", default=os.getenv("LAB_HOST", ""))
    p.add_argument("--port", type=int, default=os.getenv("LAB_PORT", "5000"))
    p.add_argument("--timeout", type=float, default=os.getenv("LAB_TIMEOUT", "5"))
    p.add_argument("--rate-hz", type=float, default=2)
    p.add_argument("--save", choices=("true", "false"), default="false")
    p.add_argument("--output-dir", default="/data")
    p.add_argument("--notes", default="")
    args = p.parse_args(argv)
    try:
        ipaddress.IPv4Address(args.host)
        if not 1 <= args.port <= 65535 or not math.isfinite(args.timeout) or args.timeout <= 0:
            raise ValueError()
        if not math.isfinite(args.rate_hz) or not 0.2 <= args.rate_hz <= 5:
            raise ValueError()
    except ValueError:
        p.error("Use IPv4 em --host/LAB_HOST, porta 1..65535, timeout positivo e rate-hz 0.2..5")
    args.save = args.save == "true"
    return args


def main(argv=None):
    args = arguments(argv)
    stopping = [False]
    def stop_signal(signum, frame):
        stopping[0] = True
    previous = {sig: signal.signal(sig, stop_signal) for sig in (signal.SIGINT, signal.SIGTERM)}
    try:
        run(args, lambda: stopping[0])
        return 0
    except (OSError, ProtocolError, ValueError) as exc:
        print(f"Monitor IMU: {exc}", file=sys.stderr)
        return 1
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)


if __name__ == "__main__":
    sys.exit(main())
