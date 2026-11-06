#!/usr/bin/env python3
"""Install the pinned Bazel and keep build outputs and caches in known locations."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
from typing import Dict, List, Tuple


class BuildError(RuntimeError):
    """The seekdb build environment could not be prepared."""


def _host_platform() -> str:
    system = {"Linux": "linux", "Darwin": "darwin", "Windows": "windows"}.get(
        platform.system()
    )
    machine = platform.machine().lower()
    architecture = {
        "x86_64": "x86_64", "amd64": "x86_64",
        "aarch64": "arm64", "arm64": "arm64",
    }.get(machine)
    if not system or not architecture:
        raise BuildError("unsupported Bazel host: %s %s" % (platform.system(), machine))
    return "%s-%s%s" % (system, architecture, ".exe" if system == "windows" else "")


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _download_binary(url: str, destination: Path, checksum: str) -> Path:
    """Verify before atomic installation; use the environment's proxy settings."""
    if destination.is_file() and _sha256_file(destination) == checksum:
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener()
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=destination.name + ".", dir=str(destination.parent)
    )
    temporary = Path(temporary_name)
    display_url = urllib.parse.urlsplit(url)._replace(query="", fragment="").geturl()
    try:
        print("Downloading Bazel: %s" % display_url, file=sys.stderr, flush=True)
        request = urllib.request.Request(url, headers={"User-Agent": "seekdb-bazel-bootstrap/1"})
        with os.fdopen(descriptor, "wb") as output:
            with opener.open(request, timeout=30) as response:
                for chunk in iter(lambda: response.read(1024 * 1024), b""):
                    output.write(chunk)
        if _sha256_file(temporary) != checksum:
            raise BuildError("SHA256 mismatch for %s" % display_url)
        temporary.chmod(0o755)
        os.replace(str(temporary), str(destination))
        return destination
    except (OSError, ValueError) as error:
        raise BuildError("cannot download Bazel from %s: %s" % (display_url, error)) from error
    finally:
        if temporary.exists():
            temporary.unlink()


def _ensure_bazel(root: Path, cache_directory: Path, version: str) -> Path:
    manifest_path = root / "bazel" / "binaries.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        host = _host_platform()
        checksum = manifest["sha256"][host]
        if manifest["version"] != version or not re.fullmatch(r"[0-9a-f]{64}", checksum):
            raise ValueError("version/checksum does not match .bazelversion")
        url = manifest["url_template"]
        if not isinstance(url, str):
            raise ValueError("url must be an HTTP or HTTPS URL")
        url = url.format(version=version, platform=host)
        source = urllib.parse.urlsplit(url)
        if source.scheme not in ("http", "https") or not source.hostname or source.fragment:
            raise ValueError("url must be an HTTP or HTTPS URL without a fragment")
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise BuildError("invalid Bazel binary manifest %s: %s" % (manifest_path, error))
    filename = "bazel-%s-%s" % (version, host)
    destination = cache_directory / "binaries" / version / filename
    return _download_binary(url, destination, checksum)


def _cache_directory() -> Path:
    if os.name == "nt":
        return Path(os.environ["LOCALAPPDATA"]) / "bazel"
    if sys.platform == "darwin":
        return Path.home() / "Library/Caches/bazel"
    return Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "bazel"


def _quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _bazel_options(bazel: Path, command: str) -> Tuple[Dict[str, bool], Dict[str, bool]]:
    """Read option names and value requirements from this Bazel, without caching."""
    with tempfile.TemporaryDirectory(prefix="options.", dir=bazel.parent) as directory:
        result = subprocess.run([
            str(bazel), "--batch", "--ignore_all_rc_files", "--output_base=" + directory,
            "help", "completion",
        ], cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8")
    if result.returncode:
        raise BuildError("cannot read Bazel options: " + result.stderr.strip())
    sections = dict(re.findall(r'^BAZEL_(\w+)="([^"]*)"', result.stdout, re.MULTILINE))
    command_section = "COMMAND_" + command.upper().replace("-", "_") + "_FLAGS"
    if "STARTUP_OPTIONS" not in sections:
        raise BuildError("Bazel completion output is missing startup options")
    if command_section not in sections:
        raise BuildError("unknown Bazel command: " + command)
    return tuple({
        option.split("=", 1)[0]: "=" in option
        for option in sections[section].split()
    } for section in ("STARTUP_OPTIONS", command_section))


def _route_options(
    command: str,
    arguments: List[str],
    startup_options: Dict[str, bool],
    command_options: Dict[str, bool],
) -> Tuple[List[str], List[str], List[str]]:
    """Move startup options before the command, keeping option values together."""
    startup, forwarded = [], [command]
    jobs, remote_executor, remote_config = "", "", False
    takes_value = {**command_options, **startup_options}
    remaining = iter(arguments)
    for argument in remaining:
        if argument == "--":
            forwarded.extend([argument, *remaining])
            break
        if argument.startswith("-o") and not argument.startswith("--"):
            suffix = argument[3:] if argument.startswith("-o=") else argument[2:]
            argument = "--output_base" + ("=" + suffix if argument[2:] else "")
        elif argument == "-j" or re.fullmatch(r"-j[0-9]+", argument):
            argument = "--jobs" + ("=" + argument[2:] if argument[2:] else "")
        name, equals, value = argument.partition("=")
        group = [argument]
        if takes_value.get(name) and not equals:
            try:
                value = next(remaining)
            except StopIteration:
                raise BuildError(name + " requires a value") from None
            group.append(value)
        if name in startup_options:
            startup.extend([name + "=" + value] if takes_value[name] else group)
        else:
            forwarded.extend(group)
            if name == "--jobs":
                jobs = value
            elif name == "--remote_executor":
                remote_executor = value
            elif name == "--config" and value == "remote":
                remote_config = True
    defaults = []
    if (
        command in ("build", "test", "run", "coverage")
        and re.fullmatch(r"[0-9]+", jobs)
        and not remote_executor
        and not remote_config
    ):
        defaults.append("build --local_resources=cpu=" + jobs)
    return startup, forwarded, defaults


def main(arguments: List[str]) -> int:
    root = Path(__file__).resolve().parent
    os.environ.setdefault("RUSTUP_DIST_SERVER", "https://rsproxy.cn")
    os.environ.setdefault("RUSTUP_UPDATE_ROOT", "https://rsproxy.cn/rustup")
    cargo_bin = str((Path(os.environ.get("CARGO_HOME", Path.home() / ".cargo")) / "bin").expanduser().resolve())
    path = os.environ.get("PATH", os.defpath)
    if cargo_bin not in path.split(os.pathsep):
        os.environ["PATH"] = cargo_bin + os.pathsep + path
    if arguments and arguments[0] == "init":
        return subprocess.run([
            sys.executable, str(root / "deps/init/prepare.py"), *arguments[1:],
        ], cwd=root).returncode

    # Find the binary before asking it which options belong before the command.
    bootstrap = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    bootstrap.add_argument("--launcher_cache_dir", type=Path, default=_cache_directory(),
                           help="directory for the downloaded Bazel binary")
    parser = argparse.ArgumentParser(
        parents=[bootstrap],
        description=__doc__,
        usage="%(prog)s <command> [-o PATH] [-j N] [Bazel options] [targets]",
        epilog="Prepare project dependencies with: ./build.py init [--android | --ios [--simulator] | --wasm]",
        allow_abbrev=False,
    )
    parser.add_argument("-o", "--output_base", type=Path, default=Path("build"),
                        help="directory for build state and output links (default: build)")
    if not arguments or arguments[0] in ("-h", "--help"):
        parser.print_help()
        return 0
    command, *arguments = arguments
    if command.startswith("-"):
        parser.error("put the command before its options, for example: build -o build3 seekdb")
    separator = arguments.index("--") if "--" in arguments else len(arguments)
    options, remaining = bootstrap.parse_known_args(arguments[:separator])
    cache = options.launcher_cache_dir.expanduser().resolve()
    version = (root / ".bazelversion").read_text().strip()
    bazel = _ensure_bazel(root, cache, version)
    startup_options, bazel_arguments, concurrency_defaults = _route_options(
        command, remaining + arguments[separator:], *_bazel_options(bazel, command),
    )
    options, startup_options = parser.parse_known_args(startup_options, namespace=options)
    output_base = (root / options.output_base.expanduser()).resolve()
    if output_base == root:
        raise BuildError("--output_base must not name the workspace root")
    output_base.mkdir(parents=True, exist_ok=True)

    # Build policy lives in .bazelrc; paths and explicit local concurrency go
    # here. Bazel flags on the command line override these defaults.
    status_command = (
        "python bazel/build_metadata.py status"
        if os.name == "nt" else " ".join(shlex.quote(argument) for argument in [
            sys.executable,
            str(root / "bazel/build_metadata.py"),
            "status",
        ])
    )
    lines = [
        "common --downloader_config=" + _quote((root / "bazel/downloader.cfg").as_posix()),
        "build --disk_cache=" + _quote((_cache_directory() / "disk-cache").as_posix()),
        "build --symlink_prefix=" + _quote(output_base.as_posix() + "/"),
        "build --workspace_status_command=" + _quote(status_command),
    ]
    lines.extend(concurrency_defaults)
    if os.name == "nt":
        # Starlark can inspect fixed action variables, but not inherited ones.
        # Native build scripts need both the compiler's PATH and shell tools.
        lines.append("build --action_env=" + _quote("PATH=" + os.environ.get("PATH", os.defpath)))
        lines.append("build --repo_env=" + _quote(
            "BAZEL_LLVM=" + (root / "deps/3rd/tools/llvm18").resolve().as_posix()
        ))
    descriptor, name = tempfile.mkstemp(prefix="runtime.", suffix=".bazelrc", dir=cache)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write("\n".join(lines) + "\n")
        return subprocess.run([
            str(bazel), "--output_base=" + str(output_base),
            "--bazelrc=" + name,
        ] + startup_options + bazel_arguments, cwd=root).returncode
    finally:
        Path(name).unlink()


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except (BuildError, OSError) as error:
        print("ERROR: %s" % error, file=sys.stderr)
        sys.exit(2)
    except KeyboardInterrupt:
        sys.exit(130)
