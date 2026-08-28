#!/usr/bin/env python3
"""Start, wait for, stop, and destroy a local seekdb instance."""

from __future__ import print_function

import argparse
import os
import shlex
import signal
import shutil
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_PORT = 2881
DEFAULT_READY_TIMEOUT = 180.0
DEFAULT_READY_INTERVAL = 1.0
CLIENT_ATTEMPT_TIMEOUT = 5.0
STOP_TIMEOUT = 20.0
KILL_TIMEOUT = 5.0
PROCESS_QUERY_TIMEOUT = 5.0
INSTANCE_MARKER_NAME = ".sdb-instance"
INSTANCE_MARKER_HEADER = "seekdb-instance-v1"


def _error(message):
    print("[sdb][ERROR] {}".format(message), file=sys.stderr)


def _warning(message):
    print("[sdb][WARN] {}".format(message), file=sys.stderr)


def _base_dir(value):
    path = Path(os.path.abspath(os.path.expanduser(value)))
    return path.parent.resolve() / path.name


def _expand_command(value):
    return os.path.expanduser(value)


def _executable_path(value):
    return Path(os.path.abspath(_expand_command(value))).resolve()


def _instance_marker(base_dir):
    return base_dir / INSTANCE_MARKER_NAME


def validate_base_dir(base_dir):
    if base_dir.is_symlink():
        raise ValueError("base-dir must not be a symbolic link")

    resolved = base_dir.resolve()
    filesystem_root = Path(resolved.anchor)
    home_dir = Path.home().resolve()
    current_dir = Path.cwd().resolve()
    if resolved == filesystem_root:
        raise ValueError("filesystem root is not allowed as base-dir")
    if resolved == home_dir or resolved in home_dir.parents:
        raise ValueError("HOME or one of its parents is not allowed as base-dir")
    if resolved == current_dir or resolved in current_dir.parents:
        raise ValueError(
            "current directory or one of its parents is not allowed as base-dir"
        )


def read_instance_binary(base_dir):
    marker = _instance_marker(base_dir)
    if marker.is_symlink():
        raise ValueError("instance marker must not be a symbolic link")
    try:
        lines = marker.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError:
        raise ValueError("base-dir is not managed by sdb (missing {})".format(marker))
    if len(lines) != 2 or lines[0] != INSTANCE_MARKER_HEADER or not lines[1]:
        raise ValueError("invalid instance marker: {}".format(marker))
    binary = Path(lines[1])
    if not binary.is_absolute():
        raise ValueError("invalid instance marker: {}".format(marker))
    return binary.resolve()


def prepare_instance_directory(base_dir, binary):
    validate_base_dir(base_dir)
    binary = binary.resolve()
    marker = _instance_marker(base_dir)
    write_marker = True
    if base_dir.exists():
        if not base_dir.is_dir():
            raise ValueError("base-dir is not a directory: {}".format(base_dir))
        if marker.is_symlink():
            raise ValueError("instance marker must not be a symbolic link")
        if marker.exists():
            existing_binary = read_instance_binary(base_dir)
            if existing_binary != binary:
                raise ValueError(
                    "base-dir is managed by {}, not {}".format(
                        existing_binary, binary
                    )
                )
            write_marker = False
        elif any(base_dir.iterdir()):
            raise ValueError(
                "refusing to use non-empty base-dir without {}".format(
                    INSTANCE_MARKER_NAME
                )
            )
    else:
        base_dir.mkdir(parents=True)

    if write_marker:
        marker.write_text(
            "{}\n{}\n".format(INSTANCE_MARKER_HEADER, binary), encoding="utf-8"
        )


def build_start_command(args, base_dir):
    command = [
        str(_executable_path(args.binary)),
        "--base-dir={}".format(base_dir),
        "--port={}".format(args.port),
    ]
    if args.nodaemon:
        command.append("--nodaemon")
    for parameter in args.parameter:
        command.extend(("--parameter", parameter))
    return command


def spawn_detached(command, base_dir, console):
    if os.name == "nt":
        raise RuntimeError("Windows is not supported yet")
    return subprocess.Popen(
        command,
        cwd=str(base_dir),
        stdout=console,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )


def command_start(args):
    base_dir = _base_dir(args.base_dir)
    log_dir = base_dir / "log"
    command = build_start_command(args, base_dir)

    try:
        prepare_instance_directory(base_dir, Path(command[0]))
        log_dir.mkdir(parents=True, exist_ok=True)
        with (log_dir / "console.log").open("ab") as console:
            process = spawn_detached(command, base_dir, console)
    except (OSError, RuntimeError, ValueError) as exc:
        _error("failed to start seekdb: {}".format(exc))
        return 1

    print("started pid={}".format(process.pid))
    return 0


def build_ready_command(args):
    return [
        _expand_command(args.client),
        "-h",
        args.host,
        "-P",
        str(args.port),
        "-u{}".format(args.user),
        "-A",
        "-N",
        "-s",
        "-e",
        "select 1",
    ]


def command_wait_ready(args):
    base_dir = _base_dir(args.base_dir)
    command = build_ready_command(args)
    deadline = time.monotonic() + args.timeout

    try:
        expected_binary = read_instance_binary(base_dir)
    except (OSError, ValueError) as exc:
        _error("unsafe base-dir {}: {}".format(base_dir, exc))
        return 1

    while time.monotonic() < deadline:
        try:
            managed_pid = inspect_instance_process(base_dir, expected_binary)
        except (OSError, RuntimeError) as exc:
            _error("managed seekdb process is unavailable: {}".format(exc))
            return 1

        remaining = deadline - time.monotonic()
        attempt_timeout = min(CLIENT_ATTEMPT_TIMEOUT, max(remaining, 0.001))
        try:
            result = subprocess.run(
                command,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=attempt_timeout,
                check=False,
            )
        except subprocess.TimeoutExpired:
            result = None
        except OSError as exc:
            _error("failed to run {}: {}".format(args.client, exc))
            return 1

        if result is not None and result.returncode == 0:
            if managed_pid is None:
                try:
                    managed_pid = inspect_instance_process(base_dir, expected_binary)
                except (OSError, RuntimeError) as exc:
                    _error("managed seekdb process is unavailable: {}".format(exc))
                    return 1
            if managed_pid is None:
                _error(
                    "endpoint {}:{} responded without a managed seekdb process".format(
                        args.host, args.port
                    )
                )
                return 1
            print("ready")
            return 0

        remaining = deadline - time.monotonic()
        if remaining > 0:
            time.sleep(min(args.interval, remaining))

    _error(
        "seekdb is not ready at {}:{} within {} seconds".format(
            args.host, args.port, args.timeout
        )
    )
    return 1


def process_exists(pid):
    if os.name != "nt":
        try:
            waited_pid, _ = os.waitpid(pid, os.WNOHANG)
            if waited_pid == pid:
                return False
        except ChildProcessError:
            pass

    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def wait_process_exit(pid, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not process_exists(pid):
            return True
        time.sleep(0.2)
    return not process_exists(pid)


def read_process_arguments(pid):
    if os.name == "nt":
        raise RuntimeError("Windows is not supported yet")

    if sys.platform.startswith("linux"):
        try:
            command_line = Path("/proc/{}/cmdline".format(pid)).read_bytes()
        except FileNotFoundError:
            return None
        return [
            argument.decode("utf-8", "replace")
            for argument in command_line.split(b"\0")
            if argument
        ]

    try:
        result = subprocess.run(
            ["ps", "-p", str(pid), "-ww", "-o", "args="],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=PROCESS_QUERY_TIMEOUT,
            universal_newlines=True,
            check=False,
        )
    except FileNotFoundError:
        raise RuntimeError("ps is required to inspect the seekdb process")
    if result.returncode != 0 or not result.stdout.strip():
        return None
    try:
        return shlex.split(result.stdout.strip())
    except ValueError as exc:
        raise RuntimeError("failed to parse process {} arguments: {}".format(pid, exc))


def process_matches_instance(pid, base_dir, expected_binary):
    arguments = read_process_arguments(pid)
    if not arguments:
        return False

    if sys.platform.startswith("linux"):
        try:
            executable = Path(os.readlink("/proc/{}/exe".format(pid))).resolve()
        except FileNotFoundError:
            return False
    else:
        executable = _executable_path(arguments[0])

    expected_base_dir = "--base-dir={}".format(base_dir)
    return executable == expected_binary.resolve() and expected_base_dir in arguments


def inspect_instance_process(base_dir, expected_binary):
    pid_file = base_dir / "run" / "seekdb.pid"
    try:
        pid_text = pid_file.read_text(encoding="utf-8").strip()
    except FileNotFoundError:
        return None
    except OSError as exc:
        raise RuntimeError("failed to read {}: {}".format(pid_file, exc))

    try:
        pid = int(pid_text)
        if pid <= 0:
            raise ValueError
    except ValueError:
        raise RuntimeError("invalid pid in {}: {!r}".format(pid_file, pid_text))

    if not process_exists(pid):
        raise RuntimeError("seekdb pid={} is not running".format(pid))
    if not process_matches_instance(pid, base_dir, expected_binary):
        raise RuntimeError("pid={} does not match this seekdb instance".format(pid))
    return pid


def remove_pid_file(pid_file):
    try:
        pid_file.unlink()
    except FileNotFoundError:
        pass


def terminate_pid(pid, base_dir, expected_binary):
    if os.name == "nt":
        raise RuntimeError("Windows is not supported yet")

    if not process_exists(pid):
        return

    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    if wait_process_exit(pid, STOP_TIMEOUT):
        return

    try:
        matches_instance = process_matches_instance(pid, base_dir, expected_binary)
    except (OSError, RuntimeError) as exc:
        raise RuntimeError(
            "failed to revalidate process {} before SIGKILL: {}".format(pid, exc)
        )
    if not matches_instance:
        _warning(
            "not sending SIGKILL to pid={}: process no longer matches this instance".format(
                pid
            )
        )
        return

    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        return
    if not wait_process_exit(pid, KILL_TIMEOUT):
        raise RuntimeError("process {} did not exit".format(pid))


def command_stop(args):
    base_dir = _base_dir(args.base_dir)
    pid_file = base_dir / "run" / "seekdb.pid"

    try:
        pid_text = pid_file.read_text(encoding="utf-8").strip()
    except FileNotFoundError:
        if not getattr(args, "quiet", False):
            print("stopped")
        return 0
    except OSError as exc:
        _error("failed to read {}: {}".format(pid_file, exc))
        return 1

    try:
        expected_binary = read_instance_binary(base_dir)
    except (OSError, ValueError) as exc:
        _error("unsafe base-dir {}: {}".format(base_dir, exc))
        return 1

    try:
        pid = int(pid_text)
        if pid <= 0:
            raise ValueError
    except ValueError:
        _warning("removing invalid pid file {}: {!r}".format(pid_file, pid_text))
        remove_pid_file(pid_file)
        if not getattr(args, "quiet", False):
            print("stopped")
        return 0

    if not process_exists(pid):
        remove_pid_file(pid_file)
        if not getattr(args, "quiet", False):
            print("stopped")
        return 0

    try:
        matches_instance = process_matches_instance(pid, base_dir, expected_binary)
    except (OSError, RuntimeError) as exc:
        _error("failed to inspect seekdb pid={}: {}".format(pid, exc))
        return 1
    if not matches_instance:
        if getattr(args, "require_match", False):
            _error(
                "refusing to stop live pid={}: process does not match this instance".format(
                    pid
                )
            )
            return 1
        _warning(
            "ignoring stale pid {} from {}: process does not match this instance".format(
                pid, pid_file
            )
        )
        remove_pid_file(pid_file)
        if not getattr(args, "quiet", False):
            print("stopped")
        return 0

    try:
        terminate_pid(pid, base_dir, expected_binary)
    except (OSError, RuntimeError) as exc:
        _error("failed to stop seekdb pid={}: {}".format(pid, exc))
        return 1

    remove_pid_file(pid_file)

    if not getattr(args, "quiet", False):
        print("stopped")
    return 0


def command_destroy(args):
    base_dir = _base_dir(args.base_dir)
    try:
        validate_base_dir(base_dir)
    except (OSError, ValueError) as exc:
        _error("unsafe base-dir {}: {}".format(base_dir, exc))
        return 1

    if not base_dir.exists():
        print("destroyed")
        return 0
    if not base_dir.is_dir():
        _error("base-dir is not a directory: {}".format(base_dir))
        return 1

    try:
        read_instance_binary(base_dir)
    except (OSError, ValueError) as exc:
        _error("unsafe base-dir {}: {}".format(base_dir, exc))
        return 1

    stop_args = argparse.Namespace(
        base_dir=str(base_dir), quiet=True, require_match=True
    )
    if command_stop(stop_args) != 0:
        return 1

    try:
        shutil.rmtree(str(base_dir))
    except OSError as exc:
        _error("failed to remove {}: {}".format(base_dir, exc))
        return 1

    print("destroyed")
    return 0


def positive_float(value):
    number = float(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return number


def create_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command")

    start = subparsers.add_parser("start", help="start seekdb and return immediately")
    start.add_argument("--binary", required=True, help="seekdb executable")
    start.add_argument("--base-dir", required=True, help="seekdb base directory")
    start.add_argument("--port", type=int, default=DEFAULT_PORT)
    start.add_argument(
        "--parameter",
        action="append",
        default=[],
        help="seekdb parameter value; may be specified more than once",
    )
    start.add_argument(
        "--nodaemon",
        action="store_true",
        help="pass --nodaemon to seekdb",
    )
    start.set_defaults(handler=command_start)

    ready = subparsers.add_parser(
        "wait-ready", help="wait until the managed seekdb accepts SELECT 1"
    )
    ready.add_argument("--client", default="obclient", help="SQL client executable")
    ready.add_argument("--base-dir", required=True, help="seekdb base directory")
    ready.add_argument("--host", default="127.0.0.1")
    ready.add_argument("--port", type=int, default=DEFAULT_PORT)
    ready.add_argument("--user", default="root")
    ready.add_argument("--timeout", type=positive_float, default=DEFAULT_READY_TIMEOUT)
    ready.add_argument("--interval", type=positive_float, default=DEFAULT_READY_INTERVAL)
    ready.set_defaults(handler=command_wait_ready)

    stop = subparsers.add_parser("stop", help="stop seekdb using its pid file")
    stop.add_argument("--base-dir", required=True, help="seekdb base directory")
    stop.set_defaults(handler=command_stop)

    destroy = subparsers.add_parser(
        "destroy", help="stop seekdb and remove its base directory"
    )
    destroy.add_argument("--base-dir", required=True, help="seekdb base directory")
    destroy.set_defaults(handler=command_destroy)

    return parser


def main(argv=None):
    parser = create_parser()
    args = parser.parse_args(argv)
    if not hasattr(args, "handler"):
        parser.print_usage(sys.stderr)
        return 2
    return args.handler(args)


if __name__ == "__main__":
    sys.exit(main())
