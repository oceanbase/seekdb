#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
"""Run the real startup probe; process exit alone is not a lifecycle pass."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


@contextmanager
def diagnostic_probe(executable, snapshot):
    """Optional JS-only instrumentation; the WASM artifact is unchanged."""
    if snapshot is None:
        yield executable
        return
    with tempfile.TemporaryDirectory(prefix="seekdb-bootstrap-diagnostic-") as directory:
        copy = Path(directory) / executable.name
        shutil.copy2(executable, copy)
        shutil.copy2(executable.with_suffix(".wasm"), copy.with_suffix(".wasm"))
        with copy.open("a") as script:
            script.write("""
if (!ENVIRONMENT_IS_PTHREAD) {
  const dump = () => { try {
    if (FS.analyzePath('/seekdb/log/observer.log').exists)
      require('node:fs').writeFileSync(SNAPSHOT_PATH, FS.readFile('/seekdb/log/observer.log'));
  } catch (_) {} };
  setInterval(dump, 1000).unref();
  process.on('exit', dump);
  process.on('uncaughtExceptionMonitor', dump);
}
""".replace("SNAPSHOT_PATH", json.dumps(str(snapshot))))
        yield copy


def diagnostics(output, build):
    """Keep failures readable without echoing Emscripten's minified JS line."""
    markers, errors, functions = [], [], []
    for line in output.splitlines():
        if len(line) > 4096:
            continue
        if line.startswith(("bootstrap:", "sql-probe:")) and line not in markers:
            markers.append(line)
        if "RuntimeError:" in line:
            error = line[line.index("RuntimeError:"):]
            if error not in errors:
                errors.append(error)
        match = re.search(r"wasm-function\[(\d+)\]", line)
        if match and match[1] not in functions and len(functions) < 8:
            functions.append(match[1])
    names = {}
    symbols = build / "seekdb_wasm_bootstrap_probe.js.symbols"
    if functions and symbols.exists():
        with symbols.open() as source:
            for line in source:
                index, separator, name = line.rstrip().partition(":")
                if separator and index in functions:
                    names[index] = name
    return dict(observed_markers=markers, runtime_errors=errors,
                stack_functions=[dict(index=index, name=names.get(index)) for index in functions])

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build_dir", type=Path)
parser.add_argument("--node", default=os.environ.get("EMSDK_NODE"))
parser.add_argument("--timeout", type=int, default=180)
parser.add_argument("--static-only", action="store_true")
parser.add_argument("--invalid-log-budget", action="store_true")
parser.add_argument("--client-sql", action="store_true")
parser.add_argument("--capture-memfs", action="store_true",
                    help="Capture the complete observer log using a diagnostic JS wrapper; may affect timing")
args = parser.parse_args()
if not args.node or args.timeout <= 0:
    parser.error("activate the pinned SDK (EMSDK_NODE), or supply --node; timeout must be positive")
build = args.build_dir.resolve()
executable = build / "seekdb_wasm_bootstrap_probe.js"
wasm = executable.with_suffix(".wasm")
digest = hashlib.sha256()
with wasm.open("rb") as artifact:
    for chunk in iter(lambda: artifact.read(1024 * 1024), b""):
        digest.update(chunk)
artifact_sha256 = digest.hexdigest()
cases = [("startup", None, [], [
    "bootstrap: init returned 0", "bootstrap: start returned 0",
    "bootstrap: wait returned 0", "bootstrap: destroy returned",
])]
expected_code = 0
if sum((args.static_only, args.invalid_log_budget, args.client_sql)) > 1:
    parser.error("select one probe mode")
if args.client_sql:
    cases = [("client-sql", None, [], [
        "bootstrap: init returned 0", "bootstrap: start returned 0",
        "sql-probe: all assertions passed", "bootstrap: wait returned 0",
        "bootstrap: destroy returned",
    ])]
if args.invalid_log_budget:
    if args.static_only:
        parser.error("select one probe mode")
    expected_code = 1
    cases = [("invalid-log-budget", None, ["--invalid-log-budget"], [
        "bootstrap: init returned 0", "bootstrap: start returned -4736",
        "bootstrap: destroy returned",
    ])]
if args.static_only:
    cases = [
        ("timezone-kathmandu", "Asia/Kathmandu", ["--static-init-only", "+05:45"], ["bootstrap: static initialization passed"]),
        ("timezone-west3", "Etc/GMT+3", ["--static-init-only", "-03:00"], ["bootstrap: static initialization passed"]),
    ]
results = []
for name, timezone, arguments, markers in cases:
    environment = os.environ.copy()
    if timezone:
        environment["TZ"] = timezone
    path = build / f"bootstrap-{name}.log"
    snapshot = build / f"bootstrap-{name}-memfs.log" if args.capture_memfs else None
    timed_out = False
    with path.open("wb") as log, diagnostic_probe(executable, snapshot) as run_executable:
        try:
            command = [args.node, str(run_executable), *arguments]
            if args.client_sql:
                harness = Path(__file__).resolve().parents[2] / "unittest/wasm/test_engine_sql.mjs"
                command = [args.node, str(harness), str(run_executable)]
            process = subprocess.run(command,
                                     env=environment, stdout=log, stderr=subprocess.STDOUT,
                                     timeout=args.timeout)
            code = process.returncode
        except subprocess.TimeoutExpired:
            code, timed_out = 124, True
    output = path.read_text(errors="replace")
    timed_out = timed_out or "sql-probe: timed out" in output.splitlines()
    missing = [marker for marker in markers if marker not in output]
    result = dict(case=name, exit_code=code, timed_out=timed_out,
                  missing_markers=missing, passed=code == expected_code and not missing, log=str(path),
                  wasm_sha256=artifact_sha256)
    if snapshot is not None:
        result["diagnostic_memfs_log"] = str(snapshot)
        result["diagnostic_js_wrapper"] = True
    result.update(diagnostics(output, build))
    results.append(result)
    print(json.dumps(result), flush=True)
(build / ("bootstrap-static-results.json" if args.static_only else
          "bootstrap-client-sql-results.json" if args.client_sql else
          "bootstrap-invalid-log-budget-results.json" if args.invalid_log_budget else "bootstrap-results.json")).write_text(
    json.dumps(results, indent=2) + "\n")
raise SystemExit(0 if all(result["passed"] for result in results) else 1)
