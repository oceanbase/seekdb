#!/usr/bin/env python3
import argparse
import hashlib
import io
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tarfile


def output(*command):
    return subprocess.check_output(command, text=True)


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def verify(binary, exports, revision, platform):
    if "DYLIB" not in output("xcrun", "otool", "-hv", str(binary)):
        raise ValueError("Expected MH_DYLIB")
    if output("xcrun", "lipo", "-archs", str(binary)).strip() != "arm64":
        raise ValueError("Expected only arm64")
    identity = "@rpath/SeekDB.framework/SeekDB"
    if output("xcrun", "otool", "-D", str(binary)).splitlines()[1].strip() != identity:
        raise ValueError("Unexpected install name")
    if "LC_RPATH" in output("xcrun", "otool", "-l", str(binary)):
        raise ValueError("Distributable framework must not carry build search paths")
    build = output("xcrun", "vtool", "-show-build", str(binary))
    if re.search(r"platform\s+(\S+)", build).group(1) != platform:
        raise ValueError("Framework platform does not match the selected SDK")
    minimum_os = re.search(r"minos\s+(\S+)", build).group(1)
    dependencies = [line.strip().split(" (", 1)[0]
                    for line in output("xcrun", "otool", "-L", str(binary)).splitlines()[1:]]
    if any(dep != identity and not dep.startswith(("/usr/lib/", "/System/Library/")) for dep in dependencies):
        raise ValueError("Non-system dynamic dependency: " + repr(dependencies))
    expected = set(exports.read_text().splitlines())
    actual = {line.split()[-1] for line in output("xcrun", "nm", "-gU", str(binary)).splitlines() if line.strip()}
    if actual != expected:
        raise ValueError("Unexpected ABI exports: " + repr(actual ^ expected))
    markers = output("strings", str(binary))
    if "SEEKDB_IN_PROCESS_ARTIFACT_BUILD_ID=" + revision not in markers:
        raise ValueError("Framework binary does not match its stamped source revision")
    return minimum_os, dependencies, sorted(actual)


def package_licenses(destination, licenses, cargo_sources):
    with tarfile.open(destination, "w") as archive:
        for path in sorted(licenses):
            content = path.read_bytes()
            entry = tarfile.TarInfo(str(path))
            entry.size = len(content)
            entry.mode = 0o644
            archive.addfile(entry, io.BytesIO(content))
        with tarfile.open(cargo_sources) as source:
            for member in source:
                if member.isfile() and re.match(r"(?i)^(license|copying|copyright|notice)", Path(member.name).name):
                    content = source.extractfile(member).read()
                    entry = tarfile.TarInfo("rust/" + member.name)
                    entry.size = len(content)
                    entry.mode = 0o644
                    archive.addfile(entry, io.BytesIO(content))


def main():
    parser = argparse.ArgumentParser(description="Verify and package an iOS framework")
    for name in ("binary", "config", "header", "output", "exports", "link-inputs", "rust-lock", "cargo-sources"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--platform", choices=["IOS", "IOSSIMULATOR"], required=True)
    parser.add_argument("--source-metadata", type=Path, nargs="+", required=True)
    parser.add_argument("--licenses", type=Path, nargs="+", required=True)
    args = parser.parse_args()
    revision = re.search(r'#define SEEKDB_IN_PROCESS_BUILD_ID "([^"]+)"', args.config.read_text()).group(1)
    minimum_os, dependencies, exports = verify(args.binary, args.exports, revision, args.platform)
    destination = args.output
    (destination / "Headers").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.header, destination / "Headers/seekdb.h")
    info = {
        "CFBundleExecutable": "SeekDB", "CFBundleIdentifier": "org.oceanbase.seekdb",
        "CFBundleName": "SeekDB", "CFBundlePackageType": "FMWK",
        "CFBundleVersion": "1", "CFBundleShortVersionString": "1.0",
        "CFBundleSupportedPlatforms": [{"IOS": "iPhoneOS", "IOSSIMULATOR": "iPhoneSimulator"}[args.platform]],
        "MinimumOSVersion": minimum_os, "SeekDBSourceRevision": revision,
    }
    (destination / "Info.plist").write_bytes(plistlib.dumps(info))
    archives = sorted({Path(line) for line in args.link_inputs.read_text().splitlines() if line.endswith(".a")})
    records = [json.loads(path.read_text()) for path in args.source_metadata]
    manifest = {
        "source_revision": revision, "platform": args.platform,
        "architecture": "arm64", "deployment_target": minimum_os,
        "binary_sha256": digest(args.binary), "public_header_sha256": digest(args.header),
        "dynamic_dependencies": dependencies, "exported_symbols": exports,
        "static_dependencies": [{"path": str(path), "sha256": digest(path)} for path in archives],
        "dependency_source_records": records, "rust_lock_sha256": digest(args.rust_lock),
    }
    (destination / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    shutil.copyfile(args.rust_lock, destination / "rust-Cargo.lock")
    package_licenses(destination / "Licenses.tar", args.licenses, args.cargo_sources)


if __name__ == "__main__":
    main()
