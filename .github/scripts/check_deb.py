#!/usr/bin/env python3
"""Install and smoke-test DEBs in their target distribution container."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import time

HERE = Path(__file__).resolve().parent
SCRIPTS = Path("/usr/libexec/seekdb/scripts")
START_LOG = Path("/tmp/seekdb-start.log")


def run(arguments, **kwargs):
    subprocess.run([str(value) for value in arguments], check=True, **kwargs)


def container():
    root = HERE.parents[1]
    arch = {"x86_64": "amd64", "aarch64": "arm64"}[os.environ["PACKAGE_ARCH"]]
    run(["docker", "run", "--rm", "--platform", "linux/" + arch,
         "--ulimit", "nofile=65536:65536", "--ulimit", "stack=-1:-1",
         "--env", "DEBIAN_FRONTEND=noninteractive", "--env", "TELEMETRY_MODE=0",
         "--mount", "type=bind,source=%s,target=/packages,readonly" % (root / "dist"),
         "--mount", "type=bind,source=%s,target=/checks,readonly" % HERE,
         os.environ["PACKAGE_IMAGE"], "bash", "-ec", "\n".join([
             "cat /etc/os-release",
             "apt-get update -qq",
             "apt-get install -y --no-install-recommends /packages/*.deb python3-pymysql",
             "exec python3 /checks/check_deb.py --installed",
         ])])


def configure():
    config = Path("/etc/seekdb/seekdb.cnf")
    content = config.read_text()
    for line in (HERE / "fixtures/seekdb-smoke.cnf").read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        key = line.split("=", 1)[0]
        content, count = re.subn(r"^" + re.escape(key) + r"=.*$", line, content, flags=re.MULTILINE)
        if not count:
            content = content.rstrip("\n") + "\n" + line + "\n"
    config.write_text(content)


def sql(process):
    import pymysql

    deadline = time.monotonic() + 240
    while True:
        try:
            connection = pymysql.connect(host="127.0.0.1", port=2881, user="root", password="",
                                         connect_timeout=2, read_timeout=10, write_timeout=10, autocommit=True)
            break
        except pymysql.err.OperationalError:
            if process.poll() is not None:
                raise RuntimeError("seekdb exited before accepting connections")
            if time.monotonic() >= deadline:
                raise
            time.sleep(2)
    with connection:
        with connection.cursor() as cursor:
            cursor.execute("CREATE DATABASE packaging_smoke")
            cursor.execute("CREATE TABLE packaging_smoke.t (id INT PRIMARY KEY, v VARCHAR(32))")
            cursor.execute("INSERT INTO packaging_smoke.t VALUES (1, 'ready')")
            cursor.execute("SELECT v FROM packaging_smoke.t WHERE id = 1")
            assert cursor.fetchone() == ("ready",)
            cursor.execute("DROP DATABASE packaging_smoke")
    print("Installed seekdb passed SQL read/write checks", flush=True)


def stop(process):
    with subprocess.Popen([str(SCRIPTS / "seekdb_systemd_stop")],
                          env=dict(os.environ, MAINPID=str(process.pid))) as stopper:
        # Reap seekdb while the stop helper runs so it does not wait on a zombie.
        try:
            result = process.wait(timeout=45)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise
    if stopper.returncode:
        raise subprocess.CalledProcessError(stopper.returncode, stopper.args)
    if result:
        raise subprocess.CalledProcessError(result, process.args)


def installed():
    run(["seekdb", "--version"])
    run(["systemd-analyze", "verify", "--man=no", "/usr/lib/systemd/system/seekdb.service"])
    configure()
    with START_LOG.open("w") as log:
        process = subprocess.Popen([str(SCRIPTS / "seekdb_systemd_start")], stdout=log, stderr=subprocess.STDOUT)
    try:
        try:
            sql(process)
        finally:
            stop(process)
    except BaseException:
        for log in [START_LOG, *sorted(Path("/var/lib/seekdb/log").glob("*.log"))]:
            subprocess.run(["tail", "-n", "80", str(log)], check=False)
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installed", action="store_true", help="check the installed package inside the container")
    arguments = parser.parse_args()
    installed() if arguments.installed else container()
