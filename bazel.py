#!/usr/bin/env python3
"""Cross-platform Bazel launcher for seekdb."""

import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse
from pathlib import Path
from typing import List, Optional, Tuple


DEFAULT_BUILD_DIRECTORY = "build_bazel"
DEFAULT_LOCAL_CPU_RESOURCES = 80
OB_MAKE_LOCAL_JOBS = 72
DEFAULT_DISK_CACHE_GC_MAX_SIZE = "50G"
DEFAULT_DISK_CACHE_GC_MAX_AGE = "14d"
DEFAULT_DISK_CACHE_GC_IDLE_DELAY = "1m"
MODULE_POLICY_ASPECT = (
    "--aspects=//bazel/architecture:"
    "module_dependency_aspect.bzl%module_dependency_aspect"
)
CONFIGURED_TARGET_COMMANDS = {
    "aquery",
    "build",
    "coverage",
    "cquery",
    "run",
    "test",
}
BAZEL_COMMANDS = CONFIGURED_TARGET_COMMANDS | {
    "analyze-profile",
    "canonicalize-flags",
    "clean",
    "dump",
    "fetch",
    "help",
    "info",
    "license",
    "mobile-install",
    "mod",
    "print_action",
    "query",
    "shutdown",
    "sync",
    "vendor",
    "version",
}


class LauncherError(RuntimeError):
    """A user-facing launcher configuration error."""


def _bazel_cache_directory() -> Path:
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA")
        return (Path(base) if base else Path.home() / "AppData" / "Local") / "bazel"
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Caches" / "bazel"
    xdg_cache = os.environ.get("XDG_CACHE_HOME")
    return (Path(xdg_cache) if xdg_cache else Path.home() / ".cache") / "bazel"


def _environment_local_jobs() -> Optional[int]:
    """Override Bazel's job count only in an ob-make environment."""
    return OB_MAKE_LOCAL_JOBS if shutil.which("ob-make") is not None else None


def _required_bazel_version(root: Path) -> str:
    version_file = root / ".bazelversion"
    try:
        version = version_file.read_text(encoding="utf-8").strip()
    except OSError as error:
        raise LauncherError("cannot read %s: %s" % (version_file, error))
    if not version or any(character.isspace() for character in version):
        raise LauncherError("%s must contain exactly one Bazel version" % version_file)
    return version


def _resolve_bazel(root: Path, override: Optional[str]) -> Path:
    requested = override or os.environ.get("BAZEL") or "bazel"
    executable = shutil.which(requested)
    if executable is None:
        required_version = _required_bazel_version(root)
        raise LauncherError(
            "Bazel %s is required but '%s' was not found; install Bazel or "
            "pass --bazel=/path/to/bazel" % (required_version, requested)
        )
    path = Path(executable).resolve()
    if not path.is_file():
        raise LauncherError("Bazel executable is not a file: %s" % path)
    return path


def _verify_bazel_version(root: Path, executable: Path) -> None:
    required_version = _required_bazel_version(root)
    result = subprocess.run(
        [str(executable), "--version"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    output = (result.stdout or result.stderr).strip()
    prefix = "bazel "
    actual_version = (
        output[len(prefix) :].strip() if output.startswith(prefix) else ""
    )
    if result.returncode != 0 or actual_version != required_version:
        raise LauncherError(
            "Bazel version mismatch: %s requires %s, but %s reported %r"
            % (root, required_version, executable, output)
        )


def _validate_no_whitespace(name: str, value: str) -> None:
    if any(character.isspace() for character in value):
        raise LauncherError("%s must not contain whitespace" % name)


def _parse_ssh_endpoint(endpoint: str) -> Tuple[str, int]:
    parsed = urllib.parse.urlsplit(endpoint)
    try:
        port = parsed.port
    except ValueError as error:
        raise LauncherError("invalid SSH remote-execution port: %s" % error)
    if (
        parsed.scheme != "ssh"
        or not parsed.hostname
        or port is None
        or parsed.password is not None
        or parsed.path not in ("", "/")
        or parsed.query
        or parsed.fragment
    ):
        raise LauncherError(
            "--remote-executor SSH form must be ssh://[user@]host:port"
        )
    host = parsed.hostname
    if ":" in host:
        host = "[%s]" % host
    destination = "%s@%s" % (parsed.username, host) if parsed.username else host
    return destination, port


def _free_local_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def _stop_process(process: Optional[subprocess.Popen]) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def _start_ssh_tunnel(endpoint: str) -> Tuple[subprocess.Popen, str]:
    destination, remote_port = _parse_ssh_endpoint(endpoint)
    ssh = shutil.which("ssh")
    if not ssh:
        raise LauncherError("ssh is required for an ssh:// remote executor")
    local_port = _free_local_port()
    command = [
        ssh,
        "-N",
        "-o",
        "BatchMode=yes",
        "-o",
        "ConnectTimeout=5",
        "-o",
        "ExitOnForwardFailure=yes",
        "-o",
        "ServerAliveCountMax=3",
        "-o",
        "ServerAliveInterval=15",
        "-o",
        "StrictHostKeyChecking=accept-new",
        "-L",
        "127.0.0.1:%d:127.0.0.1:%d" % (local_port, remote_port),
        destination,
    ]
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise LauncherError(
                "failed to establish the remote-execution SSH tunnel to %s"
                % destination
            )
        try:
            with socket.create_connection(("127.0.0.1", local_port), timeout=0.2):
                return process, "grpc://127.0.0.1:%d" % local_port
        except OSError:
            time.sleep(0.1)
    _stop_process(process)
    raise LauncherError(
        "timed out establishing the remote-execution SSH tunnel to %s" % destination
    )


def _bazelrc_quote(value: str) -> str:
    if not any(character.isspace() or character in '\\"' for character in value):
        return value
    return '"%s"' % value.replace("\\", "\\\\").replace('"', '\\"')


def _resolve_build_directory(root: Path, value: Optional[str]) -> Path:
    directory = Path(value).expanduser() if value else Path(DEFAULT_BUILD_DIRECTORY)
    if not directory.is_absolute():
        directory = root / directory
    directory = directory.resolve()
    if directory == root:
        raise LauncherError("--build-dir must not name the workspace root")
    if directory.exists() and not directory.is_dir():
        raise LauncherError("--build-dir does not name a directory: %s" % directory)
    return directory


def _remove_legacy_convenience_symlinks(root: Path) -> None:
    for name in (
        "bazel-bin",
        "bazel-out",
        "bazel-testlogs",
        "bazel-%s" % root.name,
    ):
        path = root / name
        if path.is_symlink():
            path.unlink()


def _write_runtime_bazelrc(
    cache_directory: Path,
    disk_cache: Path,
    repository_cache: Optional[Path],
    disk_cache_gc_max_size: str,
    disk_cache_gc_max_age: str,
    disk_cache_gc_idle_delay: str,
    symlink_prefix: Path,
    remote_cache: str,
    remote_executor: str,
    remote_instance_name: str,
    remote_timeout: str,
    remote_cache_read_only: bool,
    registry: str,
    downloader_config: Optional[Path],
    local_jobs: Optional[int],
) -> Path:
    lines: List[str] = [
        "build --local_resources=cpu=%d" % DEFAULT_LOCAL_CPU_RESOURCES,
        "build --disk_cache=%s" % _bazelrc_quote(disk_cache.as_posix()),
        "build --experimental_disk_cache_gc_max_size=%s"
        % disk_cache_gc_max_size,
        "build --experimental_disk_cache_gc_max_age=%s"
        % disk_cache_gc_max_age,
        "build --experimental_disk_cache_gc_idle_delay=%s"
        % disk_cache_gc_idle_delay,
        "build --symlink_prefix=%s"
        % _bazelrc_quote(symlink_prefix.as_posix().rstrip("/") + "/"),
    ]
    if local_jobs is not None:
        lines.insert(0, "build --jobs=%d" % local_jobs)
    if repository_cache is not None:
        lines.insert(
            0,
            "common --repository_cache=%s"
            % _bazelrc_quote(repository_cache.as_posix()),
        )
    if registry:
        lines.insert(0, "common --registry=%s" % _bazelrc_quote(registry))
    if downloader_config is not None:
        lines.insert(
            0,
            "common --downloader_config=%s"
            % _bazelrc_quote(downloader_config.as_posix()),
        )
    cache_endpoint = remote_cache or remote_executor
    if cache_endpoint:
        lines.append("build --remote_cache=%s" % cache_endpoint)
        if remote_cache_read_only:
            lines.append("build --noremote_upload_local_results")
    if remote_executor:
        fallback_strategy = "local" if os.name == "nt" else "sandboxed"
        lines.extend(
            [
                "build --remote_executor=%s" % remote_executor,
                "build --spawn_strategy=%s" % fallback_strategy,
                "build --strategy=CppCompile=remote,%s" % fallback_strategy,
                # Cargo still owns Rust dependency resolution and writes its
                # registry under ~/.cargo.  Rust genrules opt out of the
                # sandbox explicitly; all other genrules keep sandboxed as
                # their first strategy.
                "build --strategy=Genrule=sandboxed,standalone",
            ]
        )
    if remote_instance_name:
        lines.append("build --remote_instance_name=%s" % remote_instance_name)
    if remote_cache or remote_executor:
        lines.append("build --remote_timeout=%s" % remote_timeout)

    file_descriptor, name = tempfile.mkstemp(
        prefix="runtime.%d." % os.getpid(),
        suffix=".bazelrc",
        dir=str(cache_directory),
    )
    with os.fdopen(file_descriptor, "w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines) + "\n")
    try:
        os.chmod(name, 0o600)
    except OSError:
        pass
    return Path(name)


def _dependency_url(value: str, name: str) -> urllib.parse.SplitResult:
    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme not in ("file", "http", "https"):
        raise LauncherError("%s must use file, http, or https" % name)
    if parsed.scheme == "file":
        if parsed.netloc not in ("", "localhost") or not parsed.path.startswith("/"):
            raise LauncherError("%s must be an absolute local file URL" % name)
    elif not parsed.hostname:
        raise LauncherError("%s has no host" % name)
    if parsed.query or parsed.fragment:
        raise LauncherError("%s must not contain a query or fragment" % name)
    return parsed


def _write_downloader_config(
    cache_directory: Path,
    registry: str,
    mirror: str,
) -> Path:
    registry_url = _dependency_url(registry, "--registry")
    mirror_url = _dependency_url(mirror, "--registry-mirror")
    lines: List[str] = []
    internal_netlocs = sorted(
        {
            parsed.netloc
            for parsed in (registry_url, mirror_url)
            if parsed.scheme in ("http", "https")
        }
    )
    if mirror_url.scheme in ("http", "https"):
        excluded = "|".join(re.escape(value) for value in internal_netlocs)
        lines.append(
            "rewrite (?!(?:%s)/)(.*) %s$1"
            % (excluded, mirror.rstrip("/") + "/")
        )
    for host in sorted(
        {
            parsed.hostname
            for parsed in (registry_url, mirror_url)
            if parsed.hostname
        }
    ):
        lines.append("allow %s" % host)
    lines.append("block *")

    file_descriptor, name = tempfile.mkstemp(
        prefix="downloader.%d." % os.getpid(),
        suffix=".cfg",
        dir=str(cache_directory),
    )
    with os.fdopen(file_descriptor, "w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines) + "\n")
    try:
        os.chmod(name, 0o600)
    except OSError:
        pass
    return Path(name)


def _launcher_arguments(
    arguments: List[str],
) -> Tuple[argparse.Namespace, List[str]]:
    parser = argparse.ArgumentParser(
        description=(
            "seekdb Bazel launcher; unrecognized arguments are passed through to Bazel"
        ),
        epilog=(
            "seekdb management commands: ./bazel.py deps init; "
            "./bazel.py registry sync --root PATH"
        ),
    )
    parser.add_argument(
        "--build-dir",
        metavar="PATH",
        help=(
            "per-invocation Bazel build directory, relative to the workspace "
            "or absolute (default: %s)" % DEFAULT_BUILD_DIRECTORY
        ),
    )
    parser.add_argument(
        "--remote-executor",
        default="",
        metavar="ENDPOINT",
        help=(
            "Bazel REAPI endpoint, or ssh://[user@]host:port for an automatic "
            "tunnel"
        ),
    )
    parser.add_argument(
        "--remote-cache",
        default="",
        metavar="ENDPOINT",
        help="separate Bazel remote-cache endpoint; defaults to the executor endpoint",
    )
    parser.add_argument(
        "--remote-instance-name",
        default="",
        metavar="NAME",
        help="REAPI instance name",
    )
    parser.add_argument(
        "--remote-timeout",
        default="60s",
        metavar="DURATION",
        help="remote operation timeout (default: 60s)",
    )
    parser.add_argument(
        "--remote-cache-read-only",
        action="store_true",
        help="consume remote-cache entries without uploading local action results",
    )
    parser.add_argument(
        "--disk-cache",
        metavar="PATH",
        help="persistent local Bazel disk-cache directory (default: %s)"
        % (_bazel_cache_directory() / "disk-cache"),
    )
    parser.add_argument(
        "--repository-cache",
        metavar="PATH",
        help="override Bazel's default shared repository-cache directory",
    )
    parser.add_argument(
        "--registry",
        default=os.environ.get("SEEKDB_BAZEL_REGISTRY", ""),
        metavar="URL",
        help="authoritative private Bazel registry (or $SEEKDB_BAZEL_REGISTRY)",
    )
    parser.add_argument(
        "--registry-mirror",
        default=os.environ.get("SEEKDB_BAZEL_MIRROR", ""),
        metavar="URL",
        help="private source mirror paired with --registry (or $SEEKDB_BAZEL_MIRROR)",
    )
    parser.add_argument(
        "--disk-cache-gc-max-size",
        default=DEFAULT_DISK_CACHE_GC_MAX_SIZE,
        metavar="SIZE",
        help=(
            "maximum shared disk-cache size before background GC "
            "(default: %s)" % DEFAULT_DISK_CACHE_GC_MAX_SIZE
        ),
    )
    parser.add_argument(
        "--disk-cache-gc-max-age",
        default=DEFAULT_DISK_CACHE_GC_MAX_AGE,
        metavar="DURATION",
        help=(
            "maximum shared disk-cache entry age before background GC "
            "(default: %s)" % DEFAULT_DISK_CACHE_GC_MAX_AGE
        ),
    )
    parser.add_argument(
        "--disk-cache-gc-idle-delay",
        default=DEFAULT_DISK_CACHE_GC_IDLE_DELAY,
        metavar="DURATION",
        help=(
            "Bazel server idle delay before shared disk-cache GC "
            "(default: %s)" % DEFAULT_DISK_CACHE_GC_IDLE_DELAY
        ),
    )
    parser.add_argument(
        "--launcher-cache-dir",
        metavar="PATH",
        help="directory for generated launcher state (default: %s)"
        % _bazel_cache_directory(),
    )
    parser.add_argument(
        "--bazel",
        metavar="PATH",
        help=(
            "Bazel executable; defaults to $BAZEL or bazel from PATH "
            "(never downloaded by this launcher)"
        ),
    )
    return parser.parse_known_args(arguments)


def _run_management_command(root: Path, arguments: List[str]) -> Optional[int]:
    """Run an explicit dependency-management command outside Bazel."""
    if arguments and arguments[0] == "registry":
        registry_tool = root / "tools" / "bazel_registry.py"
        if not registry_tool.is_file():
            raise LauncherError("registry tool not found: %s" % registry_tool)
        if len(arguments) < 2:
            raise LauncherError("usage: ./bazel.py registry sync --root PATH")
        return subprocess.run(
            [sys.executable, str(registry_tool)] + arguments[1:],
            cwd=str(root),
        ).returncode
    if not arguments or arguments[0] != "deps":
        return None
    if arguments != ["deps", "init"]:
        raise LauncherError("usage: ./bazel.py deps init")

    initializer = root / "deps" / "init" / "dep_create.sh"
    if not initializer.is_file():
        raise LauncherError("dependency initializer not found: %s" % initializer)

    environment = os.environ.copy()
    environment["ANDROID_BUILD"] = "false"
    return subprocess.run(
        ["bash", initializer.name],
        cwd=str(initializer.parent),
        env=environment,
    ).returncode


def _inject_module_policy_aspect(arguments: List[str]) -> List[str]:
    """Apply the authoritative module graph to configured-target commands."""
    for index, argument in enumerate(arguments):
        if argument not in BAZEL_COMMANDS:
            continue
        if argument not in CONFIGURED_TARGET_COMMANDS:
            return arguments
        return arguments[: index + 1] + [MODULE_POLICY_ASPECT] + arguments[index + 1 :]
    return arguments


def main(arguments: List[str]) -> int:
    root = Path(__file__).resolve().parent
    dependency_status = _run_management_command(root, arguments)
    if dependency_status is not None:
        return dependency_status

    options, bazel_arguments = _launcher_arguments(arguments)

    bazel = _resolve_bazel(root, options.bazel)
    _verify_bazel_version(root, bazel)
    bazel_arguments = _inject_module_policy_aspect(bazel_arguments)
    build_directory = _resolve_build_directory(root, options.build_dir)
    output_base = build_directory / "output-base"
    default_cache_directory = _bazel_cache_directory()
    cache_directory = (
        Path(options.launcher_cache_dir).expanduser()
        if options.launcher_cache_dir
        else default_cache_directory
    )
    disk_cache = (
        Path(options.disk_cache).expanduser()
        if options.disk_cache
        else default_cache_directory / "disk-cache"
    )
    repository_cache = (
        Path(options.repository_cache).expanduser()
        if options.repository_cache
        else None
    )
    registry = options.registry
    registry_mirror = options.registry_mirror
    remote_executor = options.remote_executor
    remote_cache = options.remote_cache
    remote_instance_name = options.remote_instance_name
    remote_timeout = options.remote_timeout
    disk_cache_gc_max_size = options.disk_cache_gc_max_size
    disk_cache_gc_max_age = options.disk_cache_gc_max_age
    disk_cache_gc_idle_delay = options.disk_cache_gc_idle_delay
    local_jobs = _environment_local_jobs()

    if options.remote_cache_read_only and not (remote_cache or remote_executor):
        raise LauncherError(
            "--remote-cache-read-only requires --remote-cache or --remote-executor"
        )
    if bool(registry) != bool(registry_mirror):
        raise LauncherError("--registry and --registry-mirror must be specified together")

    for name, value in [
        ("--remote-executor", remote_executor),
        ("--remote-cache", remote_cache),
        ("--remote-instance-name", remote_instance_name),
        ("--remote-timeout", remote_timeout),
        ("--disk-cache-gc-max-size", disk_cache_gc_max_size),
        ("--disk-cache-gc-max-age", disk_cache_gc_max_age),
        ("--disk-cache-gc-idle-delay", disk_cache_gc_idle_delay),
    ]:
        if value:
            _validate_no_whitespace(name, value)

    build_directory.mkdir(parents=True, exist_ok=True)
    cache_directory.mkdir(parents=True, exist_ok=True)
    disk_cache.mkdir(parents=True, exist_ok=True)
    if repository_cache is not None:
        repository_cache.mkdir(parents=True, exist_ok=True)
    _remove_legacy_convenience_symlinks(root)
    tunnel: Optional[subprocess.Popen] = None
    runtime_bazelrc: Optional[Path] = None
    downloader_config: Optional[Path] = None
    try:
        effective_executor = remote_executor
        if remote_executor.startswith("ssh://"):
            tunnel, effective_executor = _start_ssh_tunnel(remote_executor)
        if registry:
            downloader_config = _write_downloader_config(
                cache_directory,
                registry,
                registry_mirror,
            )
        runtime_bazelrc = _write_runtime_bazelrc(
            cache_directory=cache_directory,
            disk_cache=disk_cache.resolve(),
            repository_cache=(
                repository_cache.resolve()
                if repository_cache is not None
                else None
            ),
            disk_cache_gc_max_size=disk_cache_gc_max_size,
            disk_cache_gc_max_age=disk_cache_gc_max_age,
            disk_cache_gc_idle_delay=disk_cache_gc_idle_delay,
            symlink_prefix=build_directory,
            remote_cache=remote_cache,
            remote_executor=effective_executor,
            remote_instance_name=remote_instance_name,
            remote_timeout=remote_timeout,
            remote_cache_read_only=options.remote_cache_read_only,
            registry=registry,
            downloader_config=downloader_config,
            local_jobs=local_jobs,
        )
        command = [
            str(bazel),
            "--output_base=%s" % output_base,
            "--bazelrc=%s" % runtime_bazelrc,
        ] + bazel_arguments
        return subprocess.run(command, cwd=str(root)).returncode
    finally:
        _stop_process(tunnel)
        if runtime_bazelrc is not None:
            try:
                runtime_bazelrc.unlink()
            except FileNotFoundError:
                pass
        if downloader_config is not None:
            try:
                downloader_config.unlink()
            except FileNotFoundError:
                pass


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except LauncherError as error:
        print("ERROR: %s" % error, file=sys.stderr)
        sys.exit(2)
    except KeyboardInterrupt:
        sys.exit(130)
