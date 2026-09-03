#!/usr/bin/env python3
"""Unit tests for tools/seekdb_cli.py (the embedded seekdb SQL client)."""

import importlib.util
import os
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
CLI_PATH = TOOLS_DIR / "seekdb_cli.py"

spec = importlib.util.spec_from_file_location("seekdb_cli", CLI_PATH)
seekdb_cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(seekdb_cli)


def build_packet(seq, payload):
    return struct.pack("<I", len(payload))[:3] + bytes((seq,)) + payload


def send_packet_raw(sock, seq, payload):
    sock.sendall(build_packet(seq, payload))


def recv_packet_raw(sock):
    header = b""
    while len(header) < 4:
        chunk = sock.recv(4 - len(header))
        if not chunk:
            raise EOFError("closed")
        header += chunk
    length = int.from_bytes(header[:3], "little")
    payload = b""
    while len(payload) < length:
        chunk = sock.recv(length - len(payload))
        if not chunk:
            raise EOFError("closed")
        payload += chunk
    return payload


def lenenc_str(value):
    data = value.encode("utf-8")
    size = len(data)
    if size < 251:
        prefix = bytes((size,))
    elif size < (1 << 16):
        prefix = b"\xfc" + struct.pack("<H", size)
    else:
        prefix = b"\xfd" + struct.pack("<I", size)[:3]
    return prefix + data


def column_definition(name):
    parts = [
        lenenc_str("def"),  # catalog
        lenenc_str("test"),  # schema
        lenenc_str("t"),  # table
        lenenc_str("t"),  # org_table
        lenenc_str(name),  # name
        lenenc_str(name),  # org_name
        b"\x0c",
        struct.pack("<H", 46),  # charset
        struct.pack("<I", 1024),  # column length
        bytes((0xFD,)),  # type string
        struct.pack("<H", 0),  # flags
        bytes((0,)),  # decimals
        b"\x00\x00",  # filler
        b"\x00",  # default value (empty)
    ]
    return b"".join(parts)


def row_packet(values):
    payload = b"".join(
        b"\xfb" if v is None else lenenc_str(str(v)) for v in values
    )
    return payload


class FakeSeekdbServer:
    """A tiny MySQL-protocol server used to exercise the CLI client."""

    SCRAMBLE = b"0123456789abcdefghij"

    def __init__(
        self,
        directory,
        transport="unix",
        auth_switch=False,
        password="",
        auth_plugin=b"mysql_native_password",
    ):
        self.directory = Path(directory)
        self.socket_path = self.directory / "run" / "sql.sock"
        self.transport = transport
        self.auth_switch = auth_switch
        self.auth_plugin = auth_plugin
        self.password = password
        self.expected_token = (
            seekdb_cli.native_password_token(password, self.SCRAMBLE) if password else None
        )
        if transport == "tcp":
            self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.listener.bind(("127.0.0.1", 0))
            self.port = self.listener.getsockname()[1]
        else:
            self.directory.mkdir(parents=True, exist_ok=True)
            (self.directory / "run").mkdir(parents=True, exist_ok=True)
            (self.directory / "run" / "seekdb.clients").touch()
            try:
                os.unlink(self.socket_path)
            except FileNotFoundError:
                pass
            self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.listener.bind(str(self.socket_path))
        self.listener.listen(4)
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _send_greeting(self, conn):
        caps = seekdb_cli.CLIENT_CAPABILITIES
        payload = (
            b"\x0a"
            + b"5.7.25-seekdb\x00"
            + struct.pack("<I", 42)            + self.SCRAMBLE[:8]
            + b"\x00"
            + struct.pack("<H", caps & 0xFFFF)
            + bytes((46,))
            + struct.pack("<H", 2)
            + struct.pack("<H", caps >> 16)
            + bytes((21,))
            + b"\x00" * 10
            + self.SCRAMBLE[8:]
            + b"\x00"
            + b"mysql_native_password\x00"
        )
        send_packet_raw(conn, 0, payload)

    def _send_ok(self, conn, seq):
        send_packet_raw(conn, seq, b"\x00" + b"\x00" + b"\x00" + struct.pack("<HH", 2, 0))

    def _send_error(self, conn, seq, code, message):
        payload = (
            b"\xff"
            + struct.pack("<H", code)
            + b"#42000"
            + message.encode("utf-8")
        )
        send_packet_raw(conn, seq, payload)

    def _send_resultset(self, conn, start_seq, columns, rows):
        seq = start_seq
        send_packet_raw(conn, seq, bytes((len(columns),)))
        seq += 1
        for name in columns:
            send_packet_raw(conn, seq, column_definition(name))
            seq += 1
        send_packet_raw(conn, seq, b"\xfe" + struct.pack("<HH", 0, 2))
        seq += 1
        for row in rows:
            send_packet_raw(conn, seq, row_packet(row))
            seq += 1
        send_packet_raw(conn, seq, b"\xfe" + struct.pack("<HH", 0, 2))

    def _handle_command(self, conn, payload):
        if not payload or payload[0] != seekdb_cli.COM_QUERY:
            self._send_error(conn, 1, 1064, "unsupported command")
            return
        sql = payload[1:].decode("utf-8", "replace").strip()
        upper = sql.upper()
        if upper.startswith("SELECT 1 AS ID"):
            self._send_resultset(conn, 1, ["id", "name"], [(1, "hello")])
        elif upper.startswith("SELECT NULL"):
            self._send_resultset(conn, 1, ["n", "v"], [(None, "x")])
        elif upper.startswith("SELECT"):
            self._send_resultset(conn, 1, ["count(1)"], [(2,)])
        elif upper.startswith("SHOW TABLES"):
            self._send_resultset(conn, 1, ["tables_in_test"], [("memory",)])
        elif upper.startswith("INSERT"):
            self._send_ok(conn, 1)
        elif upper.startswith("SET "):
            self._send_ok(conn, 1)
        elif upper == "BAD SQL":
            self._send_error(conn, 1, 1064, "you have an error in your SQL syntax")
        else:
            self._send_error(conn, 1, 1146, "table does not exist")

    def _serve(self):
        try:
            while True:
                conn, _ = self.listener.accept()
                self._send_greeting(conn)
                try:
                    _login = recv_packet_raw(conn)
                    if (
                        self.expected_token is not None
                        and self.expected_token not in _login
                    ):
                        self._send_error(conn, 2, 1045, "access denied")
                        continue
                    if self.auth_switch:
                        payload = (
                            b"\xfe"
                            + self.auth_plugin
                            + b"\x00"
                            + self.SCRAMBLE
                            + b"\x00"
                        )
                        send_packet_raw(conn, 2, payload)
                        auth_response = recv_packet_raw(conn)
                        if self.auth_plugin == b"mysql_clear_password":
                            if self.password and auth_response != (
                                self.password.encode("utf-8") + b"\x00"
                            ):
                                self._send_error(conn, 4, 1045, "access denied")
                                continue
                        elif (
                            self.expected_token is not None
                            and auth_response != self.expected_token
                        ):
                            self._send_error(conn, 4, 1045, "access denied")
                            continue
                        self._send_ok(conn, 4)
                    else:
                        self._send_ok(conn, 2)
                    while True:
                        payload = recv_packet_raw(conn)
                        if not payload:
                            break
                        self._handle_command(conn, payload)
                except (EOFError, OSError):
                    pass
                finally:
                    conn.close()
        except OSError:
            pass

    def close(self):
        try:
            self.listener.close()
        finally:
            if self.transport != "tcp":
                try:
                    os.unlink(self.socket_path)
                except OSError:
                    pass


class NativePasswordTests(unittest.TestCase):
    def test_empty_password(self):
        self.assertEqual(seekdb_cli.native_password_token("", b"x" * 20), b"")

    def test_token_matches_reference_computation(self):
        scramble = b"0123456789abcdefghij"
        password = "s3cret"
        token = seekdb_cli.native_password_token(password, scramble)
        stage1 = __import__("hashlib").sha1(password.encode("utf-8")).digest()
        stage2 = __import__("hashlib").sha1(stage1).digest()
        expected = bytes(
            a ^ b
            for a, b in zip(stage1, __import__("hashlib").sha1(scramble + stage2).digest())
        )
        self.assertEqual(token, expected)
        self.assertEqual(len(token), 20)


class SplitStatementsTests(unittest.TestCase):
    def test_simple(self):
        self.assertEqual(seekdb_cli.split_statements("SELECT 1; SELECT 2;"), ["SELECT 1", "SELECT 2"])

    def test_quote_semicolon_kept(self):
        sql = "SELECT 'a;b' AS v; SHOW TABLES"
        self.assertEqual(seekdb_cli.split_statements(sql), ["SELECT 'a;b' AS v", "SHOW TABLES"])

    def test_double_quote_backslash_escape(self):
        sql = 'SELECT "a\\"b;c" AS v; SELECT 2'
        self.assertEqual(seekdb_cli.split_statements(sql), ['SELECT "a\\"b;c" AS v', "SELECT 2"])

    def test_comments(self):
        sql = "-- comment; no split\nSELECT 1; /* block; comment */ SELECT 2"
        self.assertEqual(seekdb_cli.split_statements(sql), ["SELECT 1", "SELECT 2"])

    def test_trailing_partial(self):
        self.assertEqual(seekdb_cli.split_statements("SELECT 1"), ["SELECT 1"])

    def test_double_dash_in_expression(self):
        # "----" without a trailing space is arithmetic, not a comment.
        self.assertEqual(seekdb_cli.split_statements("SELECT 5--2;"), ["SELECT 5--2"])

    def test_double_dash_with_space_is_comment(self):
        self.assertEqual(
            seekdb_cli.split_statements("SELECT 5 -- 2;"),
            ["SELECT 5"],
        )


class SendPacketTests(unittest.TestCase):
    def _receive(self, sock, size):
        data = b""
        while len(data) < size:
            chunk = sock.recv(size - len(data))
            if not chunk:
                break
            data += chunk
        return data

    def _frame(self, sock):
        header = self._receive(sock, 4)
        if len(header) < 4:
            return None
        length = int.from_bytes(header[:3], "little")
        return header, self._receive(sock, length)

    def test_exact_max_packet_gets_terminator(self):
        a, b = socket.socketpair()
        try:
            payload = b"x" * 0xFFFFFF
            worker = threading.Thread(
                target=lambda: seekdb_cli.send_packet(a, 0, payload)
            )
            worker.start()
            first = self._frame(b)
            self.assertEqual(len(first[1]), 0xFFFFFF)
            terminator = self._frame(b)
            self.assertEqual(terminator[1], b"")
            worker.join(timeout=10)
            self.assertFalse(worker.is_alive())
        finally:
            a.close()
            b.close()

    def test_partial_last_packet_has_no_terminator(self):
        a, b = socket.socketpair()
        try:
            payload = b"y" * (0xFFFFFF + 5)
            worker = threading.Thread(
                target=lambda: seekdb_cli.send_packet(a, 0, payload)
            )
            worker.start()
            first = self._frame(b)
            self.assertEqual(len(first[1]), 0xFFFFFF)
            second = self._frame(b)
            self.assertEqual(len(second[1]), 5)
            worker.join(timeout=10)
            self.assertFalse(worker.is_alive())
        finally:
            a.close()
            b.close()


class RenderTests(unittest.TestCase):
    def test_display_width_counts_wide_characters(self):
        self.assertEqual(seekdb_cli.display_width("ab"), 2)
        self.assertEqual(seekdb_cli.display_width("中文"), 4)
        self.assertEqual(seekdb_cli.display_width("a中"), 3)

    def test_table_aligns_wide_characters(self):
        table = seekdb_cli.render_table(
            ["名称", "value"], [["中文", "x"]], 100
        )
        lines = table.splitlines()
        header_cell = lines[1].split("|")[1]
        row_cell = lines[3].split("|")[1]
        self.assertEqual(
            seekdb_cli.display_width(header_cell),
            seekdb_cli.display_width(row_cell),
        )


class ClientLockTests(unittest.TestCase):
    def test_shared_lock_blocks_exclusive_and_release_frees(self):
        if os.name == "nt":
            self.skipTest("flock is not available on Windows")
        try:
            import fcntl  # noqa: F401
        except ImportError:
            self.skipTest("fcntl is not available")
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            (Path(tmp) / "run").mkdir()
            lock_fd = seekdb_cli.open_client_lock(tmp)
            self.assertIsNotNone(lock_fd)
            lock_path = Path(tmp) / "run" / "seekdb.clients"
            try:
                probe = os.open(str(lock_path), os.O_RDWR)
                try:
                    # the observer's exclusive lock must not be acquirable
                    # while the CLI holds its shared lock
                    with self.assertRaises(OSError):
                        fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
                finally:
                    os.close(probe)
                os.close(lock_fd)
                lock_fd = None
                probe = os.open(str(lock_path), os.O_RDWR)
                try:
                    fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    fcntl.flock(probe, fcntl.LOCK_UN)
                finally:
                    os.close(probe)
            finally:
                if lock_fd is not None:
                    os.close(lock_fd)


class HandshakeTests(unittest.TestCase):
    def test_parse_handshake(self):
        server = FakeSeekdbServer(tempfile.mkdtemp(prefix="seekdb-cli-test-"))
        try:
            caps = seekdb_cli.CLIENT_CAPABILITIES
            payload = (
                b"\x0a"
                + b"5.7.25-seekdb\x00"
                + struct.pack("<I", 7)
                + server.SCRAMBLE[:8]
                + b"\x00"
                + struct.pack("<H", caps & 0xFFFF)
                + bytes((46,))
                + struct.pack("<H", 2)
                + struct.pack("<H", caps >> 16)
                + bytes((21,))
                + b"\x00" * 10
                + server.SCRAMBLE[8:]
                + b"\x00"
                + b"mysql_native_password\x00"
            )
            greeting = seekdb_cli.parse_handshake(payload)
            self.assertEqual(greeting["connection_id"], 7)
            self.assertEqual(greeting["scramble"], server.SCRAMBLE)
            self.assertEqual(greeting["plugin"], b"mysql_native_password")
        finally:
            server.close()

    def test_parse_handshake_rejects_unterminated_version(self):
        with self.assertRaises(seekdb_cli.ProtocolError):
            seekdb_cli.parse_handshake(b"\x0a" + b"no-nul-terminated-version")

    def test_parse_ok_rejects_short_packet(self):
        with self.assertRaises(seekdb_cli.ProtocolError):
            seekdb_cli.parse_ok(b"\x00\x00\x00")


class EndToEndTests(unittest.TestCase):
    def _run_cli(self, directory, *args, **kwargs):
        command = [sys.executable, str(CLI_PATH)] + list(args)
        return subprocess.run(
            command,
            cwd=str(directory),
            capture_output=True,
            text=True,
            timeout=30,
            **kwargs,
        )

    def test_execute_select(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "-e",
                    "SELECT 1 AS id, 'hello' AS name;",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("id", result.stdout)
                self.assertIn("name", result.stdout)
                self.assertIn("1", result.stdout)
                self.assertIn("hello", result.stdout)
                self.assertIn("1 row(s) in set", result.stdout)
            finally:
                server.close()

    def test_data_dir_discovery(self):
        """--data-dir alone must locate <data-dir>/run/sql.sock."""
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                result = self._run_cli(
                    tmp,
                    "--data-dir",
                    str(tmp),
                    "-e",
                    "SELECT 1 AS id, 'hello' AS name;",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("hello", result.stdout)
            finally:
                server.close()

    def test_execute_batch(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "--batch",
                    "-e",
                    "SHOW TABLES;",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("memory", result.stdout)
            finally:
                server.close()

    def test_execute_error(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "-e",
                    "BAD SQL;",
                )
                self.assertEqual(result.returncode, 1)
                self.assertIn("ERROR 1064", result.stderr)
            finally:
                server.close()

    def test_execute_error_does_not_abort_batch(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "-e",
                    "BAD SQL; SELECT 1 AS id, 'hello' AS name;",
                )
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("ERROR 1064", result.stderr)
                self.assertIn("hello", result.stdout)
            finally:
                server.close()

    def test_stdin_batch(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                result = subprocess.run(
                    [sys.executable, str(CLI_PATH), "--socket", str(server.socket_path)],
                    cwd=str(tmp),
                    input="SELECT 1 AS id, 'hello' AS name;\n",
                    capture_output=True,
                    text=True,
                    timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("hello", result.stdout)
            finally:
                server.close()

    def test_wrapper_executable(self):
        """The README command (python3 tools/seekdb-cli --data-dir DIR) works."""
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                result = subprocess.run(
                    [
                        sys.executable,
                        str(TOOLS_DIR / "seekdb-cli"),
                        "--data-dir",
                        str(tmp),
                        "-e",
                        "SELECT 1 AS id, 'hello' AS name;",
                    ],
                    cwd=str(tmp),
                    capture_output=True,
                    text=True,
                    timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("hello", result.stdout)
            finally:
                server.close()

    def test_execute_tcp(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp, transport="tcp")
            try:
                result = self._run_cli(
                    tmp,
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(server.port),
                    "-e",
                    "SELECT 1 AS id, 'hello' AS name;",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("hello", result.stdout)
            finally:
                server.close()

    def test_missing_socket(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            empty_dir = Path(tmp) / "not-running"
            empty_dir.mkdir()
            result = self._run_cli(
                str(empty_dir),
                "--data-dir",
                str(empty_dir),
                "-e",
                "SELECT 1;",
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("no seekdb SQL socket found", result.stderr)

    def test_batch_tab_separated(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "--batch",
                    "-e",
                    "SELECT 1 AS id, 'hello' AS name;",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("\t", result.stdout)
                self.assertIn("hello", result.stdout)
            finally:
                server.close()

    def test_execute_null(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "-e",
                    "SELECT NULL AS n, 'x' AS v;",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("NULL", result.stdout)
                self.assertIn("x", result.stdout)
            finally:
                server.close()

    def test_execute_insert(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "-e",
                    "INSERT INTO t VALUES (1);",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("Query OK", result.stdout)
            finally:
                server.close()

    def test_execute_multiple_statements(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "-e",
                    "SELECT 1 AS id, 'hello' AS name; SELECT 2;",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("hello", result.stdout)
                self.assertIn("count(1)", result.stdout)
            finally:
                server.close()

    def test_auth_switch_native(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp, auth_switch=True)
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "-e",
                    "SELECT 1 AS id, 'hello' AS name;",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("hello", result.stdout)
            finally:
                server.close()

    def test_password_auth(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp, password="s3cret")
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "-p",
                    "s3cret",
                    "-e",
                    "SELECT 1 AS id, 'hello' AS name;",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("hello", result.stdout)
            finally:
                server.close()

    def test_auth_switch_native_with_password(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp, auth_switch=True, password="s3cret")
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "-p",
                    "s3cret",
                    "-e",
                    "SELECT 1 AS id, 'hello' AS name;",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("hello", result.stdout)
            finally:
                server.close()

    def test_auth_switch_clear_password_local_socket(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(
                tmp,
                auth_switch=True,
                auth_plugin=b"mysql_clear_password",
                password="s3cret",
            )
            try:
                result = self._run_cli(
                    tmp,
                    "--socket",
                    str(server.socket_path),
                    "-p",
                    "s3cret",
                    "-e",
                    "SELECT 1 AS id, 'hello' AS name;",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("hello", result.stdout)
            finally:
                server.close()

    def test_auth_switch_clear_password_rejected_over_tcp(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(
                tmp,
                transport="tcp",
                auth_switch=True,
                auth_plugin=b"mysql_clear_password",
            )
            try:
                result = self._run_cli(
                    tmp,
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(server.port),
                    "-e",
                    "SELECT 1;",
                )
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    "mysql_clear_password auth is only allowed over a local socket",
                    result.stderr,
                )
            finally:
                server.close()

    def test_interactive(self):
        try:
            import pty
        except ImportError:
            self.skipTest("pty is not available on this platform")
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                master, slave = pty.openpty()
                proc = subprocess.Popen(
                    [sys.executable, str(CLI_PATH), "--socket", str(server.socket_path)],
                    stdin=slave,
                    stdout=slave,
                    stderr=slave,
                    cwd=str(tmp),
                )
                os.close(slave)
                os.write(
                    master,
                    b"SELECT 1 AS id, 'hello' AS name;\nquit\n",
                )
                output = b""
                while proc.poll() is None:
                    readable, _, _ = select.select([master], [], [], 0.5)
                    if readable:
                        try:
                            chunk = os.read(master, 4096)
                        except OSError:
                            break
                        if not chunk:
                            break
                        output += chunk
                while True:
                    try:
                        chunk = os.read(master, 4096)
                    except OSError:
                        break
                    if not chunk:
                        break
                    output += chunk
                os.close(master)
                rc = proc.wait(timeout=10)
                text = output.decode("utf-8", "replace")
                self.assertEqual(rc, 0, text)
                self.assertIn("hello", text)
            finally:
                server.close()

    def test_interactive_quit_mid_buffer(self):
        """exit-like commands must quit even with a partial statement buffered."""
        try:
            import pty
        except ImportError:
            self.skipTest("pty is not available on this platform")
        with tempfile.TemporaryDirectory(prefix="seekdb-cli-e2e-") as tmp:
            server = FakeSeekdbServer(tmp)
            try:
                master, slave = pty.openpty()
                proc = subprocess.Popen(
                    [sys.executable, str(CLI_PATH), "--socket", str(server.socket_path)],
                    stdin=slave,
                    stdout=slave,
                    stderr=slave,
                    cwd=str(tmp),
                )
                os.close(slave)
                os.write(master, b"SHOW TABLES\n\\q\n")
                output = b""
                while proc.poll() is None:
                    readable, _, _ = select.select([master], [], [], 0.5)
                    if readable:
                        try:
                            chunk = os.read(master, 4096)
                        except OSError:
                            break
                        if not chunk:
                            break
                        output += chunk
                while True:
                    try:
                        chunk = os.read(master, 4096)
                    except OSError:
                        break
                    if not chunk:
                        break
                    output += chunk
                os.close(master)
                rc = proc.wait(timeout=10)
                text = output.decode("utf-8", "replace")
                self.assertEqual(rc, 0, text)
                self.assertNotIn("ERROR", text)
            finally:
                server.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
