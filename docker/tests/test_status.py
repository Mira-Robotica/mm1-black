import contextlib
import io
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import status
from mock_server import HELLO, META, STATUS, exchange, read_command


@contextlib.contextmanager
def peer(handler):
    """Run a single bounded TCP peer and surface errors from its worker."""
    errors = []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(2)

        def serve():
            try:
                conn, _ = listener.accept()
                with conn:
                    conn.settimeout(2)
                    handler(conn)
            except Exception as exc:
                errors.append(exc)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        try:
            yield listener.getsockname()[1]
        finally:
            worker.join(3)
            if worker.is_alive():
                raise AssertionError("Test peer did not finish")
            if errors:
                raise errors[0]


class StatusTests(unittest.TestCase):
    def setUp(self):
        self.env = patch.dict(os.environ, {}, clear=True)
        self.env.start()
        self.addCleanup(self.env.stop)

    def test_real_tcp_success_and_exact_command(self):
        with peer(exchange) as port:
            result = status.query_status("127.0.0.1", port, 1)
        self.assertEqual(result["status"]["hosted"], "1")
        self.assertEqual(result["status"]["rssi_dbm"], "-50")
        self.assertEqual(result["lines"], [line.decode().strip() for line in (HELLO, META, STATUS)])

    def test_fragmented_tcp_crlf(self):
        def handler(conn):
            for byte in (HELLO + META).replace(b"\n", b"\r\n"):
                conn.sendall(bytes([byte]))
            read_command(conn)
            for byte in STATUS.replace(b"\n", b"\r\n"):
                conn.sendall(bytes([byte]))
        with peer(handler) as port:
            self.assertEqual(status.query_status("127.0.0.1", port, 1)["status"]["wifi"], "CONNECTED")

    def test_buffer_handles_coalesced_and_fragmented_lines(self):
        class Chunks:
            def __init__(self):
                self.parts = iter([b"first\r", b"\nsecond\nthi", b"rd\n"])
            def settimeout(self, seconds):
                pass
            def recv(self, size):
                return next(self.parts)
        reader = status.LineReader(Chunks(), 1)
        self.assertEqual([reader.read("test") for _ in range(3)], ["first", "second", "third"])

    def test_bad_hello_or_meta(self):
        cases = [
            HELLO.replace(b"MM1LAB 2", b"MM1LAB 3"),
            HELLO.replace(b"0123456789abcdef", b"unknown"),
            HELLO.replace(b"connection_id=1", b"connection_id=x"),
            HELLO + META.replace(b"STATUS", b"CAPTURE"),
            HELLO + b"# META commands=STATUS\n",
        ]
        for data in cases:
            with self.subTest(data=data), peer(lambda conn: conn.sendall(data)) as port:
                with self.assertRaises(status.ProtocolError):
                    status.query_status("127.0.0.1", port, 1)

    def test_invalid_status_responses(self):
        cases = [
            b"# ERR BUSY\n",
            b"# OK STATUS stage=network_only\n",
            STATUS.replace(b"stage=network_only", b"stage=capture"),
            STATUS.replace(b"hosted=1", b"hosted=9"),
            STATUS.replace(b"rssi_dbm=-50", b"rssi_dbm=NaN"),
            STATUS.replace(b"ip=127.0.0.1", b"ip=bad"),
            STATUS.replace(b"port=5000", b"port=0"),
            STATUS.replace(b"hosted=1", b"hosted=1 hosted=0"),
            STATUS.replace(b"uptime_ms=1234", b"uptime_ms=-1"),
        ]
        for response in cases:
            with self.subTest(response=response), peer(lambda conn: exchange(conn, response)) as port:
                with self.assertRaises(status.ProtocolError):
                    status.query_status("127.0.0.1", port, 1)

    def test_eof_in_each_phase(self):
        handlers = [
            lambda conn: None,
            lambda conn: conn.sendall(HELLO + b"# META"),
            lambda conn: exchange(conn, STATUS.rstrip(b"\n")),
        ]
        for handler in handlers:
            with self.subTest(handler=handler), peer(handler) as port:
                with self.assertRaisesRegex(status.ProtocolError, "EOF"):
                    status.query_status("127.0.0.1", port, 1)

    def test_timeout_in_each_phase(self):
        for phase in ("HELLO", "META", "STATUS"):
            done = threading.Event()
            def handler(conn):
                if phase in ("META", "STATUS"):
                    conn.sendall(HELLO)
                if phase == "STATUS":
                    conn.sendall(META)
                    read_command(conn)
                done.wait(1)
            with self.subTest(phase=phase), peer(handler) as port:
                try:
                    with self.assertRaisesRegex(TimeoutError, phase):
                        status.query_status("127.0.0.1", port, 0.05)
                finally:
                    done.set()

    def test_slow_bytes_do_not_reset_line_deadline(self):
        class Drip:
            def settimeout(self, seconds):
                pass
            def recv(self, size):
                return b"x"
        with patch.object(status.time, "monotonic", side_effect=[0, 0.04, 0.09, 0.11]):
            with self.assertRaises(TimeoutError):
                status.LineReader(Drip(), 0.1).read("HELLO")

    def test_line_limits_and_invalid_bytes(self):
        for data in (b"x" * 1025 + b"\n", b"x" * 1026, b"\n", b"bad\x00\n", b"bad\xff\n"):
            with self.subTest(data=data[:20]), peer(lambda conn: conn.sendall(data)) as port:
                with self.assertRaises(status.ProtocolError):
                    status.query_status("127.0.0.1", port, 1)

    def test_connection_refused_has_nonzero_exit_and_failure_log(self):
        with socket.socket() as reserved, tempfile.TemporaryDirectory() as directory:
            reserved.bind(("127.0.0.1", 0))  # Reserved but not listening.
            path = Path(directory) / "status.jsonl"
            with contextlib.redirect_stderr(io.StringIO()) as errors:
                code = status.main(["--host", "127.0.0.1", "--port", str(reserved.getsockname()[1]),
                                    "--log", str(path)])
            self.assertEqual(code, 1)
            self.assertIn("ERRO", errors.getvalue())
            self.assertFalse(json.loads(path.read_text())["ok"])

    def test_cli_env_and_log_append(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "status.jsonl"
            for _ in range(2):
                with peer(exchange) as port:
                    env = {"LAB_HOST": "127.0.0.1", "LAB_PORT": str(port), "LAB_STATUS_LOG": str(path)}
                    result = subprocess.run([sys.executable, status.__file__], env=env,
                                            capture_output=True, text=True, timeout=3)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("# OK STATUS", result.stdout)
            entries = [json.loads(line) for line in path.read_text().splitlines()]
            self.assertEqual(len(entries), 2)
            self.assertTrue(all(entry["ok"] for entry in entries))
            self.assertGreaterEqual(entries[0]["elapsed_ms"], 0)
            self.assertTrue(entries[0]["pc_started_utc"].endswith("+00:00"))

    def test_log_write_failure_is_not_success(self):
        with tempfile.TemporaryDirectory() as directory, peer(exchange) as port:
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                code = status.main(["--host", "127.0.0.1", "--port", str(port), "--log", directory])
        self.assertEqual(code, 1)

    def test_cli_rejects_invalid_arguments(self):
        for args in ([], ["--host", "bad"], ["--port", "0"], ["--port", "65536"],
                     ["--timeout", "0"], ["--timeout", "nan"], ["--timeout", "inf"]):
            with self.subTest(args=args), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    status.main(args)
                self.assertEqual(error.exception.code, 2)

    def test_cli_protocol_error_exit(self):
        with peer(lambda conn: conn.sendall(b"# NOT_THIS_PROTOCOL\n")) as port:
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(status.main(["--host", "127.0.0.1", "--port", str(port)]), 1)


if __name__ == "__main__":
    unittest.main()
