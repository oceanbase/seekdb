"""Repeat fresh-server bootstrap and SQL-readiness checks for issue #1544.

Requires a built seekdb binary and a MySQL-compatible command-line client.
Each trial preserves its data and logs, including on failure. This checks fresh
startup only: child processes are terminated after the readiness check, so it
does not claim to test graceful shutdown or persistence across restart.
"""

import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time


def positive_int(value):
    result = int(value)
    if result <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return result


def run_trial(binary, client, directory, port, timeout):
    # Refuse an occupied port before starting a fresh child. Queries below are
    # read-only, and we only terminate the child created by this function.
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", port))
    directory.mkdir()
    result = {"directory": str(directory), "passed": False}
    started = time.monotonic()
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    with (directory / "console.log").open("wb") as console:
        process = subprocess.Popen(
            [str(binary), "--nodaemon", f"--base-dir={directory}", f"--port={port}"],
            cwd=directory,
            stdout=console,
            stderr=subprocess.STDOUT,
            creationflags=flags,
        )
        try:
            deadline = started + timeout
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    result["error"] = f"server exited before readiness: {process.returncode}"
                    break
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                try:
                    query = subprocess.run(
                        [str(client), "--no-defaults", "--protocol=TCP",
                         "--host=127.0.0.1", f"--port={port}", "--user=root",
                         "--connect-timeout=2", "--batch", "--skip-column-names",
                         "--execute=SELECT 1"],
                        capture_output=True,
                        timeout=min(5, remaining),
                        creationflags=flags,
                    )
                    if query.returncode == 0 and query.stdout.strip() == b"1":
                        if process.poll() is None:
                            result["passed"] = True
                            break
                    result["last_client_error"] = query.stderr.decode("utf-8", errors="replace")[-2000:]
                except subprocess.TimeoutExpired:
                    result["last_client_error"] = "SQL readiness probe timed out"
                time.sleep(min(0.5, max(0, deadline - time.monotonic())))
            if not result["passed"] and "error" not in result:
                result["error"] = "readiness deadline exceeded"
            result["startup_seconds"] = round(time.monotonic() - started, 3)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=30)
            result["exit_code_after_cleanup"] = process.returncode
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--client", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="new directory for all trial data; must not already exist")
    parser.add_argument("--trials", type=positive_int, default=3)
    parser.add_argument("--timeout", type=positive_int, default=240)
    parser.add_argument("--port", type=positive_int, default=2881)
    args = parser.parse_args()
    binary, client = args.binary.resolve(), args.client.resolve()
    if not binary.is_file() or not client.is_file():
        parser.error("--binary and --client must name existing executable files")
    if args.port > 65535:
        parser.error("--port must be at most 65535")
    output = args.output.resolve()
    if output.exists():
        parser.error("--output must be a new directory; previous evidence is never overwritten")
    output.mkdir(parents=True)
    results = []
    for trial in range(1, args.trials + 1):
        directory = output / f"trial-{trial:03d}"
        try:
            result = run_trial(binary, client, directory, args.port, args.timeout)
        except (OSError, subprocess.SubprocessError) as error:
            result = {"directory": str(directory), "passed": False, "error": str(error)}
        results.append(result)
        report = {"binary": str(binary), "requested_trials": args.trials, "results": results}
        (output / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(result), flush=True)
        if not result["passed"]:
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
