import argparse
from contextlib import ExitStack
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
from importlib.metadata import version
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import traceback

import pymysql


READY_MARKER = b"[OBSERVER_NOTICE] observer start service"
BOOTSTRAP_MARKERS = {
    "init_global_stat": b":init_global_stat execute success",
    "bootstrap_complete": b"execute_bootstrap finished(ret=0)",
}
CLIENT_EXIT_MARKER = b"no clients remaining, exiting"


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class Overlapped(ctypes.Structure):
    _fields_ = [
        ("Internal", ctypes.c_size_t),
        ("InternalHigh", ctypes.c_size_t),
        ("Offset", wintypes.DWORD),
        ("OffsetHigh", wintypes.DWORD),
        ("hEvent", wintypes.HANDLE),
    ]


def windows_api():
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.CreateFileW.argtypes = [
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    ]
    api.CreateFileW.restype = wintypes.HANDLE
    api.LockFileEx.argtypes = [
        wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
        wintypes.DWORD, ctypes.POINTER(Overlapped),
    ]
    api.LockFileEx.restype = wintypes.BOOL
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    api.CloseHandle.restype = wintypes.BOOL
    api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenProcess.restype = wintypes.HANDLE
    api.SetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.c_size_t]
    api.SetProcessAffinityMask.restype = wintypes.BOOL
    api.GetProcessAffinityMask.argtypes = [
        wintypes.HANDLE, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t),
    ]
    api.GetProcessAffinityMask.restype = wintypes.BOOL
    return api


def set_cpu_affinity(api, pid, mask):
    handle = api.OpenProcess(0x0200 | 0x1000, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        original = ctypes.c_size_t()
        system = ctypes.c_size_t()
        if not api.GetProcessAffinityMask(handle, ctypes.byref(original), ctypes.byref(system)):
            raise ctypes.WinError(ctypes.get_last_error())
        require((mask & ~system.value) == 0, f"CPU affinity mask {hex(mask)} exceeds system mask {hex(system.value)}")
        if not api.SetProcessAffinityMask(handle, mask):
            raise ctypes.WinError(ctypes.get_last_error())
        applied = ctypes.c_size_t()
        if not api.GetProcessAffinityMask(handle, ctypes.byref(applied), ctypes.byref(system)):
            raise ctypes.WinError(ctypes.get_last_error())
        require(applied.value == mask, "Applied CPU affinity differs from the requested mask")
        return {
            "requested_mask": hex(mask), "original_mask": hex(original.value),
            "applied_mask": hex(applied.value), "system_mask": hex(system.value),
        }
    finally:
        if not api.CloseHandle(handle):
            raise ctypes.WinError(ctypes.get_last_error())


def describe_error(error):
    return {
        "type": type(error).__name__, "message": str(error),
        "traceback": "".join(traceback.format_exception(type(error), error, error.__traceback__)),
    }


def prepare_runtime(args, trial, original_digest):
    binary = args.binary
    runtime = {
        "copied": args.copy_runtime_per_trial, "binary": str(binary),
        "copied_files": [], "copy_elapsed_seconds": 0,
    }
    trial["runtime"] = runtime
    if args.copy_runtime_per_trial:
        destination = Path(trial["instance_dir"]).parent / "runtime"
        destination.mkdir()
        binary = destination / args.binary.name
        runtime["binary"] = str(binary)
        started = time.monotonic()
        files = [args.binary] + sorted(
            path for path in args.binary.parent.glob("*.dll") if path.is_file()
        )
        for source in files:
            target = destination / source.name
            shutil.copy2(source, target)
            runtime["copied_files"].append({
                "source": str(source), "destination": str(target),
                "size_bytes": target.stat().st_size,
            })
        runtime["copy_elapsed_seconds"] = time.monotonic() - started
    with binary.open("rb") as stream:
        runtime["binary_sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
    require(runtime["binary_sha256"] == original_digest, "Trial executable SHA-256 differs from the input executable")
    return binary


class EmbeddedClientLock:
    def __init__(self, api, path):
        self.api = api
        self.path = path
        self.handle = None
        self.overlapped = Overlapped()

    def acquire(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.handle = self.api.CreateFileW(
            str(self.path), 0xC0000000, 0x00000003, None, 4, 0x80, None,
        )
        if self.handle == ctypes.c_void_p(-1).value:
            self.handle = None
            raise ctypes.WinError(ctypes.get_last_error())
        # 与服务端独占锁覆盖同一范围，客户端持有共享锁。
        if not self.api.LockFileEx(
            self.handle, 0x00000001, 0, 0xFFFFFFFF, 0xFFFFFFFF,
            ctypes.byref(self.overlapped),
        ):
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle is not None:
            handle = self.handle
            self.handle = None
            if not self.api.CloseHandle(handle):
                raise ctypes.WinError(ctypes.get_last_error())


class LogWatcher:
    def __init__(self, directory, markers):
        self.directory = directory
        self.markers = markers
        self.offsets = {}
        self.tails = {}
        self.found = {}
        self.started = time.monotonic()
        self.tail_size = max(map(len, markers.values())) - 1
        for path in self.files():
            stat = path.stat()
            self.offsets[(stat.st_dev, stat.st_ino)] = stat.st_size

    def files(self):
        return sorted(
            path for path in self.directory.glob("seekdb.log*")
            if path.is_file() and ".wf" not in path.name
        )

    def update(self):
        for path in self.files():
            stat = path.stat()
            key = (stat.st_dev, stat.st_ino)
            offset = self.offsets.get(key, 0)
            if stat.st_size < offset:
                offset = 0
                self.tails.pop(key, None)
            with path.open("rb") as stream:
                stream.seek(offset)
                data = self.tails.get(key, b"") + stream.read()
                self.offsets[key] = stream.tell()
            for name, marker in self.markers.items():
                if name not in self.found and marker in data:
                    self.found[name] = {
                        "file": str(path),
                        "marker": marker.decode("ascii"),
                        "observed_after_seconds": time.monotonic() - self.started,
                    }
            self.tails[key] = data[-self.tail_size:]


class Recorder:
    def __init__(self, path, report):
        self.path = path
        self.report = report
        self.active_stage = None
        self.stage_started = None

    def write(self):
        self.path.write_text(
            json.dumps(self.report, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )

    def phase(self, trial, name, operation):
        print(f"trial={trial['trial']} phase={name}", flush=True)
        stage = {"name": name, "status": "running", "started_at": timestamp()}
        trial["stages"].append(stage)
        self.active_stage = stage
        self.stage_started = time.monotonic()
        self.write()
        result = operation()
        stage.update(status="passed", elapsed_seconds=time.monotonic() - self.stage_started)
        self.active_stage = None
        self.write()
        return result

    def finish(self, error):
        if error is not None:
            self.report["status"] = "failed"
            self.report["error"] = describe_error(error)
            if self.active_stage is not None:
                self.active_stage.update(
                    status="failed", elapsed_seconds=time.monotonic() - self.stage_started,
                )
        else:
            self.report["status"] = "passed"
        self.report["finished_at"] = timestamp()
        self.write()


class ServerRun:
    def __init__(self, api, args, binary, directory, name, parameters):
        self.api = api
        self.args = args
        self.binary = binary
        self.directory = directory
        self.client_lock = EmbeddedClientLock(api, directory / "run" / "seekdb.clients")
        self.process = None
        self.console = None
        self.metadata = {
            "name": name, "client_lock": str(self.client_lock.path),
            "binary": str(binary), "cwd": str(binary.parent),
            "shutdown_method": "embedded_client_file_lock_release",
            "command": [
                str(binary), "--nodaemon", "--embedded", "--log-level", "INFO",
                f"--base-dir={directory}", f"--port={args.port}",
            ],
        }
        for key, value in parameters.items():
            self.metadata["command"].extend(["--parameter", f"{key}={value}"])
        self.console_path = directory.parent / f"{name}.console.log"
        self.metadata["console_log"] = str(self.console_path)

    def start(self, fresh):
        self.client_lock.acquire()
        markers = {"sql_service_ready": READY_MARKER, "client_exit": CLIENT_EXIT_MARKER}
        if fresh:
            markers.update(BOOTSTRAP_MARKERS)
        self.watcher = LogWatcher(self.directory / "log", markers)
        self.metadata["log_evidence"] = self.watcher.found
        self.console = self.console_path.open("wb")
        self.process = subprocess.Popen(
            self.metadata["command"], cwd=self.binary.parent,
            stdout=self.console, stderr=subprocess.STDOUT,
        )
        self.metadata["pid"] = self.process.pid
        if self.args.cpu_affinity_mask is not None:
            self.metadata["cpu_affinity"] = set_cpu_affinity(
                self.api, self.process.pid, self.args.cpu_affinity_mask,
            )
        deadline = time.monotonic() + self.args.startup_timeout
        while True:
            exit_code = self.process.poll()
            self.metadata["exit_code"] = exit_code
            require(exit_code is None, f"Server exited during startup: {exit_code}; {self.console_path}")
            self.watcher.update()
            if "sql_service_ready" in self.watcher.found:
                break
            require(time.monotonic() < deadline, f"Startup timeout; inspect {self.directory / 'log'}")
            time.sleep(0.1)
        if fresh:
            for name in BOOTSTRAP_MARKERS:
                require(name in self.watcher.found, f"Missing bootstrap milestone: {name}")

    def close(self):
        self.metadata["alive_before_client_lock_release"] = self.process.poll() is None
        require(self.metadata["alive_before_client_lock_release"], "Server exited before client lock release")
        self.client_lock.close()
        self.metadata["client_lock_released"] = True
        self.metadata["exit_code"] = self.process.wait(timeout=self.args.shutdown_timeout)
        self.watcher.update()
        self.metadata["client_exit_log_observed"] = "client_exit" in self.watcher.found
        require(self.process.returncode == 0, f"Server exit code: {self.process.returncode}")

    def cleanup(self):
        # 异常路径仅结束本脚本启动的进程，完整保留实例目录与日志。
        try:
            if self.process is not None and self.process.poll() is None:
                self.metadata["cleanup_method"] = "TerminateProcess"
                self.process.terminate()
                self.process.wait(timeout=self.args.shutdown_timeout)
        finally:
            if self.process is not None:
                self.metadata["exit_code"] = self.process.poll()
            try:
                self.client_lock.close()
            finally:
                if self.console is not None:
                    self.console.close()


def verify_sql(args, expected, create):
    with pymysql.connect(
        host="127.0.0.1", port=args.port, user="root", password="",
        charset="utf8mb4", autocommit=False, connect_timeout=10,
        read_timeout=30, write_timeout=30,
    ) as connection:
        with connection.cursor() as cursor:
            cursor.execute("SELECT 1")
            require(cursor.fetchone() == (1,), "SELECT 1 returned an unexpected result")
            if create:
                cursor.execute("CREATE DATABASE bootstrap_lifecycle CHARACTER SET utf8mb4")
                cursor.execute(
                    "CREATE TABLE bootstrap_lifecycle.records "
                    "(record_id INT PRIMARY KEY, payload VARCHAR(128))"
                )
                cursor.execute(
                    "INSERT INTO bootstrap_lifecycle.records VALUES (%s, %s)", (1, expected),
                )
                connection.commit()
            cursor.execute("SELECT record_id, payload FROM bootstrap_lifecycle.records")
            require(cursor.fetchall() == ((1, expected),), "Persisted data differs from the committed record")


def positive_number(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("Value must be finite and positive")
    return number


def positive_affinity_mask(value):
    mask = int(value, 0)
    if mask <= 0 or mask >= 1 << (ctypes.sizeof(ctypes.c_size_t) * 8):
        raise argparse.ArgumentTypeError("CPU affinity mask must be a positive DWORD_PTR value")
    return mask


def main():
    parser = argparse.ArgumentParser(description="Validate Windows bootstrap and embedded file-lock lifecycle")
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--trials", type=int, default=10)
    parser.add_argument("--port", type=int, default=2881)
    parser.add_argument("--startup-timeout", type=positive_number, default=240)
    parser.add_argument("--shutdown-timeout", type=positive_number, default=60)
    parser.add_argument("--cpu-affinity-mask", type=positive_affinity_mask)
    parser.add_argument("--copy-runtime-per-trial", action="store_true")
    parser.add_argument("--parameter", action="append", default=[], metavar="KEY=VALUE")
    args = parser.parse_args()
    require(os.name == "nt", "This integration test requires Windows")
    require(args.trials > 0, "--trials must be positive")
    require(1 <= args.port <= 65535, "--port must be between 1 and 65535")
    args.binary = args.binary.resolve(strict=True)
    require(args.binary.is_file(), "--binary must identify a file")
    args.work_dir = args.work_dir.resolve()
    args.work_dir.mkdir(parents=True, exist_ok=False)
    parameters = {"memory_budget": "1G", "log_disk_size": "2G", "mysql_port_mode": "specified"}
    for parameter in args.parameter:
        key, separator, value = parameter.partition("=")
        require(separator and key and value, "--parameter must be KEY=VALUE")
        require(key != "mysql_port", "Use --port to choose the SQL port")
        parameters[key] = value
    require(parameters["mysql_port_mode"] == "specified", "TCP validation requires mysql_port_mode=specified")
    with args.binary.open("rb") as binary:
        digest = hashlib.file_digest(binary, "sha256").hexdigest()
    report = {
        "status": "running", "started_at": timestamp(), "binary": str(args.binary),
        "binary_sha256": digest, "binary_size_bytes": args.binary.stat().st_size,
        "platform": sys.platform, "python_version": sys.version,
        "pymysql_version": version("PyMySQL"),
        "pymysql_compatibility_version": pymysql.__version__,
        "work_dir": str(args.work_dir), "trials_requested": args.trials, "port": args.port,
        "startup_timeout_seconds": args.startup_timeout,
        "shutdown_timeout_seconds": args.shutdown_timeout, "parameters": parameters,
        "cpu_affinity_mask": hex(args.cpu_affinity_mask) if args.cpu_affinity_mask is not None else None,
        "copy_runtime_per_trial": args.copy_runtime_per_trial,
        "coverage": "native Windows bootstrap, TCP SQL, committed data across embedded client file-lock exit",
        "trials": [],
    }
    recorder = Recorder(args.work_dir / "report.json", report)
    recorder.write()
    runs = []
    try:
        api = windows_api()
        for number in range(1, args.trials + 1):
            directory = args.work_dir / f"trial-{number:03d}" / "instance"
            directory.parent.mkdir()
            trial = {"trial": number, "instance_dir": str(directory), "status": "running", "stages": [], "processes": []}
            report["trials"].append(trial)
            binary = recorder.phase(trial, "prepare_runtime", lambda: prepare_runtime(args, trial, digest))
            expected = f"seekdb 初始化验证 🐬 第 {number} 轮"
            trial["expected_payload"] = expected
            for fresh, name in [(True, "first"), (False, "reopen")]:
                server = ServerRun(api, args, binary, directory, name, parameters)
                runs.append(server)
                trial["processes"].append(server.metadata)
                recorder.phase(trial, f"{name}_startup", lambda: server.start(fresh))
                recorder.phase(trial, f"{name}_sql", lambda: verify_sql(args, expected, fresh))
                recorder.phase(trial, f"{name}_client_lock_exit", server.close)
                server.cleanup()
            trial["status"] = "passed"
            recorder.write()
    finally:
        operation_error = sys.exc_info()[1]
        try:
            with ExitStack() as cleanup:
                for server in runs:
                    cleanup.callback(server.cleanup)
        finally:
            error = sys.exc_info()[1]
            if error is not None and error is not operation_error:
                report["cleanup_error"] = describe_error(error)
                if operation_error is not None:
                    report["operation_error"] = describe_error(operation_error)
            if error is not None and report["trials"]:
                report["trials"][-1]["status"] = "failed"
            recorder.finish(error)
    print(f"report={recorder.path}", flush=True)


if __name__ == "__main__":
    main()
