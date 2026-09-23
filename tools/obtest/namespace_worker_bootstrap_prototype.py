#!/usr/bin/env python3
"""Run the single-process namespace bootstrap integration gate."""
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
        command = [self.binary, "--nodaemon", "--base-dir=" + str(self.base), "-P" + str(self.port),
                   "--log-level=WARN", "--parameter", "memory_budget=2G", "--parameter", "cpu_count=4",
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--in-process", action="store_true",
                        help="accepted for compatibility; single-process is the only mode")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    from namespace_inprocess_prototype import run_case
    run_case(args.binary, "bootstrap")


if __name__ == "__main__":
    main()
