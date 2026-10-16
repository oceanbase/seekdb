#!/usr/bin/env python3
"""Verify a distributable iOS framework and record its linked dependency inventory."""
import argparse
import hashlib
import json
import os
import plistlib
from pathlib import Path
import re
import shlex
import shutil
import subprocess


def output(*command):
    """Run a read-only artifact inspection and fail on command errors."""
    return subprocess.check_output(command, text=True)


def digest(path):
    """Return the SHA256 of a dependency without loading the archive into memory."""
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def archive_inventory(build, source):
    """Resolve static libraries from the actual framework linker command."""
    links = list(build.glob("src/observer/ios/CMakeFiles/seekdb_ios_framework.dir/link.txt"))
    if len(links) != 1:
        raise ValueError("Framework link command is missing")
    tokens = shlex.split(links[0].read_text())
    working = links[0].parents[2]
    search = [Path(token[2:]) for token in tokens if token.startswith("-L")]
    archives = set()
    for token in tokens:
        if token.endswith(".a"):
            candidate = Path(token.removeprefix("-Wl,-force_load,"))
            archives.add(candidate if candidate.is_absolute() else working / candidate)
        elif token.startswith("-l"):
            filename = "lib" + token[2:] + ".a"
            candidates = [directory / filename for directory in search]
            match = next((path for path in candidates if path.is_file()), None)
            if match is not None:
                archives.add(match)
    result = []
    for path in sorted(archives):
        path = path.resolve(strict=True)
        result.append({"path": str(path.relative_to(source)), "sha256": digest(path)})
    return result


def copy_notices(package, destination):
    for notice in package.iterdir():
        if notice.is_file() and re.match(r"(?i)^(license|copying|copyright|notice)", notice.name):
            target = destination / package.name
            target.mkdir(exist_ok=True)
            shutil.copyfile(notice, target / notice.name)
    jemalloc_license = package / "vendor/jemalloc/COPYING"
    if jemalloc_license.is_file():
        target = destination / package.name
        target.mkdir(exist_ok=True)
        shutil.copyfile(jemalloc_license, target / "jemalloc-COPYING")


def cargo_packages(lockfile):
    packages = []
    for block in lockfile.read_text().split("[[package]]")[1:]:
        fields = dict(re.findall(r'^(name|version|source) = "([^"]*)"$', block, re.M))
        if fields.get("source", "").startswith("registry+"):
            packages.append(fields["name"] + "-" + fields["version"])
    return packages


def package_licenses(source, destination):
    """Preserve available local dependency notices and the complete Rust dependency lock."""
    sources = source / "deps/ios/sources"
    if sources.is_dir():
        for package in sources.iterdir():
            if package.is_dir():
                copy_notices(package, destination)
    cargo_home = Path(os.environ.get("CARGO_HOME", Path.home() / ".cargo"))
    registries = [path for path in (cargo_home / "registry/src").glob("*") if path.is_dir()]
    for lockfile in (source / "rust/Cargo.lock", source / "deps/external/Cargo.lock"):
        for name in cargo_packages(lockfile):
            package = next((registry / name for registry in registries if (registry / name).is_dir()), None)
            if package is None:
                raise ValueError("Cargo package source for license notices is missing: " + name)
            copy_notices(package, destination)
    shutil.copyfile(source / "LICENSE", destination / "seekdb-LICENSE")
    shutil.copyfile(source / "rust/Cargo.lock", destination.parent / "rust-Cargo.lock")


def dependency_source_records(source, sdk):
    """Record pinned package versions and source checksums from existing dependency verification."""
    records = []
    sdk_name = "iphonesimulator" if "simulator" in sdk.lower() else "iphoneos"
    prefix = source / "deps/ios" / sdk_name / "devel"
    for marker in sorted((source / "deps/ios" / sdk_name / "build").glob("*/verified.json")):
        package = json.loads(marker.read_text())
        # Ignore historical markers whose installed inputs have since been replaced.
        installed = package.get("outputs", {})
        if not installed or any(not (prefix / name).is_file() or digest(prefix / name) != checksum
                                for name, checksum in installed.items()):
            continue
        records.append({key: package[key] for key in
                        ("package", "version", "sha256", "platform", "architecture", "deployment_target", "builder")})
    if not {"openssl", "zlib"}.issubset({record["package"] for record in records}):
        raise ValueError("Missing current OpenSSL/zlib dependency source verification")
    return records


def main():
    """Check Mach-O identity, dependencies, exports and hooks, then write provenance."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--sdk", required=True)
    parser.add_argument("--deployment", required=True)
    args = parser.parse_args()
    binary, source, build = args.binary.resolve(), args.source.resolve(), args.build.resolve()
    info = plistlib.loads((binary.parent / "Info.plist").read_bytes())
    if info.get("CFBundleExecutable") != "SeekDB" or info.get("CFBundlePackageType") != "FMWK":
        raise ValueError("Invalid framework bundle identity")
    if info.get("MinimumOSVersion") != args.deployment:
        raise ValueError("Invalid framework deployment metadata")
    public_header = binary.parent / "Headers/seekdb.h"
    if public_header.read_bytes() != (source / "src/observer/in_process/seekdb.h").read_bytes():
        raise ValueError("Public C ABI header is missing or changed")
    if "DYLIB" not in output("otool", "-hv", str(binary)):
        raise ValueError("Expected MH_DYLIB")
    if output("lipo", "-archs", str(binary)).strip() != "arm64":
        raise ValueError("Expected only arm64")
    identity = "@rpath/SeekDB.framework/SeekDB"
    if output("otool", "-D", str(binary)).splitlines()[1].strip() != identity:
        raise ValueError("Unexpected install name")
    load_commands = output("otool", "-l", str(binary))
    if "LC_RPATH" in load_commands:
        raise ValueError("Distributable framework must not carry build search paths")
    platform = "IOSSIMULATOR" if "simulator" in args.sdk.lower() else "IOS"
    version = output("xcrun", "vtool", "-show-build", str(binary))
    if not re.search(r"platform\s+" + platform + r"\b", version):
        raise ValueError("Framework platform does not match the selected SDK")
    if not re.search(r"minos\s+" + re.escape(args.deployment) + r"(?:\.0)?\b", version):
        raise ValueError("Unexpected deployment target")
    dependencies = [line.strip().split(" (", 1)[0] for line in output("otool", "-L", str(binary)).splitlines()[1:]]
    if any(dep != identity and not dep.startswith(("/usr/lib/", "/System/Library/")) for dep in dependencies):
        raise ValueError("Non-system dynamic dependency: " + repr(dependencies))
    expected = set((source / "src/observer/ios/exports.txt").read_text().splitlines())
    actual = {line.split()[-1] for line in output("nm", "-gU", str(binary)).splitlines() if line.strip()}
    if actual != expected:
        raise ValueError("Unexpected ABI exports: " + repr(actual ^ expected))
    markers = output("strings", str(binary))
    revision = output("git", "-C", str(source), "rev-parse", "HEAD").strip()
    if info.get("SeekDBSourceRevision") != revision[:12]:
        raise ValueError("Framework metadata does not match HEAD; reconfigure and rebuild")
    if "SEEKDB_IN_PROCESS_ARTIFACT_BUILD_ID=" + revision[:12] not in markers:
        raise ValueError("Framework binary does not match HEAD; reconfigure and rebuild")
    provenance = {
        "source_revision": revision,
        "source_dirty": bool(output("git", "-C", str(source), "status", "--porcelain").strip()),
        "source_diff_sha256": hashlib.sha256(subprocess.check_output(["git", "-C", str(source), "diff", "HEAD"])).hexdigest(),
        "platform": platform, "architecture": "arm64", "deployment_target": args.deployment,
        "binary_sha256": digest(binary),
        "public_header_sha256": digest(public_header),
        "dynamic_dependencies": dependencies, "static_dependencies": archive_inventory(build, source),
        "exported_symbols": sorted(actual),
        "dependency_source_records": dependency_source_records(source, args.sdk),
        "rust_lock_sha256": digest(source / "rust/Cargo.lock"),
    }
    (binary.parent / "build-manifest.json").write_text(json.dumps(provenance, indent=2) + "\n")
    licenses = binary.parent / "Licenses"
    shutil.rmtree(licenses, ignore_errors=True)
    licenses.mkdir()
    package_licenses(source, licenses)
    print("Verified", platform, "arm64 framework:", binary)


if __name__ == "__main__":
    main()
