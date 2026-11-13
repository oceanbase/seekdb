#!/usr/bin/env python3
"""Export verified iOS frameworks and combine their archives into an XCFramework."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

import package_framework

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import binary
import symbols

ROOT = Path(__file__).resolve().parents[2]
MODULE_MAP = 'framework module SeekDB {\n  umbrella header "seekdb.h"\n  export *\n}\n'


def run(arguments, **kwargs):
    subprocess.run([str(value) for value in arguments], check=True, **kwargs)


def verify_framework(framework, sdk, options):
    platform = "IOSSIMULATOR" if sdk == "iphonesimulator" else "IOS"
    minimum, dependencies, exports = package_framework.verify(
        framework / "SeekDB", ROOT / "src/observer/ios/exports.txt", options.source_commit, platform)
    if binary.version_tuple(minimum) != binary.version_tuple(options.minimum_os):
        raise ValueError("iOS framework deployment target does not match " + options.minimum_os)
    manifest = json.loads((framework / "build-manifest.json").read_text())
    expected = {
        "source_revision": options.source_commit, "platform": platform, "architecture": "arm64",
        "deployment_target": minimum, "binary_sha256": binary.digest(framework / "SeekDB"),
        "public_header_sha256": binary.digest(framework / "Headers/seekdb.h"),
        "dynamic_dependencies": dependencies, "exported_symbols": exports,
        "rust_lock_sha256": binary.digest(framework / "rust-Cargo.lock"),
    }
    if any(manifest.get(key) != value for key, value in expected.items()):
        raise ValueError("iOS framework build manifest does not match its contents")
    with (framework / "Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    if (info.get("SeekDBSourceRevision") != options.source_commit
            or info.get("MinimumOSVersion") != minimum
            or info.get("CFBundleExecutable") != "SeekDB"
            or info.get("CFBundleSupportedPlatforms") != [
                "iPhoneSimulator" if sdk == "iphonesimulator" else "iPhoneOS"]):
        raise ValueError("iOS framework Info.plist does not match its target")
    if not (framework / "Licenses.tar").is_file():
        raise ValueError("iOS framework licenses are missing")


def swift_import(framework, sdk, options):
    sdk_path = subprocess.check_output(["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True).strip()
    target = "arm64-apple-ios" + options.minimum_os
    if sdk == "iphonesimulator":
        target += "-simulator"
    run(["xcrun", "--sdk", sdk, "swiftc", "-typecheck", "-target", target, "-sdk", sdk_path,
         "-F", framework.parent, "-"],
        input="import SeekDB\nlet _: SeekdbHandle? = nil\nlet _ = SEEKDB_SUCCESS\n", text=True)


def write_distribution(prefix, kind, variant, options, inputs=None, built_at=None, **extra):
    built_at = built_at or datetime.now(timezone.utc).replace(microsecond=0)
    files = {path.relative_to(prefix).as_posix(): {
        "sha256": binary.digest(path), "size": metadata.st_size,
    } for path, metadata in sorted(binary.entries(prefix)) if stat.S_ISREG(metadata.st_mode)}
    manifest = {
        "format": kind, "schema_version": 1, "version": options.version,
        "source_commit": options.source_commit, "built_at": built_at.isoformat().replace("+00:00", "Z"),
        "platform": {"os": "ios", "arch": "arm64", "minimum_os": options.minimum_os, "variant": variant},
        "files": files,
    }
    manifest.update(extra)
    if inputs is not None:
        manifest["input_archives"] = inputs
    (prefix / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    checksums = {name: info["sha256"] for name, info in files.items()}
    checksums["manifest.json"] = binary.digest(prefix / "manifest.json")
    (prefix / "SHA256SUMS").write_text("".join(
        "%s  %s\n" % (digest, name) for name, digest in sorted(checksums.items())))
    suffix = "-simulator" if variant == "simulator" else ""
    name = "%s-%s-ios%s%s-arm64-%s-sdb%s.zip" % (
        kind, options.version, options.minimum_os, suffix,
        built_at.strftime("%Y%m%dT%H%M%SZ"), options.source_commit[:12])
    options.output.mkdir(parents=True, exist_ok=True)
    archive = options.output / name
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as stream:
        for path in sorted(prefix.rglob("*")):
            if path.is_file():
                stream.write(path, path.relative_to(prefix).as_posix())
    archive.with_name(name + ".sha256").write_text("%s  %s\n" % (binary.digest(archive), name))
    print(archive, flush=True)
    return archive


def load_distribution(archive, destination, options):
    expected = archive.with_name(archive.name + ".sha256").read_text().split()
    if expected != [binary.digest(archive), archive.name]:
        raise ValueError("iOS archive checksum mismatch: " + archive.name)
    destination.mkdir()
    seen = set()
    with zipfile.ZipFile(archive) as stream:
        for member in stream.infolist():
            path = binary.relative(member.filename)
            key = path.as_posix().casefold()
            mode = member.external_attr >> 16
            if key in seen or mode & 0o7000 or not (
                    stat.S_ISREG(mode) or (member.is_dir() and stat.S_ISDIR(mode))):
                raise ValueError("Unsupported or duplicate iOS archive member: " + member.filename)
            seen.add(key)
            target = destination / path
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with stream.open(member) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
                target.chmod(mode & 0o777)
    manifest = json.loads((destination / "manifest.json").read_text())
    if (manifest.get("format") != "seekdb-framework" or manifest.get("schema_version") != 1
            or manifest.get("version") != options.version
            or manifest.get("source_commit") != options.source_commit):
        raise ValueError("iOS archive does not match the selected source/version")
    platform = manifest.get("platform", {})
    variant = platform.get("variant")
    expected_platform = {"os": "ios", "arch": "arm64", "minimum_os": options.minimum_os, "variant": variant}
    if platform != expected_platform or variant not in ("device", "simulator"):
        raise ValueError("iOS archive target does not match the selected platform")
    actual = {path.relative_to(destination).as_posix()
              for path, mode in binary.entries(destination) if stat.S_ISREG(mode.st_mode)}
    files = manifest["files"]
    if actual != set(files) | {"manifest.json", "SHA256SUMS"}:
        raise ValueError("iOS archive file set does not match its manifest")
    checksums = {}
    for name, info in files.items():
        path = destination / binary.relative(name)
        if path.stat().st_size != info["size"] or binary.digest(path) != info["sha256"]:
            raise ValueError("iOS archive file checksum mismatch: " + name)
        checksums[name] = info["sha256"]
    checksums["manifest.json"] = binary.digest(destination / "manifest.json")
    expected_sums = "".join("%s  %s\n" % (digest, name) for name, digest in sorted(checksums.items()))
    if (destination / "SHA256SUMS").read_text() != expected_sums:
        raise ValueError("iOS archive checksum list does not match its manifest")
    sdk = "iphonesimulator" if variant == "simulator" else "iphoneos"
    framework = destination / "SeekDB.framework"
    verify_framework(framework, sdk, options)
    symbols.verify_stripped(framework)
    if (framework / "Modules/module.modulemap").read_text() != MODULE_MAP:
        raise ValueError("Unexpected iOS framework module map")
    return framework, variant


def framework(options, temporary):
    destination = temporary / "package"
    destination.mkdir()
    result = destination / "SeekDB.framework"
    shutil.copytree(options.input, result)
    verify_framework(result, options.sdk, options)
    debug = temporary / "debug-info"
    records = symbols.separate(destination, debug, "ios", options.input / "SeekDB",
                               Path("SeekDB.framework/SeekDB"))
    manifest_path = result / "build-manifest.json"
    manifest = json.loads(manifest_path.read_text())
    manifest["binary_sha256"] = binary.digest(result / "SeekDB")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    verify_framework(result, options.sdk, options)
    (result / "Modules").mkdir(exist_ok=True)
    (result / "Modules/module.modulemap").write_text(MODULE_MAP)
    swift_import(result, options.sdk, options)
    variant = "simulator" if options.sdk == "iphonesimulator" else "device"
    built_at = datetime.now(timezone.utc).replace(microsecond=0)
    related = {}
    if records:
        archive = write_distribution(debug, "seekdb-debug-info", variant, options, built_at=built_at, binaries=records)
        related = {"archive": archive.name, "sha256": binary.digest(archive)}
    write_distribution(destination, "seekdb-framework", variant, options, built_at=built_at, debug_info=related)


def xcframework(options, temporary):
    archives = sorted(options.input.glob("seekdb-framework-*.zip"))
    if len(archives) != 2:
        raise ValueError("XCFramework requires exactly one device and one simulator archive")
    frameworks = {}
    inputs = {}
    for index, archive in enumerate(archives):
        result, variant = load_distribution(archive, temporary / ("input-" + str(index)), options)
        if variant in frameworks:
            raise ValueError("XCFramework contains duplicate iOS variants")
        frameworks[variant] = result
        inputs[archive.name] = binary.digest(archive)
    if set(frameworks) != {"device", "simulator"}:
        raise ValueError("XCFramework requires both iOS variants")
    destination = temporary / "package"
    destination.mkdir()
    result = destination / "SeekDB.xcframework"
    run(["xcodebuild", "-create-xcframework", "-framework", frameworks["device"],
         "-framework", frameworks["simulator"], "-output", result])
    with (result / "Info.plist").open("rb") as stream:
        libraries = plistlib.load(stream)["AvailableLibraries"]
    variants = set()
    for library in libraries:
        variant = library.get("SupportedPlatformVariant", "device")
        if (library.get("SupportedPlatform") != "ios" or library.get("SupportedArchitectures") != ["arm64"]
                or library.get("LibraryPath") != "SeekDB.framework" or variant not in frameworks
                or variant in variants):
            raise ValueError("Unexpected XCFramework platform/architecture")
        variants.add(variant)
        sdk = "iphonesimulator" if variant == "simulator" else "iphoneos"
        nested = result / binary.relative(library["LibraryIdentifier"]) / "SeekDB.framework"
        verify_framework(nested, sdk, options)
        symbols.verify_stripped(nested)
        swift_import(nested, sdk, options)
    if variants != {"device", "simulator"}:
        raise ValueError("XCFramework is missing an iOS variant")
    write_distribution(destination, "seekdb-xcframework", "device+simulator", options, inputs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("format", choices=("framework", "xcframework"))
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--minimum-os", required=True)
    parser.add_argument("--sdk", choices=("iphoneos", "iphonesimulator"))
    options = parser.parse_args()
    if (not re.fullmatch(r"[0-9A-Za-z.+~_]+", options.version)
            or not re.fullmatch(r"[0-9a-f]{40}", options.source_commit)
            or not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", options.minimum_os)):
        parser.error("Invalid iOS package version, source commit, or deployment target")
    if options.format == "framework" and not options.sdk:
        parser.error("Framework export requires --sdk")
    with tempfile.TemporaryDirectory(prefix="ios-distribution-") as directory:
        {"framework": framework, "xcframework": xcframework}[options.format](options, Path(directory))


if __name__ == "__main__":
    main()
