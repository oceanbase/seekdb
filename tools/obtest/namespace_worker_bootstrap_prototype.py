#!/usr/bin/env python3
"""PROTOTYPE V18: cold bootstrap with shared SQL execution forbidden.

SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp python3 tools/obtest/namespace_worker_bootstrap_prototype.py --binary build_release/src/observer/seekdb
"""
import argparse
import os
import resource
import subprocess
import time

import pymysql
from namespace_fork_prototype import Experiment


class BootstrapExperiment(Experiment):
    def start(self):
        env = os.environ.copy()
        env.update(SEEKDB_NAMESPACE_FORK_PROTOTYPE="6",
                   SEEKDB_NAMESPACE_SQL_WORKER_PROTOTYPE="1")
        # Worker mode itself enforces Worker-only SQL execution; there is no
        # separate bootstrap rollout switch.
        command = [self.binary, "--nodaemon", "--base-dir=" + str(self.base), "-P" + str(self.port),
                   "--log-level=WARN", "--parameter", "memory_budget=2G", "--parameter", "cpu_count=4",
                   "--parameter", "namespace_sql_worker_memory_budget=640M",
                   "--parameter", "datafile_size=256M", "--parameter", "datafile_maxsize=512M",
                   "--parameter", "log_disk_size=2G", "--parameter", "max_syslog_file_count=16"]
        for name, value in getattr(self, "extra_parameters", ()):
            command.extend(("--parameter", f"{name}={value}"))
        self.proc = subprocess.Popen(command, env=env, stdout=self.output, stderr=subprocess.STDOUT)
        self.record("setup", binary=self.binary, base=self.base, pid=self.proc.pid, port=self.port)
        deadline = time.monotonic() + 120
        last_error = None
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"bootstrap exited ({self.proc.returncode}): {self.base}")
            try:
                self.connection = pymysql.connect(host="127.0.0.1", port=self.port, user="root", password="",
                                                  autocommit=True, connect_timeout=2, read_timeout=15)
                break
            except pymysql.MySQLError as error:
                last_error = error
                time.sleep(.5)
        if self.connection is None:
            raise TimeoutError((str(self.base), last_error))

    def probe(self):
        assert self.sql("SELECT 1") == ((1,),)
        self.sql("BEGIN")
        assert self.connection.server_status & 1
        assert self.sql("SELECT @@autocommit") == ((1,),)
        self.sql("COMMIT")
        assert not self.connection.server_status & 1
        databases = self.sql("SELECT database_id,database_name FROM oceanbase.__all_database ORDER BY database_id")
        assert databases and any(row[1] == "oceanbase" for row in databases), databases
        names = self.sql("SELECT name FROM oceanbase.__all_sys_variable ORDER BY name", log=False)
        assert len(names) > 64 and len(set(names)) == len(names), names
        filtered = self.sql("SELECT name FROM oceanbase.__all_sys_variable WHERE LENGTH(name)>15 ORDER BY name", log=False)
        assert filtered == tuple(row for row in names if len(row[0]) > 15)
        assert self.sql("SELECT count(*) FROM oceanbase.__all_virtual_parameter_stat "
                        "WHERE name='enable_record_trace_log' AND value!='false'") == ((0,),)
        log = self.engine_log()
        assert "PROTOTYPE_V18_SHARED_SQL_REJECT" not in log
        assert "[bootstrap 10/10] cluster bootstrap success" in log
        workers = list((self.base / "run").glob("namespace-worker-1-*/process.out"))
        assert workers and any("PROTOTYPE_V18_INNER_EXECUTE" in path.read_text(errors="replace") for path in workers)
        assert any(
            "PROTOTYPE_NAMESPACE_WORKER_RESOURCES ns=1 memory_budget=671088640 threads=2"
            in path.read_text(errors="replace")
            for path in workers), workers
        self.record("worker_only_sql_verified", system_databases=len(databases), variable_names=len(names))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    experiment = BootstrapExperiment(args.binary, "bootstrap_v18", prototype=6)
    try:
        experiment.start()
        experiment.probe()
        experiment.connection.close(); experiment.connection = None
        experiment.proc.kill(); experiment.proc.wait(timeout=15); experiment.proc = None
        experiment.record("crash_for_recovery")
        experiment.start()
        experiment.probe()
        experiment.record("PASS", cold_bootstrap=True, shared_sql_forbidden=True, recovery=True)
    finally:
        experiment.close()


if __name__ == "__main__":
    main()
