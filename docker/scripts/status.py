"""One bounded STATUS exchange with the MM1LAB network-only firmware."""

import argparse
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


def query_status(host, port=5000, timeout=5.0):
    """Return protocol lines and fields; never retry or issue CAPTURE implicitly."""
    # Numeric IPv4 avoids an unbounded DNS lookup before the connect timeout.
    host = str(ipaddress.IPv4Address(host))
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(timeout)
        try:
            sock.connect((host, port))
        except socket.timeout as exc:
            raise TimeoutError("CONNECT: timeout ao conectar") from exc
        reader = LineReader(sock, timeout)
        hello_line = reader.read("HELLO")
        hello = fields(hello_line, "# HELLO MM1LAB 2", ("stage", "boot_id", "connection_id"))
        expect(hello, "stage", "network_only")
        if not re.fullmatch(r"[0-9a-fA-F]{16}", hello["boot_id"]):
            raise ProtocolError("HELLO: boot_id deve conter 16 dígitos hexadecimais")
        integer(hello, "connection_id", 0, 0xFFFFFFFF)

        meta_line = reader.read("META")
        meta = fields(meta_line, "# META", ("capture", "commands"))
        expect(meta, "capture", "unavailable")
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
        expect(status, "stage", "network_only")
        expect(status, "sensors", "NOT_IMPLEMENTED")
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
        return {
            "lines": [hello_line, meta_line, status_line],
            "hello": hello, "meta": meta, "status": status,
        }


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
    parser = argparse.ArgumentParser(description="Consulta única de STATUS do firmware MM1LAB 2.")
    parser.add_argument("--host", type=ipv4, default=os.getenv("LAB_HOST") or None,
                        help="IPv4 da trena (ou LAB_HOST)")
    parser.add_argument("--port", type=port_number, default=os.getenv("LAB_PORT", "5000"))
    parser.add_argument("--timeout", type=timeout_seconds, default=os.getenv("LAB_TIMEOUT", "5"),
                        help="segundos por conexão, envio e linha completa (padrão: 5)")
    parser.add_argument("--log", type=Path, default=os.getenv("LAB_STATUS_LOG") or None,
                        help="acrescenta o resultado a um JSONL, por exemplo /data/status.jsonl")
    args = parser.parse_args(argv)
    if args.host is None:
        parser.error("informe --host IP ou configure LAB_HOST")

    record = {"host": args.host, "port": args.port, "timeout_s": args.timeout,
              "pc_started_utc": utc_now()}
    started = time.monotonic()
    exit_code = 0
    try:
        record.update(query_status(args.host, args.port, args.timeout))
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
