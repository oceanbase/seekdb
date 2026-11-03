#!/usr/bin/env python3
"""Build/run the IN-list dedup regression against this checkout's Bazel production runtime.

This command does not initialize dependencies or launch a database. Use --dry-run
to inspect the exact command without downloading Bazel or compiling anything.
"""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time
import uuid


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, default=root / ".inlist-dedup-validation")
    parser.add_argument("--workspace-root", type=Path, default=root,
                        help="explicit personal workspace containing checkout and existing Rust homes")
    parser.add_argument("--cargo-home", type=Path)
    parser.add_argument("--rustup-home", type=Path)
    parser.add_argument("--jobs", type=int, choices=range(1, 5), default=2)
    parser.add_argument("--memory-mb", type=int, default=6144)
    parser.add_argument("--no-run", action="store_true", help="compile/link only")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    output = args.output_root.resolve()
    workspace = args.workspace_root.resolve()
    if not root.is_relative_to(workspace):
        parser.error("this checkout must reside within --workspace-root")
    if not output.is_relative_to(root) or output == root:
        parser.error("--output-root must be a subdirectory of this checkout")
    if args.memory_mb < 1024:
        parser.error("--memory-mb must be at least 1024")
    common_git = Path(subprocess.check_output(
        ["git", "rev-parse", "--git-common-dir"], cwd=root, text=True).strip())
    common_git = (root / common_git).resolve()
    xdg_cache = workspace / ".cache" / "seekdb-inlist-dedup" / root.name
    if (not xdg_cache.is_relative_to(workspace) or xdg_cache.is_relative_to(root)
            or xdg_cache.is_relative_to(common_git.parent)):
        parser.error("--workspace-root must allow a personal XDG cache outside both checkout and main repository")
    command = [
        "nice", "-n", "10", sys.executable, str(root / "build.py"),
        "build" if args.no_run else "test",
        "--batch", "--output_user_root=" + str(output / "bazel-user"),
        "--host_jvm_args=-Xmx2048m",
        "--launcher_cache_dir=" + str(output / "bazel-launcher"),
        "--output_base=" + str(output / "build"),
        "--repository_cache=" + str(output / "repository-cache"),
        "--repo_contents_cache=",
        "--disk_cache=" + str(output / "action-cache"),
        "--experimental_disk_cache_gc_max_size=8G",
        "--http_max_parallel_downloads=4", "--lockfile_mode=error",
        "--jobs=" + str(args.jobs),
        "--local_resources=cpu=" + str(args.jobs),
        "--local_resources=memory=" + str(args.memory_mb),
        "--strategy=CppCompile=worker,sandboxed,local",
        "//unittest/sql/resolver:test_inlist_dedup",
    ]
    if not args.no_run:
        command += ["--test_output=errors", "--test_timeout=120"]
    # These are subprocess-only defaults, never changes to a shell profile.
    environment = os.environ.copy()
    directories = {"TMPDIR": output / "tmp", "XDG_CACHE_HOME": xdg_cache}
    for name, requested, fallback in (("CARGO_HOME", args.cargo_home, "cargo"),
                                       ("RUSTUP_HOME", args.rustup_home, "rustup")):
        directory = Path(requested or environment.get(name) or output / fallback).expanduser().resolve()
        if not directory.is_relative_to(workspace) or directory == workspace:
            parser.error(name + " must reside inside --workspace-root; supply an explicit safe home")
        directories[name] = directory
    for name, directory in directories.items():
        environment[name] = str(directory)
    if args.dry_run:
        print(json.dumps({"argv": command, "cwd": str(root),
                          "environment_overrides": {k: environment[k] for k in
                           ("TMPDIR", "XDG_CACHE_HOME", "CARGO_HOME", "RUSTUP_HOME")}}, indent=2))
        return 0
    for name in ("TMPDIR", "XDG_CACHE_HOME", "CARGO_HOME", "RUSTUP_HOME"):
        Path(environment[name]).mkdir(parents=True, exist_ok=True)
    sources = [
        "src/sql/resolver/dml/ob_inlist_resolver.cpp",
        "src/sql/resolver/dml/ob_inlist_resolver.h",
        "src/sql/resolver/expr/ob_raw_expr_resolver_impl.cpp",
        "src/sql/BUILD.bazel",
        "unittest/sql/resolver/BUILD.bazel",
        "unittest/sql/resolver/test_inlist_dedup.cpp",
        "unittest/sql/resolver/run_inlist_dedup_test.py",
        "unittest/sql/resolver/run_inlist_dedup_sql.py",
        "unittest/sql/resolver/README.md",
    ]
    manifest = {"command": command, "display_command": shlex.join(command),
                "cwd": str(root), "mode": "build_only" if args.no_run else "test",
                "environment_paths": {name: str(directory) for name, directory in directories.items()},
                "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
                "source_sha256": {name: sha256(root / name) for name in sources},
                "started_unix": time.time(), "exit_code": None}
    run_id = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid.uuid4().hex[:10]
    evidence_dir = output / "runs" / run_id
    evidence_dir.mkdir(parents=True, exist_ok=False)
    manifest["run_id"] = run_id
    manifest_path = evidence_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    start = time.monotonic()
    with (evidence_dir / "bazel.log").open("w") as log:
        result = subprocess.run(command, cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT)
    manifest.update(exit_code=result.returncode, seconds=time.monotonic() - start)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print("Bazel exit code:", result.returncode, "; evidence:", manifest_path)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
