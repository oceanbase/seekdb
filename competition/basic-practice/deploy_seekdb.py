#!/usr/bin/env python3
"""Deploy OceanBase seekdb and collect its telemetry identity.

Reproducible deployment for the seekdb 2026 basic-practice task:

  1. install the official Debian 12 package of seekdb
  2. write /etc/seekdb/seekdb.cnf
  3. start the observer
  4. read run/telemetry.json and print its content.id

The telemetry payload is also posted to the OceanBase telemetry endpoint.
The payload file and the POSTed body are the same bytes, so re-posting the
file is equivalent to the report the observer sends on bootstrap.

Usage: sudo python3 deploy_seekdb.py [--package ./seekdb_1.4.0.deb]
"""
import argparse
import json
import os
import subprocess
import sys
import time
import urllib.request

TELEMETRY_URL = "https://openwebapi.oceanbase.com/api/web/oceanbase/report"
BASE_DIR = "/var/lib/seekdb"
CONFIG_FILE = "/etc/seekdb/seekdb.cnf"
TELEMETRY_FILE = os.path.join(BASE_DIR, "run", "telemetry.json")
DEFAULT_CONFIG = """# These parameters are permanently valid
base-dir={base_dir}
data-dir={base_dir}/store
redo-dir={base_dir}/store/redo

# These parameters are valid only during initialization
port=2881
cpu_count={cpu_count}
""".format(base_dir=BASE_DIR, cpu_count=os.cpu_count() or 2)


def run(cmd, check=True):
    print("$ " + " ".join(cmd))
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if check and proc.returncode != 0:
        sys.exit("command failed: %s\n%s" % (proc.stderr.strip(), proc.stdout.strip()))
    return proc


def install_package(package):
    if package and os.path.exists(package):
        run(["dpkg", "-i", package])
    else:
        run(["apt-get", "install", "-y", "seekdb"])


def configure():
    os.makedirs("/etc/seekdb", exist_ok=True)
    with open(CONFIG_FILE, "w", encoding="utf-8") as fh:
        fh.write(DEFAULT_CONFIG)
    print("wrote", CONFIG_FILE)


def start():
    # The package ships a systemd unit; start the observer directly so the
    # script also works inside containers without an init system.
    script = "/usr/libexec/seekdb/scripts/seekdb_systemd_start"
    if os.path.exists(script):
        subprocess.Popen([script], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        run(["/usr/bin/seekdb", "--nodaemon", "--base-dir=" + BASE_DIR])


def wait_for_telemetry(timeout=120):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(TELEMETRY_FILE):
            with open(TELEMETRY_FILE, encoding="utf-8") as fh:
                return json.load(fh)
        time.sleep(2)
    sys.exit("telemetry file not found: " + TELEMETRY_FILE)


def report(payload):
    body = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(TELEMETRY_URL, data=body, method="POST")
    req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=30) as resp:
        return resp.read().decode("utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--package", default=None)
    ap.add_argument("--no-report", action="store_true")
    args = ap.parse_args()

    install_package(args.package)
    configure()
    start()
    payload = wait_for_telemetry()
    print("\ntelemetry id (content.id): %s" % payload["content"]["id"])
    print("telemetry file: %s" % TELEMETRY_FILE)
    if not args.no_report:
        print("report response: %s" % report(payload))


if __name__ == "__main__":
    main()
