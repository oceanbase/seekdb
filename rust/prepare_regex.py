# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
"""Apply the pinned SQL regexp dependency patch in a private Cargo workspace."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare_workspace(original, build, cargo, environment):
    try:
        build.relative_to(original)
    except ValueError:
        pass
    else:
        raise SystemExit("SQL regexp action scratch must be outside its input workspace")
    spec_path = original / "regex-engine/fancy-regex-patch.json"
    spec = json.loads(spec_path.read_text())
    if spec["name"] != "fancy-regex" or spec["version"] != "0.20.0":
        raise SystemExit("Unexpected SQL regexp dependency patch")

    def metadata(workspace, options, locked):
        command = [cargo, *options, "metadata", "--offline", "--format-version", "1",
                   "--manifest-path", str(workspace / "Cargo.toml")]
        if locked:
            command.append("--locked")
        return json.loads(subprocess.check_output(
            command, cwd=workspace, env=environment, universal_newlines=True))

    before = metadata(original, [], True)
    matches = [p for p in before["packages"]
               if p["name"] == spec["name"] and p["version"] == spec["version"]]
    if len(matches) != 1 or not (matches[0]["source"] or "").startswith("registry+"):
        raise SystemExit("SQL regexp patch requires the exact locked registry package")
    source = Path(matches[0]["manifest_path"]).parent
    dependency = build / "patched-crates/fancy-regex-0.20.0"
    # Keep the complete upstream crate, including licenses, tests and examples.
    # Never edit the registry/vendor source or its checksum metadata.
    shutil.copytree(source, dependency)
    expected = {entry["path"] for entry in spec["files"]}
    if expected != {"src/lib.rs", "src/parse.rs", "src/parse_flags.rs"}:
        raise SystemExit("Unexpected SQL regexp patch file set")
    for entry in spec["files"]:
        target = dependency / entry["path"]
        data = target.read_bytes()
        if hashlib.sha256(data).hexdigest() != entry["input_sha256"]:
            raise SystemExit("SQL regexp patch input changed: " + entry["path"])
        for old, new in entry["replacements"]:
            if data.count(old.encode()) != 1:
                raise SystemExit("SQL regexp patch context changed: " + entry["path"])
            data = data.replace(old.encode(), new.encode())
        if hashlib.sha256(data).hexdigest() != entry["output_sha256"]:
            raise SystemExit("SQL regexp patch output changed: " + entry["path"])
        target.write_bytes(data)

    source_files = {p.relative_to(source).as_posix(): digest(p)
                    for p in source.rglob("*") if p.is_file()}
    copied_files = {p.relative_to(dependency).as_posix(): digest(p)
                    for p in dependency.rglob("*") if p.is_file()}
    if source_files.keys() != copied_files.keys():
        raise SystemExit("SQL regexp private source copy is incomplete")
    if {p for p in source_files if source_files[p] != copied_files[p]} != expected:
        raise SystemExit("SQL regexp private source copy has unexpected modifications")

    workspace = build / "workspace"
    # Preserve any existing platform overlay, notably the Wasm spdlog patch.
    shutil.copytree(original, workspace)
    options = ["--config", "patch.crates-io.fancy-regex.path=" + json.dumps(str(dependency))]
    lock_before = digest(workspace / "Cargo.lock")
    # Resolve only the declared private path replacement, then compile --locked.
    after = metadata(workspace, options, False)
    registry = lambda packages: {(p["name"], p["version"], p["source"])
                                 for p in packages if p["source"] and p["name"] != spec["name"]}
    if registry(before["packages"]) != registry(after["packages"]):
        raise SystemExit("SQL regexp overlay changed unrelated registry dependencies")
    patched = [p for p in after["packages"] if p["name"] == spec["name"]]
    if (len(patched) != 1 or patched[0]["version"] != spec["version"]
            or patched[0]["source"] is not None
            or Path(patched[0]["manifest_path"]).resolve() != (dependency / "Cargo.toml").resolve()):
        raise SystemExit("SQL regexp path overlay did not resolve to the patched source")
    metadata(workspace, options, True)
    receipt = {"reason": "sql-regexp-overlay", "workspace": str(workspace),
               "config": options, "source": str(source), "dependency": str(dependency),
               "patch_sha256": digest(spec_path), "input_lock_sha256": lock_before,
               "effective_lock_sha256": digest(workspace / "Cargo.lock"),
               "source_files": source_files, "patched_files": copied_files}
    (build / "overlay.json").write_text(json.dumps(receipt, indent=2))
    summary = {key: value for key, value in receipt.items()
               if key not in ("source_files", "patched_files")}
    summary.update(source_file_count=len(source_files),
                   source_manifest_sha256=hashlib.sha256(
                       json.dumps(source_files, sort_keys=True).encode()).hexdigest(),
                   patched_files={name: copied_files[name] for name in sorted(expected)})
    print(json.dumps(summary), flush=True)
    return workspace, options
