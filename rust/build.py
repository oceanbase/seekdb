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
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cargo", default="cargo")
    parser.add_argument("--profile", default="release")
    parser.add_argument("--target")
    parser.add_argument("--target-dir", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("cargo_args", nargs="*",
                        help="extra arguments for cargo build, given after --")
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parent
    exports = json.loads((workspace / "exports.json").read_text())
    output = Path(args.output).resolve()
    target_dir = Path(args.target_dir).resolve()
    command = [args.cargo, "build", "--locked", "--workspace", "--profile", args.profile,
               "--manifest-path", str(workspace / "Cargo.toml"),
               "--target-dir", str(target_dir), "--message-format=json-render-diagnostics"]
    if args.target:
        command.extend(["--target", args.target])
    command.extend(args.cargo_args)
    libraries = {}
    packages = {}
    build_dirs = {}
    with subprocess.Popen(command, cwd=workspace, stdout=subprocess.PIPE,
                          text=True) as cargo:
        for line in cargo.stdout:
            message = json.loads(line)
            if message["reason"] == "build-script-executed":
                build_dirs[message["package_id"]] = Path(message["out_dir"])
            elif message["reason"] == "compiler-artifact":
                packages[message["target"]["name"]] = message["package_id"]
                if "staticlib" in message["target"]["crate_types"]:
                    archive = next(Path(p) for p in message["filenames"]
                                   if p.endswith((".a", ".lib")))
                    libraries[message["target"]["name"]] = archive
        if cargo.wait():
            raise SystemExit(cargo.returncode)
    output.mkdir(parents=True, exist_ok=True)
    copied = set()
    for name, export in exports.items():
        archive = libraries[export.get("library", name)]
        package = packages[export["crate"].replace("-", "_")]
        library_name = archive.name
        if library_name not in copied:
            shutil.copyfile(archive, output / library_name)
            copied.add(library_name)
        for header, source in export["headers"].items():
            destination = output / "include" / header
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(build_dirs[package] / source, destination)


if __name__ == "__main__":
    main()
