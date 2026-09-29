"""One-shot loopback simulator for the network-only firmware (no hardware)."""

import argparse
import socket

HELLO = b"# HELLO MM1LAB 2 stage=network_only boot_id=0123456789abcdef connection_id=1\n"
META = b"# META capture=unavailable commands=STATUS\n"
STATUS = (b"# OK STATUS stage=network_only wifi=CONNECTED ip=127.0.0.1 rssi_dbm=-50 "
          b"tcp=LISTENING port=5000 hosted=1 sensors=NOT_IMPLEMENTED "
          b"attempt=1 disconnect_reason=0 uptime_ms=1234\n")


def read_command(conn):
    command = bytearray()
    while not command.endswith(b"\n") and len(command) < 128:
        chunk = conn.recv(1)
        if not chunk:
            break
        command.extend(chunk)
    if command != b"STATUS\n":
        raise AssertionError(f"Expected exactly STATUS, got {bytes(command)!r}")


def exchange(conn, response=STATUS):
    conn.sendall(HELLO + META)
    read_command(conn)
    conn.sendall(response)


def main():
    parser = argparse.ArgumentParser(description="Simulador local: atende uma consulta e encerra.")
    parser.add_argument("--port", type=int, default=15000)
    args = parser.parse_args()
    with socket.socket() as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", args.port))
        server.listen(1)
        server.settimeout(60)
        print(f"SIMULADOR em 127.0.0.1:{server.getsockname()[1]} (uma consulta)", flush=True)
        conn, _ = server.accept()
        with conn:
            conn.settimeout(5)
            exchange(conn)


if __name__ == "__main__":
    main()
