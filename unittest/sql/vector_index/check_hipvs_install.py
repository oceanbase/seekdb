#!/usr/bin/env python3
"""Install and check an actual Linux GPU Release server component.

Requires an already built seekdb target. The install prefix must be absent or
empty so an executable from an earlier installation cannot hide missing files.
This checks deployment and loading; check_hipvs_sql.py checks database behavior.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    prefix = args.prefix.resolve()
    report = {"completed": False, "build_directory": str(build),
              "install_prefix": str(prefix), "commands": [],
              "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    env = dict(os.environ)
    env.pop("LD_LIBRARY_PATH", None)
    env.pop("LD_PRELOAD", None)

    def run(command, expected=0):
        result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=180)
        report["commands"].append({"command": command, "returncode": result.returncode,
                                   "stdout": result.stdout, "stderr": result.stderr})
        if result.returncode != expected:
            raise RuntimeError(f"command failed ({result.returncode}): {command[0]}")
        return result.stdout + result.stderr

    try:
        if sys.platform != "linux":
            raise RuntimeError("run this deployment check on Linux")
        if not (build / "CMakeCache.txt").is_file():
            raise RuntimeError("the build directory has no CMake configuration")
        if prefix.exists() and (not prefix.is_dir() or any(prefix.iterdir())):
            raise RuntimeError("install prefix must be absent or empty")
        run([args.cmake, "--install", str(build), "--component", "server",
             "--prefix", str(prefix)])
        required = ["usr/bin/seekdb", "usr/bin/seekdb_hipvs_hnsw_worker",
                    "usr/libexec/seekdb/lib/libseekdb_hipvs_hnsw.so"]
        missing = [name for name in required if not (prefix / name).is_file()]
        report["required_files"] = required
        if missing:
            raise RuntimeError("server component is incomplete: " + ", ".join(missing))
        for name in required[:2]:
            if not os.access(prefix / name, os.X_OK):
                raise RuntimeError("installed program is not executable: " + name)
        report["server_version"] = run([str(prefix / required[0]), "--version"]).strip()
        # No arguments returns 2 after the ELF loader resolves the bridge and
        # hipVS/ROCm libraries; it does not run a GPU build or touch a database.
        run([str(prefix / required[1])], expected=2)
        dependencies = run(["ldd", str(prefix / required[1])])
        if "not found" in dependencies:
            raise RuntimeError("installed GPU worker has unresolved runtime libraries")
        report["completed"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        report["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"completed": report["completed"], "error": report.get("error"),
                          "output": str(args.output)}, ensure_ascii=False), flush=True)
    return 0 if report["completed"] else 1


if __name__ == "__main__":
    sys.exit(main())
