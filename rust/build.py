# Copyright (c) 2026 OceanBase.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Build the Cargo workspace and export the C++ artifacts in exports.json."""

import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile


def _write_tar_member(tar, name, data):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mtime = 0
    info.uid = 0
    info.gid = 0
    info.mode = 0o644
    tar.addfile(info, io.BytesIO(data))


def _collect_depfiles(out_dirs, target_dir, archive_path):
    """Collect -MD depfiles from every Cargo build-script OUT_DIR.

    The genrule injects -MD only into CXXFLAGS. cc reads CXXFLAGS only for
    cpp(true) actions, so a depfile is emitted only by C++ compiles; C/asm
    actions (which read CFLAGS) emit none. Every OUT_DIR observed in Cargo's
    build-script-executed stream is scanned, including multiple OUT_DIRs for
    one package_id (e.g. host and target builds). This does not reverse-infer
    a C++ object set from the depfiles found; coverage is argued separately
    from source evidence. Fail closed when the archive is requested but
    nothing usable can be collected.
    """
    if not out_dirs:
        raise SystemExit("no Cargo build-script OUT_DIR observed; cannot collect depfiles")

    target_dir = Path(target_dir).resolve()

    def _stable_out_dir(out_dir):
        # Store OUT_DIR relative to the Cargo target dir rather than as an
        # absolute path: an absolute path embeds $(@D)/output-base and is not
        # stable across workspaces or Bazel output roots.
        try:
            return out_dir.resolve().relative_to(target_dir).as_posix()
        except ValueError:
            raise SystemExit("OUT_DIR is not under --target-dir: " + str(out_dir))

    records = []
    ordered = sorted(out_dirs.items(), key=lambda kv: (_stable_out_dir(kv[1]), str(kv[0])))
    for (package_id, _), out_dir in ordered:
        if not out_dir.is_dir():
            continue
        out_dir_stable = _stable_out_dir(out_dir)
        for dep in sorted(out_dir.rglob("*.d")):
            data = dep.read_bytes()
            content = data.decode("utf-8", "replace")
            if ":" not in content:
                raise SystemExit("malformed depfile (no target rule): " + str(dep))
            records.append({
                "package_id": package_id,
                "out_dir": out_dir_stable,
                "depfile": dep.relative_to(out_dir).as_posix(),
                "sha256": hashlib.sha256(data).hexdigest(),
                "content": content,
            })

    if not records:
        raise SystemExit("no -MD depfile found in any Cargo OUT_DIR; refusing to emit an empty depfile archive")

    for index, record in enumerate(records):
        record["archive_member"] = "depfiles/%04d.d" % index

    manifest = {
        "generator": "rust/build.py: CXXFLAGS -MD, action-local to the Linux Cargo genrule",
        "scope": "all *.d found under Cargo build-script OUT_DIRs; C++ actions only by cc CXXFLAGS semantics",
        "depfile_count": len(records),
        "depfiles": records,
    }
    manifest_bytes = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")

    archive_path = Path(archive_path).resolve()
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive_path, "w") as tar:
        _write_tar_member(tar, "depfile-manifest.json", manifest_bytes)
        for record in records:
            _write_tar_member(tar, record["archive_member"], record["content"].encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cargo", default="cargo")
    parser.add_argument("--profile", default="release")
    parser.add_argument("--target")
    parser.add_argument("--target-dir", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--depfiles-archive")
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parent
    exports = json.loads((workspace / "exports.json").read_text())
    output = Path(args.output).resolve()
    target_dir = Path(args.target_dir).resolve()
    command = [args.cargo, "build", "--workspace", "--profile", args.profile,
               "--manifest-path", str(workspace / "Cargo.toml"),
               "--target-dir", str(target_dir), "--message-format=json-render-diagnostics"]
    if args.target:
        command.extend(["--target", args.target])
    libraries = {}
    build_dirs = {}
    all_out_dirs = {}
    # Preserve inherited GNU Make jobserver descriptors when CMake provides them.
    with subprocess.Popen(command, cwd=workspace, stdout=subprocess.PIPE,
                          text=True, close_fds=False) as cargo:
        for line in cargo.stdout:
            message = json.loads(line)
            if message["reason"] == "build-script-executed":
                out_dir = Path(message["out_dir"])
                # Keep the last OUT_DIR for the existing header lookup below,
                # but retain every distinct OUT_DIR (keyed by package_id AND
                # out_dir) so depfile collection cannot drop a same-package
                # host/target build.
                build_dirs[message["package_id"]] = out_dir
                all_out_dirs[(message["package_id"], str(out_dir))] = out_dir
            elif message["reason"] == "compiler-artifact":
                if "staticlib" in message["target"]["crate_types"]:
                    archive = next(Path(p) for p in message["filenames"]
                                   if p.endswith((".a", ".lib")))
                    libraries[message["target"]["name"]] = (archive, message["package_id"])
        if cargo.wait():
            raise SystemExit(cargo.returncode)
    if args.depfiles_archive:
        _collect_depfiles(all_out_dirs, target_dir, args.depfiles_archive)
    output.mkdir(parents=True, exist_ok=True)
    for name, export in exports.items():
        archive, package = libraries[name]
        shutil.copyfile(archive, output / archive.name)
        for header, source in export["headers"].items():
            destination = output / "include" / header
            destination.parent.mkdir(parents=True, exist_ok=True)
            contents = (build_dirs[package] / source).read_bytes()
            # Preserve header mtimes when only Rust implementation changed.
            if not destination.exists() or destination.read_bytes() != contents:
                destination.write_bytes(contents)


if __name__ == "__main__":
    main()
