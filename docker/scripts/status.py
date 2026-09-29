"""Bounded STATUS and optional single-sample CAPTURE exchanges with MM1LAB."""

import argparse
import csv
from datetime import datetime, timezone
import ipaddress
import json
import math
import os
from pathlib import Path
import re
import socket
import sys
import time


MAX_LINE_BYTES = 1024


class ProtocolError(Exception):
    """The peer did not send a supported, complete protocol message."""


class LineReader:
    def __init__(self, sock, timeout):
        self.sock = sock
        self.timeout = timeout
        self.buffer = bytearray()

    def read(self, label):
        # A deadline for the whole line also bounds peers that drip bytes forever.
        deadline = time.monotonic() + self.timeout
        while True:
            newline = self.buffer.find(b"\n")
            if newline >= 0:
                raw = bytes(self.buffer[:newline])
                del self.buffer[:newline + 1]
                if raw.endswith(b"\r"):
                    raw = raw[:-1]
                if len(raw) > MAX_LINE_BYTES:
                    raise ProtocolError(f"{label}: linha excede {MAX_LINE_BYTES} bytes")
                if not raw or any(byte < 32 or byte > 126 for byte in raw):
                    raise ProtocolError(f"{label}: linha vazia ou caracteres não ASCII imprimíveis")
                return raw.decode("ascii")
            if len(self.buffer) >= MAX_LINE_BYTES + 2:
                raise ProtocolError(f"{label}: linha excede {MAX_LINE_BYTES} bytes")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"{label}: timeout esperando linha completa")
            self.sock.settimeout(remaining)
            try:
                chunk = self.sock.recv(MAX_LINE_BYTES + 2 - len(self.buffer))
            except socket.timeout as exc:
                raise TimeoutError(f"{label}: timeout esperando linha completa") from exc
            if not chunk:
                raise ProtocolError(f"{label}: conexão encerrada antes da linha completa (EOF)")
            self.buffer.extend(chunk)


def fields(line, prefix, required):
    tokens = line.split()
    prefix_tokens = prefix.split()
    if tokens[:len(prefix_tokens)] != prefix_tokens:
        raise ProtocolError(f"Esperado {prefix!r}; recebido {line[:160]!r}")
    result = {}
    for token in tokens[len(prefix_tokens):]:
        key, sep, value = token.partition("=")
        if not sep or not key or not value or key in result:
            raise ProtocolError(f"{prefix}: campo inválido ou repetido: {token!r}")
        result[key] = value
    missing = set(required) - result.keys()
    if missing:
        raise ProtocolError(f"{prefix}: campos ausentes: {', '.join(sorted(missing))}")
    return result


def expect(values, key, expected):
    if values[key] != expected:
        raise ProtocolError(f"{key}: esperado {expected!r}, recebido {values[key]!r}")


def integer(values, key, low, high):
    value = values[key]
    if not re.fullmatch(r"-?[0-9]+", value) or not low <= int(value) <= high:
        raise ProtocolError(f"{key}: inteiro fora da faixa [{low}, {high}]: {value!r}")


def query_status(host, port=5000, timeout=5.0, capture_id=None):
    """Return protocol lines and fields; never retry or issue CAPTURE implicitly."""
    # Numeric IPv4 avoids an unbounded DNS lookup before the connect timeout.
    host = str(ipaddress.IPv4Address(host))
    if capture_id is not None and (type(capture_id) is not int or not 1 <= capture_id <= 0xFFFFFFFF):
        raise ValueError("ID da captura deve estar entre 1 e 4294967295")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(timeout)
        try:
            sock.connect((host, port))
        except socket.timeout as exc:
            raise TimeoutError("CONNECT: timeout ao conectar") from exc
        reader = LineReader(sock, timeout)
        hello_line = reader.read("HELLO")
        hello = fields(hello_line, "# HELLO MM1LAB 2", ("stage", "boot_id", "connection_id"))
        stage = hello["stage"]
        if stage not in {"network_only", "unit_capture"}:
            raise ProtocolError(f"HELLO: etapa não suportada: {stage}")
        if not re.fullmatch(r"[0-9a-fA-F]{16}", hello["boot_id"]):
            raise ProtocolError("HELLO: boot_id deve conter 16 dígitos hexadecimais")
        integer(hello, "connection_id", 0, 0xFFFFFFFF)

        meta_line = reader.read("META")
        meta = fields(meta_line, "# META", ("capture", "commands"))
        expect(meta, "capture", "single" if stage == "unit_capture" else "unavailable")
        if "STATUS" not in meta["commands"].split(","):
            raise ProtocolError("META: comando STATUS não anunciado")

        sock.settimeout(timeout)
        try:
            sock.sendall(b"STATUS\n")
        except socket.timeout as exc:
            raise TimeoutError("SEND: timeout enviando STATUS") from exc
        status_line = reader.read("STATUS")
        status = fields(status_line, "# OK STATUS", (
            "stage", "wifi", "ip", "rssi_dbm", "tcp", "port", "hosted",
            "sensors", "attempt", "disconnect_reason", "uptime_ms",
        ))
        expect(status, "stage", stage)
        expect(status, "sensors", "ENABLED" if stage == "unit_capture" else "NOT_IMPLEMENTED")
        if status["wifi"] not in {"CONFIG_REQUIRED", "RETRY_WAIT", "CONNECTING", "CONNECTED", "FAULT"}:
            raise ProtocolError("STATUS: estado Wi-Fi desconhecido")
        if status["tcp"] not in {"LISTENING", "OFF"}:
            raise ProtocolError("STATUS: estado TCP desconhecido")
        try:
            ipaddress.IPv4Address(status["ip"])
        except ValueError as exc:
            raise ProtocolError("STATUS: endereço IPv4 inválido") from exc
        integer(status, "port", 1, 65535)
        integer(status, "hosted", 0, 1)
        integer(status, "rssi_dbm", -2147483648, 2147483647)
        for key in ("attempt", "disconnect_reason", "uptime_ms"):
            integer(status, key, 0, 0xFFFFFFFF)
        result = {
            "lines": [hello_line, meta_line, status_line],
            "hello": hello, "meta": meta, "status": status,
        }
        if capture_id is not None:
            if stage != "unit_capture" or "CAPTURE" not in meta["commands"].split(","):
                raise ProtocolError("Este firmware ainda não oferece captura unitária")
            for key, value in (("schema", "2"), ("n_max", "1"), ("csv_header", "per_capture")):
                if meta.get(key) != value:
                    raise ProtocolError(f"META: captura unitária requer {key}={value}")
            sock.settimeout(timeout)
            sock.sendall(f"CAPTURE {capture_id} 1\n".encode("ascii"))
            ack_line = reader.read("ACK")
            ack = fields(ack_line, "# OK CAPTURE", ("request_id", "n", "timeout_ms"))
            expect(ack, "request_id", str(capture_id))
            expect(ack, "n", "1")
            integer(ack, "timeout_ms", 1, 9000)
            reader.timeout = int(ack["timeout_ms"])/1000 + timeout
            header = reader.read("CSV header")
            row = reader.read("CSV row")
            done_line = reader.read("DONE")
            expected = ("request_id,sample_index,distance_m,laser_valid,laser_error,qw,qx,qy,qz,"
                        "azimuth_deg,inclination_deg,roll_deg,angles_valid,angle_error,accuracy_rad,"
                        "imu_status,imu_seq,imu_valid,imu_error")
            if header != expected:
                raise ProtocolError("Cabeçalho CSV de captura inesperado")
            cells = next(csv.reader([row]))
            if len(cells) != len(expected.split(",")):
                raise ProtocolError("Quantidade de colunas incorreta")
            sample = dict(zip(expected.split(","), cells))
            expect(sample, "request_id", str(capture_id))
            expect(sample, "sample_index", "1")
            for key in ("laser_valid", "imu_valid", "angles_valid"):
                if sample[key] not in {"0", "1"}:
                    raise ProtocolError(f"CSV: flag inválida em {key}")
            if sample["angles_valid"] == "1" and sample["imu_valid"] != "1":
                raise ProtocolError("Ângulos válidos sem IMU válida")
            # Invalid reports may retain finite raw components and quality. At an
            # angular singularity only the undefined angles are empty.
            received = sample["imu_seq"] != ""
            for keys, valid, optional in ((["distance_m"], sample["laser_valid"], False),
                                (["qw", "qx", "qy", "qz", "accuracy_rad"], sample["imu_valid"], received),
                                (["azimuth_deg", "inclination_deg", "roll_deg"], sample["angles_valid"], sample["imu_valid"] == "1")):
                for key in keys:
                    try:
                        ok = ((sample[key] == "" and valid != "1") or
                              ((valid == "1" or optional) and math.isfinite(float(sample[key]))))
                    except ValueError:
                        ok = False
                    if not ok:
                        raise ProtocolError(f"CSV: valor/validade incompatível em {key}")
            if sample["imu_valid"] == "1" and not received:
                raise ProtocolError("CSV: IMU válida sem relatório")
            if received:
                integer(sample, "imu_status", 0, 3)
                integer(sample, "imu_seq", 0, 255)
            elif sample["imu_status"]:
                raise ProtocolError("CSV: qualidade sem relatório")
            if sample["laser_valid"] == "1" and float(sample["distance_m"]) <= 0:
                raise ProtocolError("CSV: distância válida deve ser positiva")
            if sample["imu_valid"] == "1" and float(sample["accuracy_rad"]) < 0:
                raise ProtocolError("CSV: accuracy_rad válida deve ser não negativa")
            for flag, error in (("laser_valid", "laser_error"), ("imu_valid", "imu_error"),
                                ("angles_valid", "angle_error")):
                if not sample[error] or (sample[error] == "NONE") != (sample[flag] == "1"):
                    raise ProtocolError(f"CSV: erro/validade incompatível em {error}")
            done = fields(done_line, "# DONE", ("request_id", "completion", "result", "n_requested",
                                                 "n_rows", "n_valid", "n_laser_valid", "n_imu_valid", "code", "state"))
            expect(done, "request_id", str(capture_id))
            expect(done, "completion", "COMPLETE")
            expect(done, "n_requested", "1")
            expect(done, "n_rows", "1")
            expect(done, "code", "NONE")
            expect(done, "state", "IDLE")
            lv, iv = int(sample["laser_valid"]), int(sample["imu_valid"])
            expect(done, "n_valid", str(lv*iv))
            expect(done, "n_laser_valid", str(lv))
            expect(done, "n_imu_valid", str(iv))
            expect(done, "result", "OK" if lv and iv else "PARTIAL" if lv or iv else "ERROR")
            result["lines"].extend([ack_line, header, row, done_line])
            result["capture"] = {"sample": sample, "done": done}
        return result


def ipv4(value):
    try:
        return str(ipaddress.IPv4Address(value))
    except ValueError as exc:
        raise argparse.ArgumentTypeError("informe o IPv4 mostrado pela placa") from exc


def port_number(value):
    try:
        port = int(value)
        if 1 <= port <= 65535:
            return port
    except ValueError:
        pass
    raise argparse.ArgumentTypeError("porta deve estar entre 1 e 65535")


def timeout_seconds(value):
    try:
        timeout = float(value)
        if math.isfinite(timeout) and timeout > 0:
            return timeout
    except ValueError:
        pass
    raise argparse.ArgumentTypeError("timeout deve ser um número finito maior que zero")


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def main(argv=None):
    parser = argparse.ArgumentParser(description="STATUS e teste opcional de captura unitária do MM1LAB 2.")
    parser.add_argument("--host", type=ipv4, default=os.getenv("LAB_HOST") or None,
                        help="IPv4 da trena (ou LAB_HOST)")
    parser.add_argument("--port", type=port_number, default=os.getenv("LAB_PORT", "5000"))
    parser.add_argument("--timeout", type=timeout_seconds, default=os.getenv("LAB_TIMEOUT", "5"),
                        help="segundos por conexão, envio e linha completa (padrão: 5)")
    parser.add_argument("--log", type=Path, default=os.getenv("LAB_STATUS_LOG") or None,
                        help="acrescenta o resultado a um JSONL, por exemplo /data/status.jsonl")
    parser.add_argument("--capture", type=int, metavar="ID",
                        help="após STATUS, testa uma aquisição unitária e aguarda DONE")
    args = parser.parse_args(argv)
    if args.host is None:
        parser.error("informe --host IP ou configure LAB_HOST")
    if args.capture is not None and not 1 <= args.capture <= 0xFFFFFFFF:
        parser.error("ID da captura deve estar entre 1 e 4294967295")

    record = {"host": args.host, "port": args.port, "timeout_s": args.timeout,
              "pc_started_utc": utc_now()}
    if args.capture is not None:
        record["capture_id"] = args.capture
    started = time.monotonic()
    exit_code = 0
    try:
        record.update(query_status(args.host, args.port, args.timeout, args.capture))
        record["ok"] = True
        for line in record["lines"]:
            print(line)
    except (OSError, ProtocolError) as exc:
        record.update(ok=False, error=str(exc))
        print(f"ERRO {args.host}:{args.port}: {exc}", file=sys.stderr)
        exit_code = 1
    except KeyboardInterrupt:
        record.update(ok=False, error="Interrompido pelo usuário")
        print(record["error"], file=sys.stderr)
        exit_code = 130
    record["pc_finished_utc"] = utc_now()
    record["elapsed_ms"] = round((time.monotonic() - started) * 1000, 3)
    if args.log:
        try:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            with args.log.open("a", encoding="utf-8") as output:
                output.write(json.dumps(record, ensure_ascii=False) + "\n")
        except OSError as exc:
            print(f"ERRO ao gravar {args.log}: {exc}", file=sys.stderr)
            exit_code = 1
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
