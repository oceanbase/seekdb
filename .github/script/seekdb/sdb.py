#!/usr/bin/env python3
"""Start, wait for, stop, and destroy a local seekdb instance."""

from __future__ import print_function

import argparse
import os
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


def _error(message):
    print("[sdb][ERROR] {}".format(message), file=sys.stderr)


def _base_dir(value):
    return Path(os.path.abspath(os.path.expanduser(value)))


def _expand_command(value):
    return os.path.expanduser(value)


def build_start_command(args, base_dir):
    command = [
        _expand_command(args.binary),
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
        log_dir.mkdir(parents=True, exist_ok=True)
        with (log_dir / "console.log").open("ab") as console:
            process = spawn_detached(command, base_dir, console)
    except (OSError, RuntimeError) as exc:
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
    command = build_ready_command(args)
    deadline = time.monotonic() + args.timeout

    while time.monotonic() < deadline:
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


def terminate_pid(pid):
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
        pid = int(pid_text)
        if pid <= 0:
            raise ValueError
    except ValueError:
        _error("invalid pid in {}: {!r}".format(pid_file, pid_text))
        return 1

    try:
        terminate_pid(pid)
    except (OSError, RuntimeError) as exc:
        _error("failed to stop seekdb pid={}: {}".format(pid, exc))
        return 1

    if not getattr(args, "quiet", False):
        print("stopped")
    return 0


def validate_destroy_target(base_dir):
    if base_dir.is_symlink():
        raise ValueError("base-dir must not be a symbolic link")

    resolved = base_dir.resolve()
    filesystem_root = Path(resolved.anchor)
    home_dir = Path.home().resolve()
    current_dir = Path.cwd().resolve()
    if resolved == filesystem_root:
        raise ValueError("refusing to remove the filesystem root")
    if resolved == home_dir or resolved in home_dir.parents:
        raise ValueError("refusing to remove HOME or one of its parents")
    if resolved == current_dir or resolved in current_dir.parents:
        raise ValueError("refusing to remove the current directory or one of its parents")


def command_destroy(args):
    base_dir = _base_dir(args.base_dir)
    try:
        validate_destroy_target(base_dir)
    except (OSError, ValueError) as exc:
        _error("unsafe base-dir {}: {}".format(base_dir, exc))
        return 1

    if not base_dir.exists():
        print("destroyed")
        return 0
    if not base_dir.is_dir():
        _error("base-dir is not a directory: {}".format(base_dir))
        return 1

    stop_args = argparse.Namespace(base_dir=str(base_dir), quiet=True)
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

    ready = subparsers.add_parser("wait-ready", help="wait until SELECT 1 succeeds")
    ready.add_argument("--client", default="obclient", help="SQL client executable")
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
