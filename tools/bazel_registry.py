#!/usr/bin/env python3
"""Prepare an internal Bazel registry and warm its source archive mirror."""

import argparse
import base64
import concurrent.futures
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple


DEFAULT_BCR_SOURCE = "https://github.com/bazelbuild/bazel-central-registry.git"
DOWNLOAD_WORKERS = 8


class RegistryError(RuntimeError):
    """A user-facing registry preparation error."""


def _run(command: Sequence[str], cwd: Optional[Path] = None) -> None:
    result = subprocess.run(command, cwd=str(cwd) if cwd else None)
    if result.returncode != 0:
        raise RegistryError("command failed (%d): %s" % (result.returncode, " ".join(command)))


def _validate_registry_checkout(path: Path) -> Path:
    path = path.expanduser().resolve()
    if not (path / "modules").is_dir() or not (path / "bazel_registry.json").is_file():
        raise RegistryError("not a Bazel index registry checkout: %s" % path)
    return path


def _sync_upstream(root: Path, source: str) -> Path:
    source_path = Path(source).expanduser()
    if source_path.exists():
        return _validate_registry_checkout(source_path)

    checkout = root / "upstream-bcr"
    if checkout.exists():
        _validate_registry_checkout(checkout)
        if not (checkout / ".git").is_dir():
            raise RegistryError("managed BCR checkout has no .git directory: %s" % checkout)
        status = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=str(checkout),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if status.returncode != 0:
            raise RegistryError("cannot inspect managed BCR checkout: %s" % status.stderr.strip())
        if status.stdout.strip():
            raise RegistryError("managed BCR checkout is dirty: %s" % checkout)
        _run(["git", "pull", "--ff-only", "--depth=1", "origin", "main"], checkout)
        return checkout

    temporary = Path(tempfile.mkdtemp(prefix="upstream-bcr.", dir=str(root)))
    shutil.rmtree(str(temporary))
    try:
        _run(["git", "clone", "--depth=1", "--branch", "main", source, str(temporary)])
        _validate_registry_checkout(temporary)
        os.replace(str(temporary), str(checkout))
    finally:
        if temporary.exists():
            shutil.rmtree(str(temporary))
    return checkout


def _normalized_base_url(value: str, name: str) -> str:
    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme not in ("file", "http", "https"):
        raise RegistryError("%s must use file, http, or https: %s" % (name, value))
    if parsed.scheme == "file":
        if parsed.netloc not in ("", "localhost") or not parsed.path.startswith("/"):
            raise RegistryError("%s must be an absolute local file URL: %s" % (name, value))
    elif not parsed.hostname:
        raise RegistryError("%s has no host: %s" % (name, value))
    if parsed.query or parsed.fragment:
        raise RegistryError("%s must not contain a query or fragment: %s" % (name, value))
    return value.rstrip("/") + "/"


def _materialize_registry(upstream: Path, registry: Path, mirror_url: str) -> None:
    registry.mkdir(parents=True, exist_ok=True)
    modules_link = registry / "modules"
    expected_target = os.path.relpath(str(upstream / "modules"), str(registry))
    if modules_link.is_symlink():
        if os.readlink(str(modules_link)) != expected_target:
            raise RegistryError(
                "registry modules link points somewhere unexpected: %s" % modules_link
            )
    elif modules_link.exists():
        raise RegistryError("registry modules path is not tool-managed symlink: %s" % modules_link)
    else:
        modules_link.symlink_to(expected_target, target_is_directory=True)

    try:
        metadata = json.loads((upstream / "bazel_registry.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise RegistryError("cannot read upstream bazel_registry.json: %s" % error)
    metadata["mirrors"] = [mirror_url]
    (registry / "bazel_registry.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _registry_relative_path(url: str) -> Optional[Path]:
    parsed = urllib.parse.urlsplit(url)
    marker = "/modules/"
    if marker in parsed.path:
        return Path("modules") / parsed.path.split(marker, 1)[1]
    if parsed.path.endswith("/bazel_registry.json"):
        return Path("bazel_registry.json")
    return None


def _load_locked_sources(
    upstream: Path,
    registry: Path,
    lockfiles: Iterable[Path],
) -> List[Path]:
    source_files: Set[Path] = set()
    for lockfile in lockfiles:
        try:
            lock = json.loads(lockfile.read_text(encoding="utf-8"))
            hashes = lock["registryFileHashes"]
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise RegistryError("cannot read Bazel lockfile %s: %s" % (lockfile, error))
        if not isinstance(hashes, dict):
            raise RegistryError("registryFileHashes is not an object in %s" % lockfile)
        for url, expected_hash in hashes.items():
            relative = _registry_relative_path(url)
            if relative is None:
                continue
            metadata_file = upstream / relative
            try:
                content = metadata_file.read_bytes()
            except OSError as error:
                raise RegistryError(
                    "locked registry file is missing: %s (%s)"
                    % (metadata_file, error)
                )
            candidate_hashes = {hashlib.sha256(content).hexdigest()}
            if relative == Path("bazel_registry.json"):
                candidate_hashes.add(
                    hashlib.sha256((registry / relative).read_bytes()).hexdigest()
                )
            if expected_hash not in candidate_hashes:
                raise RegistryError(
                    "private registry metadata differs from %s: %s" % (lockfile, relative)
                )
            if relative.name == "source.json":
                source_files.add(metadata_file)
    return sorted(source_files)


def _integrity_digest(value: str) -> Tuple[str, bytes]:
    try:
        algorithm, encoded = value.split("-", 1)
        expected = base64.b64decode(encoded, validate=True)
    except (ValueError, TypeError) as error:
        raise RegistryError("invalid source integrity %r: %s" % (value, error))
    if algorithm not in ("sha256", "sha384", "sha512"):
        raise RegistryError("unsupported source integrity algorithm: %s" % algorithm)
    return algorithm, expected


def _matches_integrity(path: Path, integrity: str) -> bool:
    algorithm, expected = _integrity_digest(integrity)
    digest = hashlib.new(algorithm)
    try:
        with path.open("rb") as stream:
            while True:
                chunk = stream.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
    except OSError:
        return False
    return digest.digest() == expected


def _source_archive(source_file: Path, mirror: Path) -> Tuple[str, Path, str]:
    try:
        source = json.loads(source_file.read_text(encoding="utf-8"))
        url = source["url"]
        integrity = source["integrity"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise RegistryError("cannot read archive source %s: %s" % (source_file, error))
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme not in ("http", "https") or not parsed.netloc or not parsed.path:
        raise RegistryError("unsupported archive URL in %s: %s" % (source_file, url))
    if parsed.query or parsed.fragment:
        raise RegistryError("archive URL with query/fragment cannot be mirrored safely: %s" % url)
    destination = mirror / parsed.netloc / parsed.path.lstrip("/")
    return url, destination, integrity


def _download_archive(item: Tuple[str, Path, str]) -> Tuple[Path, bool]:
    url, destination, integrity = item
    if destination.is_file() and _matches_integrity(destination, integrity):
        return destination, False
    destination.parent.mkdir(parents=True, exist_ok=True)
    file_descriptor, temporary_name = tempfile.mkstemp(
        prefix=destination.name + ".",
        suffix=".tmp",
        dir=str(destination.parent),
    )
    os.close(file_descriptor)
    temporary = Path(temporary_name)
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "seekdb-bazel-registry/1"})
        with urllib.request.urlopen(request, timeout=120) as response, temporary.open(
            "wb"
        ) as output:
            shutil.copyfileobj(response, output, length=1024 * 1024)
        if not _matches_integrity(temporary, integrity):
            raise RegistryError("downloaded archive failed integrity verification: %s" % url)
        os.replace(str(temporary), str(destination))
        return destination, True
    except RegistryError:
        raise
    except Exception as error:
        raise RegistryError("cannot mirror %s: %s" % (url, error))
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def _warm_mirror(source_files: Iterable[Path], mirror: Path, jobs: int) -> Tuple[int, int]:
    archives: Dict[Path, Tuple[str, Path, str]] = {}
    for source_file in source_files:
        item = _source_archive(source_file, mirror)
        existing = archives.get(item[1])
        if existing is not None and existing != item:
            raise RegistryError("two sources map to different content at %s" % item[1])
        archives[item[1]] = item

    downloaded = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        futures = [executor.submit(_download_archive, item) for item in archives.values()]
        for future in concurrent.futures.as_completed(futures):
            path, changed = future.result()
            print(("mirrored " if changed else "verified ") + str(path))
            downloaded += int(changed)
    return len(archives), downloaded


def _sync(arguments: argparse.Namespace) -> int:
    root = Path(arguments.root).expanduser().resolve()
    root.mkdir(parents=True, exist_ok=True)
    mirror = root / "mirror"
    mirror.mkdir(parents=True, exist_ok=True)
    mirror_url = _normalized_base_url(
        arguments.mirror_url or mirror.resolve().as_uri(),
        "--mirror-url",
    )
    upstream = _sync_upstream(root, arguments.upstream)
    registry = root / "registry"
    _materialize_registry(upstream, registry, mirror_url)
    lockfiles = [Path(value).expanduser().resolve() for value in arguments.lockfile]
    source_files = _load_locked_sources(upstream, registry, lockfiles)
    archive_count, downloaded = _warm_mirror(source_files, mirror, arguments.jobs)
    print("registry=%s" % registry.resolve().as_uri())
    print("mirror-storage=%s" % mirror.resolve().as_uri())
    print("mirror-url=%s" % mirror_url)
    print("archives=%d downloaded=%d" % (archive_count, downloaded))
    return 0


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    sync = subparsers.add_parser(
        "sync",
        help="sync BCR metadata and mirror archives selected by Bazel lockfiles",
    )
    sync.add_argument("--root", required=True, help="private registry storage root")
    sync.add_argument(
        "--upstream",
        default=DEFAULT_BCR_SOURCE,
        help="official BCR Git URL or an existing BCR checkout",
    )
    sync.add_argument(
        "--mirror-url",
        help="client-visible source mirror URL (default: file URL under --root)",
    )
    sync.add_argument(
        "--lockfile",
        action="append",
        default=[],
        help="Bazel lockfile whose selected archives should be mirrored (repeatable)",
    )
    sync.add_argument(
        "--jobs",
        type=int,
        default=DOWNLOAD_WORKERS,
        help="parallel archive downloads (default: %d)" % DOWNLOAD_WORKERS,
    )
    return parser


def main(arguments: List[str]) -> int:
    parser = _parser()
    options = parser.parse_args(arguments)
    if options.command == "sync":
        if not options.lockfile:
            options.lockfile = [str(Path.cwd() / "MODULE.bazel.lock")]
        if options.jobs < 1:
            raise RegistryError("--jobs must be positive")
        return _sync(options)
    raise RegistryError("unknown command: %s" % options.command)


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except RegistryError as error:
        print("ERROR: %s" % error, file=sys.stderr)
        sys.exit(2)
