#!/usr/bin/env python3
"""Verify a distributable iOS framework and record its linked dependency inventory."""
import argparse
import hashlib
import json
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
    links = list(build.glob("src/observer/ios/driver/CMakeFiles/seekdb_ios_framework.dir/link.txt"))
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


def package_licenses(source, destination):
    """Preserve available local dependency notices and the complete Rust dependency lock."""
    roots = [source / "deps/ios/sources"]
    cargo_sources = source / "deps/ios/cargo/registry/src"
    if cargo_sources.is_dir():
        roots.extend(path for path in cargo_sources.iterdir() if path.is_dir())
    for root in roots:
        if not root.is_dir():
            continue
        for package in root.iterdir():
            if not package.is_dir():
                continue
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
    if info.get("MinimumOSVersion") != args.deployment or info.get("SeekDBTestHooks") != "disabled":
        raise ValueError("Invalid framework deployment or hook metadata")
    public_header = binary.parent / "Headers/seekdb.h"
    if public_header.read_bytes() != (source / "src/observer/ios/driver/seekdb.h").read_bytes():
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
    expected = set((source / "src/observer/ios/driver/exports.txt").read_text().splitlines())
    actual = {line.split()[-1] for line in output("nm", "-gU", str(binary)).splitlines() if line.strip()}
    if actual != expected:
        raise ValueError("Unexpected ABI exports: " + repr(actual ^ expected))
    markers = output("strings", str(binary))
    if "SEEKDB_IOS_ARTIFACT_HOOK_MODE=disabled" not in markers or "SEEKDB_IOS_ARTIFACT_HOOK_MODE=enabled" in markers:
        raise ValueError("Missing or conflicting disabled test hook marker")
    revision = output("git", "-C", str(source), "rev-parse", "HEAD").strip()
    if info.get("SeekDBSourceRevision") != revision[:12]:
        raise ValueError("Framework metadata does not match HEAD; reconfigure and rebuild")
    if "SEEKDB_IOS_ARTIFACT_BUILD_ID=" + revision[:12] not in markers:
        raise ValueError("Framework binary does not match HEAD; reconfigure and rebuild")
    provenance = {
        "source_revision": revision,
        "source_dirty": bool(output("git", "-C", str(source), "status", "--porcelain").strip()),
        "source_diff_sha256": hashlib.sha256(subprocess.check_output(["git", "-C", str(source), "diff", "HEAD"])).hexdigest(),
        "platform": platform, "architecture": "arm64", "deployment_target": args.deployment,
        "test_hooks": "disabled", "binary_sha256": digest(binary),
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
