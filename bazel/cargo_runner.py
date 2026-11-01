#!/usr/bin/env python3
"""Run Cargo with declared tools/sources and a private, offline Cargo home."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--work-dir", required=True)
parser.add_argument("--vendor", required=True)
parser.add_argument("--cargo", required=True)
parser.add_argument("--rustc", required=True)
parser.add_argument("--sources", required=True)
parser.add_argument("--target", default="")
parser.add_argument("--cc", required=True)
parser.add_argument("--ar", required=True)
parser.add_argument("command", nargs=argparse.REMAINDER)
args = parser.parse_args()
work = Path(args.work_dir).absolute()
# Each action owns the whole directory; no incremental state survives outside
# the declared outputs or leaks in from a user's Cargo configuration.
if work.exists():
    shutil.rmtree(work)
work.mkdir(parents=True)
workspace = work / "workspace"
for source in json.loads(Path(args.sources).read_text()):
    destination = workspace / source
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
vendor = work / "vendor"
with tarfile.open(args.vendor) as archive:
    # Produced from cargo vendor's checksum-verified files by prepare_cargo.py.
    for member in archive.getmembers():
        # The repository emits only regular files with relative crate paths.
        destination = vendor / member.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        with archive.extractfile(member) as source, destination.open("wb") as output:
            shutil.copyfileobj(source, output)
        # Windows cannot remove read-only files when Bazel retries the action.
        destination.chmod(member.mode | 0o200)
cargo_home = work / "cargo-home"
cargo_home.mkdir()
(cargo_home / "config.toml").write_text(
    '[net]\noffline = true\n[source.crates-io]\nreplace-with = "bazel"\n'
    '[source.bazel]\ndirectory = ' + json.dumps(str(vendor)) + '\n'
)
home = work / "home"
home.mkdir()
cargo = Path(args.cargo).absolute()
rustc = Path(args.rustc).absolute()
cc = Path(args.cc).absolute()
ar = Path(args.ar).absolute()
environment = os.environ.copy()
environment.update({
    "HOME": str(home), "USERPROFILE": str(home),
    "CARGO_HOME": str(cargo_home), "CARGO_NET_OFFLINE": "true",
    "CARGO": str(cargo), "RUSTC": str(rustc),
    "CARGO_WORKSPACE": str(workspace),
    "CARGO_TARGET_DIR": str(work / "target"),
})
if os.name != "nt":
    environment["CXXFLAGS"] = "-D_GLIBCXX_USE_CXX11_ABI=1"
# System utilities (shell, make, SDK launchers) belong to the local execution
# platform. Rust/Python/C++ compilers themselves are declared action inputs.
environment["PATH"] = os.pathsep.join([
    str(cargo.parent), str(Path(environment["PYTHONHOME"]).absolute() / "bin"), str(cc.parent),
    environment.get("PATH", os.defpath),
])
environment["PYTHONHOME"] = str(Path(environment["PYTHONHOME"]).absolute())
target = args.target
if not target:
    environment.update({"CC": str(cc), "CXX": str(cc), "AR": str(ar)})
    version = subprocess.check_output([str(rustc), "-vV"], text=True)
    target = next(line.split(": ", 1)[1] for line in version.splitlines() if line.startswith("host: "))
if target:
    target = target.replace("-", "_")
    environment["CC_" + target] = str(cc)
    environment["CXX_" + target] = str(cc)
    environment["AR_" + target] = str(ar)
    # MSVC Rust targets use link.exe syntax, not the C compiler's syntax.
    if not target.endswith("_msvc"):
        environment["CARGO_TARGET_" + target.upper() + "_LINKER"] = str(cc)
command = args.command[1:] if args.command[:1] == ["--"] else args.command
for index, argument in enumerate(command):
    for key in ("CARGO_WORKSPACE", "CARGO_TARGET_DIR", "CARGO"):
        argument = argument.replace("${" + key + "}", environment[key])
    command[index] = argument
status = subprocess.call(command, env=environment)
if status == 0:
    # Only exported archives/headers need caching. Cargo's private workspace,
    # extracted sources and intermediate objects are disposable action scratch.
    shutil.rmtree(work)
    work.mkdir()
raise SystemExit(status)
