#!/usr/bin/env python3
"""Exercise replay retirement using an isolated seekdb binary and data directory.

Requires PyMySQL. Run with --binary <seekdb> --work-dir <new-directory>.
Pass --with-standby to also exercise live replay and online promotion.
"""

import argparse
import json
import os
import signal
import socket
import subprocess
import threading
import time
from pathlib import Path

import pymysql


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Server:
    def __init__(self, binary, directory, role="PRIMARY", source=None):
        self.binary = binary
        self.directory = directory
        self.directory.mkdir()
        self.port = free_port()
        self.rpc_port = free_port()
        self.role = role
        self.datafile_size = "2G" if role == "STANDBY" else "64M"
        self.source = source
        self.process = None
        self.boot = 0

    def start(self):
        self.boot += 1
        args = [str(self.binary), "--nodaemon", "--base-dir", str(self.directory),
                "--port", str(self.port), "--role", self.role, "--log-level", "INFO"]
        for parameter in [f"rpc_port={self.rpc_port}", "cpu_count=2",
                          "memory_budget=1G", "log_disk_size=2G",
                          f"datafile_size={self.datafile_size}", "datafile_maxsize=4G",
                          "enable_rpc_service=true"]:
            args += ["--parameter", parameter]
        if self.source:
            args += ["--parameter", f"log_restore_source=127.0.0.1:{self.source.rpc_port}"]
        environment = os.environ.copy()
        for key in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy"):
            environment.pop(key, None)
        with (self.directory / f"console-{self.boot}.log").open("ab") as output:
            self.process = subprocess.Popen(args, stdout=output, stderr=subprocess.STDOUT, env=environment)
        deadline = time.monotonic() + 120
        last_error = None
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise AssertionError(f"seekdb exited with {self.process.returncode}: {self.directory}")
            try:
                self.query("SELECT 1")
                return
            except pymysql.Error as error:
                last_error = error
                time.sleep(0.2)
        raise AssertionError(f"seekdb did not become ready: {last_error}: {self.directory}")

    def query(self, sql, parameters=None):
        with pymysql.connect(host="127.0.0.1", port=self.port, user="root",
                             autocommit=True, connect_timeout=2, read_timeout=60,
                             cursorclass=pymysql.cursors.DictCursor) as connection:
            with connection.cursor() as cursor:
                cursor.execute(sql, parameters)
                return cursor.fetchall()

    def stop(self, crash=False):
        if self.process and self.process.poll() is None:
            self.process.send_signal(signal.SIGKILL if crash else signal.SIGUSR1)
            try:
                self.process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
                raise AssertionError(f"shutdown did not finish: {self.directory}")
            if not crash:
                assert self.process.returncode == 0, self.process.returncode

    def cleanup(self):
        if self.process and self.process.poll() is None:
            self.process.kill()
            self.process.wait()

    def replay_stat(self):
        rows = self.query("SELECT * FROM oceanbase.__all_virtual_replay_stat")
        assert len(rows) == 1, rows
        return {key.lower(): value for key, value in rows[0].items()}

    def decided_scn(self):
        return int(self.query("SELECT sync_scn FROM oceanbase.__all_virtual_server_stat")[0]["sync_scn"])

    def assert_retired(self):
        stat = self.replay_stat()
        assert stat["enabled"] == 0 and stat["pending_cnt"] == 0, stat
        threads = [item.read_text().strip()
                   for item in Path(f"/proc/{self.process.pid}/task").glob("*/comm")]
        assert not any("ReplaySrv" in name for name in threads), threads
        return stat


def wait_rows(server, expected):
    deadline = time.monotonic() + 60
    last_error = None
    while time.monotonic() < deadline:
        try:
            result = server.query("SELECT COUNT(*) AS n FROM replay_lifecycle.rows_test")
            if result[0]["n"] == expected:
                return
        except pymysql.Error as error:
            last_error = error
        time.sleep(0.2)
    raise AssertionError(f"expected {expected} rows: {last_error}")


def insert_rows(server, start, count):
    # Each batch is committed before the process is killed. Payloads leave enough
    # redo to exercise buffered replay rather than only an empty restart.
    for offset in range(start, start + count, 100):
        values = [(i, "x" * 1024) for i in range(offset, min(offset + 100, start + count))]
        with pymysql.connect(host="127.0.0.1", port=server.port, user="root",
                             autocommit=True, read_timeout=60) as connection:
            with connection.cursor() as cursor:
                cursor.executemany("INSERT INTO replay_lifecycle.rows_test VALUES (%s, %s)", values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--with-standby", action="store_true")
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=False)
    primary = Server(args.binary.resolve(), args.work_dir / "primary")
    servers = [primary]
    results = {}
    try:
        primary.start()
        results["fresh_primary"] = primary.assert_retired()
        primary.query("CREATE DATABASE replay_lifecycle")
        primary.query("CREATE TABLE replay_lifecycle.rows_test (id INT PRIMARY KEY, payload VARCHAR(2048))")
        insert_rows(primary, 0, 2000)
        before_crash = primary.assert_retired()
        primary.stop(crash=True)
        primary.start()
        wait_rows(primary, 2000)
        recovered = primary.assert_retired()
        results["crash_recovery"] = recovered
        assert recovered["unsubmitted_lsn"] > results["fresh_primary"]["unsubmitted_lsn"], recovered
        assert primary.decided_scn() >= recovered["unsubmitted_log_scn"] - 1

        # New primary writes advance the live log tail; saved replay progress stays fixed.
        insert_rows(primary, 2000, 100)
        after_write = primary.assert_retired()
        assert after_write["end_lsn"] > before_crash["end_lsn"], after_write
        assert after_write["unsubmitted_log_scn"] == recovered["unsubmitted_log_scn"], after_write
        primary.query("ALTER SYSTEM MINOR FREEZE")
        results["primary_write_and_freeze"] = after_write
        primary.stop()
        primary.start()
        wait_rows(primary, 2100)
        results["graceful_restart"] = primary.assert_retired()

        if args.with_standby:
            primary.stop()
            # Keep the replication fixture independent of the preceding freeze
            # and restart tests, which replace physical SSTable baselines.
            primary = Server(args.binary.resolve(), args.work_dir / "source")
            servers.append(primary)
            primary.start()
            primary.query("CREATE DATABASE replay_lifecycle")
            primary.query("CREATE TABLE replay_lifecycle.rows_test (id INT PRIMARY KEY, payload VARCHAR(2048))")
            insert_rows(primary, 0, 2100)
            # Physical bootstrap needs a persisted replay baseline, including a
            # nonzero timestamp for the standby timestamp provider.
            primary.query("ALTER SYSTEM MINOR FREEZE")
            deadline = time.monotonic() + 60
            while True:
                checkpoint = primary.query(
                    "SELECT checkpoint_scn FROM oceanbase.__all_virtual_ls_info")[0]["checkpoint_scn"]
                if checkpoint > 0:
                    break
                assert time.monotonic() < deadline, "source checkpoint did not advance"
                time.sleep(0.2)
            time.sleep(1)
            standby = Server(args.binary.resolve(), args.work_dir / "standby", "STANDBY", primary)
            servers.append(standby)
            standby.start()
            wait_rows(standby, 2100)
            active = standby.replay_stat()
            assert active["enabled"] == 1, active
            results["standby_live_replay"] = active
            insert_rows(primary, 2100, 100)
            wait_rows(standby, 2200)
            standby.stop()
            standby.start()
            wait_rows(standby, 2200)
            restarted = standby.replay_stat()
            assert restarted["enabled"] == 1, restarted
            results["standby_restart"] = restarted
            # Readers continue through the executor-to-snapshot handoff.
            stop_readers = threading.Event()
            read_errors = []
            samples = []

            def read_progress():
                while not stop_readers.is_set():
                    try:
                        samples.append((standby.replay_stat(), standby.decided_scn()))
                    except Exception as error:
                        read_errors.append(repr(error))
                        return

            readers = [threading.Thread(target=read_progress) for _ in range(2)]
            for reader in readers:
                reader.start()
            try:
                primary.query("ALTER SYSTEM SWITCHOVER TO STANDBY")
                standby.query("ALTER SYSTEM SWITCHOVER TO PRIMARY")
                time.sleep(0.2)
            finally:
                stop_readers.set()
                for reader in readers:
                    reader.join(timeout=65)
            assert not read_errors, read_errors
            assert len(samples) > 0
            results["concurrent_progress_reads"] = len(samples)
            results["promoted_primary"] = standby.assert_retired()
            insert_rows(standby, 2200, 100)
            wait_rows(standby, 2300)
            standby.query("ALTER SYSTEM MINOR FREEZE")
            standby.stop()
            standby.role = "PRIMARY"
            standby.source = None
            standby.start()
            wait_rows(standby, 2300)
            results["promoted_primary_restart"] = standby.assert_retired()
            standby.stop()
        primary.stop()
        results["result"] = "passed"
        print(json.dumps(results, indent=2), flush=True)
    finally:
        for server in reversed(servers):
            server.cleanup()
        (args.work_dir / "results.json").write_text(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
