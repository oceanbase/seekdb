#!/usr/bin/env python3
"""seekdb-cli: a small MySQL-protocol client for embedded seekdb databases.

The embedded seekdb engine exposes its SQL service on a Unix socket at
``<data-dir>/run/sql.sock`` (Linux/macOS) while the application that opened
the database is running.  This CLI speaks the MySQL wire protocol directly
over that socket (or over TCP for server mode), so it has no dependency on
pymysql or any other client library.

Typical usage::

    # inspect a database owned by a running pyseekdb application
    python3 tools/seekdb_cli.py --data-dir ./agent_state.db

    # run a single statement
    python3 tools/seekdb_cli.py --data-dir ./agent_state.db -e "SELECT * FROM memory LIMIT 10;"
"""

import argparse
import hashlib
import os
import socket
import struct
import sys
import unicodedata

try:  # enables line editing and history in interactive mode on Unix
    import readline  # noqa: F401
except ImportError:
    pass

# --- MySQL client capability flags (see MySQL C API / protocol 4.1) ---
CAP_LONG_PASSWORD = 0x00000001
CAP_LONG_FLAG = 0x00000004
CAP_CONNECT_WITH_DB = 0x00000008
CAP_PROTOCOL_41 = 0x00000200
CAP_TRANSACTIONS = 0x00002000
CAP_SECURE_CONNECTION = 0x00008000
CAP_MULTI_STATEMENTS = 0x00010000
CAP_MULTI_RESULTS = 0x00020000
CAP_PLUGIN_AUTH = 0x00080000

CLIENT_CAPABILITIES = (
    CAP_LONG_PASSWORD
    | CAP_LONG_FLAG
    | CAP_CONNECT_WITH_DB
    | CAP_PROTOCOL_41
    | CAP_TRANSACTIONS
    | CAP_SECURE_CONNECTION
    | CAP_MULTI_STATEMENTS
    | CAP_MULTI_RESULTS
    | CAP_PLUGIN_AUTH
)

DEFAULT_MAX_PACKET = (1 << 24) - 1
CLIENT_CHARSET = 46  # utf8mb4_bin; matches what seekdb advertises in its greeting

PROTOCOL_41_GREETING = 10
EOF_PACKET = 0xFE
OK_PACKET = 0x00
ERR_PACKET = 0xFF
LOCAL_INFILE = 0xFB
AUTH_SWITCH = 0xFE
COM_QUERY = 0x03


class MysqlError(RuntimeError):
    """An error packet returned by the server."""

    def __init__(self, code, sqlstate, message):
        super().__init__(message)
        self.code = code
        self.sqlstate = sqlstate
        self.message = message

    def __str__(self):
        state = self.sqlstate or "HY000"
        return "ERROR {code} ({state}): {message}".format(
            code=self.code, state=state, message=self.message
        )


class ProtocolError(RuntimeError):
    """The server sent something this client does not understand."""


def recv_exact(sock, size):
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise EOFError("connection closed by server")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_packet(sock):
    """Read one MySQL packet, joining 16MB continuation chunks."""
    header = recv_exact(sock, 4)
    length = int.from_bytes(header[:3], "little")
    seq = header[3]
    payload = []
    while length == 0xFFFFFF:
        payload.append(recv_exact(sock, length))
        header = recv_exact(sock, 4)
        length = int.from_bytes(header[:3], "little")
        seq = header[3]
    payload.append(recv_exact(sock, length))
    return b"".join(payload), seq


def send_packet(sock, seq, payload):
    offset = 0
    while True:
        chunk = payload[offset : offset + 0xFFFFFF]
        header = struct.pack("<I", len(chunk))[:3] + bytes((seq,))
        sock.sendall(header + chunk)
        seq += 1
        offset += len(chunk)
        if len(chunk) < 0xFFFFFF:
            break
    if offset > 0 and offset % 0xFFFFFF == 0:
        # A message ending exactly on the 16MB boundary needs an empty
        # terminator packet so the server knows it is complete.
        sock.sendall(struct.pack("<I", 0)[:3] + bytes((seq,)))


def read_lenenc(data, pos):
    """Return (value, new_pos); value is None for the NULL marker (0xfb)."""
    if pos >= len(data):
        raise ProtocolError("truncated length-encoded integer")
    first = data[pos]
    if first < 251:
        return first, pos + 1
    if first == 251:
        return None, pos + 1
    if first == 252:
        need = 2
    elif first == 253:
        need = 3
    elif first == 254:
        need = 8
    else:
        raise ProtocolError("unexpected length-encoded marker 0x{:02x}".format(first))
    if pos + 1 + need > len(data):
        raise ProtocolError("truncated length-encoded integer")
    value = int.from_bytes(data[pos + 1 : pos + 1 + need], "little")
    return value, pos + 1 + need


def read_lenenc_bytes(data, pos):
    length, pos = read_lenenc(data, pos)
    if length is None:
        return None, pos
    end = pos + length
    if end > len(data):
        raise ProtocolError("truncated length-encoded string")
    return data[pos:end], end


def parse_handshake(payload):
    """Parse the server greeting packet (protocol 4.1)."""
    if not payload or payload[0] == ERR_PACKET:
        raise ProtocolError("server did not send a greeting")
    if payload[0] != PROTOCOL_41_GREETING:
        raise ProtocolError(
            "unsupported handshake protocol version {}".format(payload[0])
        )
    pos = 1
    end = payload.find(b"\x00", pos)
    if end < 0:
        raise ProtocolError("malformed greeting: unterminated server version")
    server_version = payload[pos:end]
    pos = end + 1
    conn_id = struct.unpack_from("<I", payload, pos)[0]
    pos += 4
    scramble_part1 = payload[pos : pos + 8]
    pos += 8
    pos += 1  # filler
    caps_low = struct.unpack_from("<H", payload, pos)[0]
    pos += 2
    charset = payload[pos]
    pos += 1
    status = struct.unpack_from("<H", payload, pos)[0]
    pos += 2
    caps_high = struct.unpack_from("<H", payload, pos)[0]
    pos += 2
    capabilities = caps_low | (caps_high << 16)
    auth_len = payload[pos] if capabilities & CAP_PLUGIN_AUTH else 0
    pos += 1
    pos += 10  # reserved

    second = b""
    if capabilities & CAP_PLUGIN_AUTH and auth_len > 0:
        # The server sends auth_len bytes of plugin data; 8 were already read
        # as part 1.  MySQL clients read at least 13 bytes after the reserved
        # section, trimming the trailing NUL that completes the scramble.
        second_len = max(13, auth_len - 8)
        second = payload[pos : pos + second_len]
        pos += len(second)
        if second.endswith(b"\x00") and len(second) > 1:
            second = second[:-1]
    scramble = (scramble_part1 + second)[:20]

    plugin = b"mysql_native_password"
    if capabilities & CAP_PLUGIN_AUTH and pos < len(payload):
        plugin = payload[pos:].split(b"\x00", 1)[0]

    return {
        "server_version": server_version,
        "connection_id": conn_id,
        "scramble": scramble,
        "capabilities": capabilities,
        "charset": charset,
        "status": status,
        "plugin": plugin,
    }


def build_handshake_response(greeting, user, password, database):
    capabilities = CLIENT_CAPABILITIES & greeting["capabilities"]
    scramble = greeting["scramble"]
    if not scramble:
        raise ProtocolError("server sent an empty auth scramble")
    if greeting["plugin"] != b"mysql_native_password":
        raise ProtocolError(
            "unsupported auth plugin {!r}".format(greeting["plugin"].decode("latin1"))
        )

    token = native_password_token(password, scramble)
    parts = [
        struct.pack("<I", capabilities),
        struct.pack("<I", DEFAULT_MAX_PACKET),
        bytes((CLIENT_CHARSET,)),
        b"\x00" * 23,
        user.encode("utf-8") + b"\x00",
        bytes((len(token),)) + token,
    ]
    if capabilities & CAP_CONNECT_WITH_DB:
        parts.append(database.encode("utf-8") + b"\x00")
    if capabilities & CAP_PLUGIN_AUTH:
        parts.append(b"mysql_native_password\x00")
    return b"".join(parts)


def native_password_token(password, scramble):
    """mysql_native_password auth response for the server's scramble."""
    if not password:
        return b""
    pwd = password.encode("utf-8")
    stage1 = hashlib.sha1(pwd).digest()
    stage2 = hashlib.sha1(stage1).digest()
    scrambled = hashlib.sha1(scramble + stage2).digest()
    return bytes(a ^ b for a, b in zip(stage1, scrambled))


def parse_ok(payload):
    if len(payload) < 7:
        raise ProtocolError("malformed OK packet")
    pos = 1
    affected, pos = read_lenenc(payload, pos)
    last_id, pos = read_lenenc(payload, pos)
    status = struct.unpack_from("<H", payload, pos)[0]
    pos += 2
    warnings = struct.unpack_from("<H", payload, pos)[0]
    pos += 2
    info = payload[pos:].decode("utf-8", "replace")
    return {
        "affected_rows": affected or 0,
        "last_insert_id": last_id or 0,
        "status": status,
        "warnings": warnings,
        "info": info,
    }


def parse_error(payload):
    if len(payload) < 3:
        raise ProtocolError("malformed error packet")
    code = struct.unpack_from("<H", payload, 1)[0]
    pos = 3
    sqlstate = None
    if pos < len(payload) and payload[pos : pos + 1] == b"#":
        sqlstate = payload[pos + 1 : pos + 6].decode("ascii", "replace")
        pos += 6
    message = payload[pos:].decode("utf-8", "replace")
    return MysqlError(code, sqlstate, message)


def parse_auth_switch(payload):
    """Parse an AuthSwitchRequest packet and return (plugin, scramble)."""
    if not payload or payload[0] != AUTH_SWITCH:
        raise ProtocolError("malformed auth switch request")
    pos = 1
    end = payload.find(b"\x00", pos)
    if end < 0:
        raise ProtocolError("malformed auth switch request")
    plugin = payload[pos:end]
    data = payload[end + 1 :]
    if data.endswith(b"\x00"):
        data = data[:-1]
    return plugin, data


def parse_results(sock):
    """Read a resultset (or OK/error) and return (columns, rows, ok)."""
    payload, _ = read_packet(sock)
    if not payload:
        raise ProtocolError("empty response packet")
    first = payload[0]
    if first == OK_PACKET:
        return None, None, parse_ok(payload)
    if first == ERR_PACKET:
        raise parse_error(payload)
    if first == LOCAL_INFILE:
        raise ProtocolError("LOAD DATA LOCAL INFILE is not supported")

    column_count, pos = read_lenenc(payload, 0)
    columns = []
    for _ in range(column_count):
        field_payload, _ = read_packet(sock)
        name = parse_column_name(field_payload)
        columns.append(name)

    # EOF after column definitions.
    eof, _ = read_packet(sock)
    if eof and eof[0] == EOF_PACKET:
        pass
    elif eof and eof[0] == OK_PACKET:
        return columns, [], parse_ok(eof)

    rows = []
    while True:
        row_payload, _ = read_packet(sock)
        if not row_payload:
            raise ProtocolError("empty row packet")
        if (row_payload[0] == EOF_PACKET and len(row_payload) < 9) or (
            row_payload[0] == OK_PACKET and len(row_payload) < 9
        ):
            return columns, rows, None
        rows.append(parse_row(row_payload, column_count))
        if len(rows) > 1_000_000:
            raise ProtocolError("result set too large")


def parse_column_name(payload):
    """Extract the field name from a column definition packet."""
    pos = 0
    entries = []
    for _ in range(6):
        value, pos = read_lenenc_bytes(payload, pos)
        entries.append(value)
    # Fixed-length part: 0x0c, charset(2), column_length(4), type(1),
    # flags(2), decimals(1), filler(2), then one lenenc default value.
    if pos < len(payload) and payload[pos] == 0x0C:
        pos += 1
        pos += 2 + 4 + 1 + 2 + 1 + 2
        if pos < len(payload):  # columns without a default omit the value
            _, pos = read_lenenc_bytes(payload, pos)
    name = entries[4]
    return name.decode("utf-8", "replace") if name is not None else ""


def parse_row(payload, column_count):
    pos = 0
    values = []
    for _ in range(column_count):
        value, pos = read_lenenc_bytes(payload, pos)
        if value is None:
            values.append(None)
        else:
            values.append(value.decode("utf-8", "replace"))
    return values


def execute_statement(sock, statement):
    send_packet(sock, 0, bytes((COM_QUERY,)) + statement.encode("utf-8"))
    return parse_results(sock)


def split_statement_parts(sql):
    """Split SQL text on top-level semicolons, honoring quotes/comments.

    Returns ``(complete_statements, trailing_partial)``.
    """
    statements = []
    buf = []
    quote = None
    line_comment = False
    block_comment = False
    i = 0
    while i < len(sql):
        char = sql[i]
        if line_comment:
            if char == "\n":
                line_comment = False
            i += 1
            continue
        if block_comment:
            if char == "*" and i + 1 < len(sql) and sql[i + 1] == "/":
                block_comment = False
                i += 2
                continue
            i += 1
            continue
        if quote:
            buf.append(char)
            if char == quote:
                if i + 1 < len(sql) and sql[i + 1] == quote:
                    buf.append(sql[i + 1])
                    i += 2
                    continue
                quote = None
            elif char == "\\" and quote in ("'", '"'):
                if i + 1 < len(sql):
                    buf.append(sql[i + 1])
                    i += 2
                    continue
            i += 1
            continue
        if char in ("'", '"', "`"):
            quote = char
            buf.append(char)
        elif (
            char == "-"
            and i + 1 < len(sql)
            and sql[i + 1] == "-"
            and (i + 2 >= len(sql) or sql[i + 2] in " \t\n\r\x0b\x0c")
        ):
            line_comment = True
            i += 2
            continue
        elif char == "#":
            line_comment = True
            i += 1
            continue
        elif char == "/" and i + 1 < len(sql) and sql[i + 1] == "*":
            block_comment = True
            i += 2
            continue
        elif char == ";":
            stmt = "".join(buf).strip()
            if stmt:
                statements.append(stmt)
            buf = []
        else:
            buf.append(char)
        i += 1
    rest = "".join(buf).strip()
    return statements, rest


def split_statements(sql):
    """Split SQL text into complete statements on top-level semicolons."""
    statements, partial = split_statement_parts(sql)
    if partial:
        statements.append(partial)
    return statements


def char_width(char):
    return 2 if unicodedata.east_asian_width(char) in ("W", "F") else 1


def display_width(value):
    return sum(char_width(ch) for ch in str(value))


def pad_text(text, width):
    return text + " " * (width - display_width(text))


def format_value(value):
    return "NULL" if value is None else str(value)


def clip_to_width(text, width):
    """Truncate a cell to fit width (wide characters count as two cells)."""
    if display_width(text) <= width:
        return text
    limit = max(1, width - 3)
    used = 0
    kept = []
    for char in text:
        if used + char_width(char) > limit:
            break
        kept.append(char)
        used += char_width(char)
    return "".join(kept) + "..."


def render_table(columns, rows, max_width):
    headers = columns
    cells = [[format_value(v) for v in row] for row in rows]
    widths = [display_width(h) for h in headers]
    for row in cells:
        for idx, value in enumerate(row):
            widths[idx] = max(widths[idx], display_width(value))
    cap_widths = [min(w, max_width) for w in widths]
    headers = [
        clip_to_width(h, w) for h, w in zip(headers, cap_widths)
    ]
    cells = [
        [clip_to_width(v, w) for v, w in zip(row, cap_widths)]
        for row in cells
    ]
    lines = []
    lines.append("+".join("-" * (w + 2) for w in cap_widths))
    lines.append(
        "| "
        + " | ".join(pad_text(h, w) for h, w in zip(headers, cap_widths))
        + " |"
    )
    lines.append("+".join("-" * (w + 2) for w in cap_widths))
    for row in cells:
        lines.append(
            "| "
            + " | ".join(pad_text(v, w) for v, w in zip(row, cap_widths))
            + " |"
        )
    lines.append("+".join("-" * (w + 2) for w in cap_widths))
    return "\n".join(lines)


def render_batch(columns, rows):
    lines = ["\t".join(columns)]
    for row in rows:
        lines.append("\t".join("NULL" if v is None else str(v) for v in row))
    return "\n".join(lines)


def render_vertical(columns, rows):
    lines = []
    for index, row in enumerate(rows, 1):
        lines.append("********** {}. row **********".format(index))
        for name, value in zip(columns, row):
            lines.append("{}: {}".format(name, format_value(value)))
    if not rows:
        lines.append("Empty set")
    return "\n".join(lines)


def print_result(columns, rows, ok, args):
    if ok is not None:
        print(
            "Query OK, {} row(s) affected (warnings: {})".format(
                ok["affected_rows"], ok["warnings"]
            )
        )
        return
    if columns is None:
        return
    if args.vertical:
        print(render_vertical(columns, rows))
    elif args.batch:
        print(render_batch(columns, rows))
        return
    else:
        print(render_table(columns, rows, args.max_width))
    print("{} row(s) in set".format(len(rows)))


def discover_socket(args):
    """Find the embedded database's local SQL endpoint."""
    if args.socket:
        return args.socket
    if not args.data_dir:
        candidates = ["./seekdb.db", "."]
    else:
        candidates = [args.data_dir]
    for directory in candidates:
        sock_path = os.path.join(directory, "run", "sql.sock")
        if os.path.exists(sock_path):
            return sock_path
    return None


def open_client_lock(data_dir):
    """Hold a shared lock on run/seekdb.clients while connected.

    The embedded observer exits once no client remains, so this keeps the
    database alive for the duration of a CLI session.
    """
    if os.name == "nt" or not data_dir:
        return None
    try:
        import fcntl
    except ImportError:
        return None
    lock_path = os.path.join(data_dir, "run", "seekdb.clients")
    fd = None
    try:
        fd = os.open(lock_path, os.O_RDWR | os.O_CREAT, 0o644)
        fcntl.flock(fd, fcntl.LOCK_SH)
    except (OSError, IOError):
        if fd is not None:
            try:
                os.close(fd)
            except OSError:
                pass
        return None
    return fd


def connect(args):
    endpoint = None
    sock = None
    lock_fd = None

    if args.host:
        endpoint = (args.host, args.port or 2881)
    else:
        socket_path = discover_socket(args)
        if socket_path is None:
            raise ProtocolError(
                "no seekdb SQL socket found; is the embedded database running? "
                "Pass --socket PATH, or --host/--port for server mode."
            )
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(socket_path)
        data_dir = args.data_dir or os.path.dirname(os.path.dirname(socket_path))
        if data_dir != ".":
            lock_fd = open_client_lock(data_dir)
        # fall back to locking from the directory of the discovered socket
        if lock_fd is None:
            lock_fd = open_client_lock(os.path.dirname(os.path.dirname(socket_path)))
        endpoint = socket_path

    if sock is None:
        sock = socket.create_connection(endpoint, timeout=args.timeout)
    else:
        sock.settimeout(args.timeout)

    greeting_payload, _ = read_packet(sock)
    greeting = parse_handshake(greeting_payload)
    response = build_handshake_response(
        greeting, args.user, args.password, args.database
    )
    send_packet(sock, 1, response)
    auth_result, auth_seq = read_packet(sock)
    while auth_result and auth_result[0] == AUTH_SWITCH:
        plugin, scramble = parse_auth_switch(auth_result)
        if plugin == b"mysql_native_password":
            token = native_password_token(args.password, scramble[:20])
            send_packet(sock, auth_seq + 1, token)
        elif plugin == b"mysql_clear_password":
            if args.host:
                raise ProtocolError(
                    "mysql_clear_password auth is only allowed over a local socket"
                )
            send_packet(sock, auth_seq + 1, args.password.encode("utf-8") + b"\x00")
        else:
            raise ProtocolError(
                "unsupported auth plugin {!r}".format(plugin.decode("latin1"))
            )
        auth_result, auth_seq = read_packet(sock)
    if auth_result and auth_result[0] == ERR_PACKET:
        raise parse_error(auth_result)
    if not auth_result or auth_result[0] != OK_PACKET:
        raise ProtocolError("unexpected authentication response")
    return sock, lock_fd


def run_statements(args, statements):
    try:
        sock, lock_fd = connect(args)
    except KeyboardInterrupt:
        print()
        return 130
    except Exception as exc:
        print("seekdb-cli: {}".format(exc), file=sys.stderr)
        return 1
    try:
        failed = False
        for statement in statements:
            try:
                columns, rows, ok = execute_statement(sock, statement)
                print_result(columns, rows, ok, args)
            except MysqlError as exc:
                print(str(exc), file=sys.stderr)
                failed = True
            except Exception as exc:
                print("seekdb-cli: {}".format(exc), file=sys.stderr)
                failed = True
                break
            except KeyboardInterrupt:
                print()
                return 130
        return 1 if failed else 0
    finally:
        try:
            sock.close()
        finally:
            if lock_fd is not None:
                try:
                    os.close(lock_fd)
                except OSError:
                    pass


def build_parser():
    parser = argparse.ArgumentParser(
        prog="seekdb-cli",
        description="Execute SQL against a running embedded seekdb database.",
        add_help=False,
    )
    parser.add_argument("--help", action="help")
    parser.add_argument(
        "--data-dir",
        "-d",
        default=None,
        help="embedded database directory (default: ./seekdb.db, else cwd)",
    )
    parser.add_argument(
        "--socket", "-S", default=None, help="path to the local SQL socket"
    )
    parser.add_argument(
        "--host", "-h", default=None, help="server host (TCP mode; default none)"
    )
    parser.add_argument(
        "--port", "-P", type=int, default=None, help="server port (default: 2881)"
    )
    parser.add_argument("--user", "-u", default="root", help="user name (default: root)")
    parser.add_argument(
        "--password",
        "-p",
        default=None,
        help="password (default: $SEEKDB_PASSWORD or empty)",
    )
    parser.add_argument(
        "--database", dest="database", default="test", help="database (default: test)"
    )
    parser.add_argument(
        "--execute", "-e", default=None, help="execute one statement/batch and exit"
    )
    parser.add_argument(
        "--batch", "-B", action="store_true", help="batch output: tab-separated"
    )
    parser.add_argument(
        "--vertical", "-E", action="store_true", help="vertical output (one value per line)"
    )
    parser.add_argument(
        "--max-width", type=int, default=100, help="max display width for table cells"
    )
    parser.add_argument(
        "--timeout", type=float, default=30.0, help="connection/query timeout seconds"
    )
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.password is None:
        args.password = os.environ.get("SEEKDB_PASSWORD", "")
    args.interactive = False

    if args.execute is not None:
        statements = split_statements(args.execute)
        if not statements:
            print("empty statement", file=sys.stderr)
            return 1
        return run_statements(args, statements)

    if not sys.stdin.isatty():
        sql = sys.stdin.read()
        statements = split_statements(sql)
        if not statements:
            return 0
        return run_statements(args, statements)

    args.interactive = True
    try:
        sock, lock_fd = connect(args)
    except Exception as exc:
        print("seekdb-cli: {}".format(exc), file=sys.stderr)
        return 1
    try:
        buffer_lines = []
        while True:
            try:
                line = input("    -> " if buffer_lines else "seekdb> ")
            except (EOFError, KeyboardInterrupt):
                print()
                break
            trimmed = line.strip()
            if trimmed.lower() in ("exit", "quit", "\\q"):
                break
            buffer_lines.append(line)
            text = "\n".join(buffer_lines)
            if text.strip():
                executed, partial = split_statement_parts(text)
                if executed:
                    for stmt in executed:
                        try:
                            columns, rows, ok = execute_statement(sock, stmt)
                            print_result(columns, rows, ok, args)
                        except MysqlError as exc:
                            print(str(exc), file=sys.stderr)
                        except Exception as exc:
                            print("seekdb-cli: {}".format(exc), file=sys.stderr)
                        except KeyboardInterrupt:
                            print()
                            return 130
                buffer_lines = [partial] if partial else []
    finally:
        try:
            sock.close()
        finally:
            if lock_fd is not None:
                try:
                    os.close(lock_fd)
                except OSError:
                    pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
