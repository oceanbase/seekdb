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

"""Build the Rust static library from Bazel's prepared configuration source."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cargo", default="cargo")
    parser.add_argument("--target")
    parser.add_argument("--target-dir", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("cargo_args", nargs="*")
    args = parser.parse_args()
    workspace = Path(__file__).absolute().parent
    output = Path(args.output).resolve()
    environment = os.environ.copy()
    environment["CONFIG_RS"] = str(Path(environment["CONFIG_RS"]).resolve())
    command = [args.cargo, "build", "--locked", "--workspace", "--release",
               "--manifest-path", str(workspace / "Cargo.toml"),
               "--target-dir", str(Path(args.target_dir).resolve()),
               "--message-format=json-render-diagnostics"]
    if args.target:
        command += ["--target", args.target]
    command += args.cargo_args
    with subprocess.Popen(command, cwd=workspace, env=environment, stdout=subprocess.PIPE,
                          universal_newlines=True) as cargo:
        for line in cargo.stdout:
            message = json.loads(line)
            if (message["reason"] == "compiler-artifact"
                    and message["target"]["name"] == "seekdb_rust"):
                archive = next(Path(p) for p in message["filenames"] if p.endswith((".a", ".lib")))
                shutil.copyfile(archive, output)
        if cargo.wait():
            raise SystemExit(cargo.returncode)


if __name__ == "__main__":
    main()
