#!/usr/bin/env python3
"""Prepare seekdb dependencies for native and cross-platform builds."""

import argparse
import hashlib
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import urllib.request


ROOT = Path(__file__).resolve().parents[2]
PROFILES = Path(__file__).resolve().parent
EL_RELEASES = ((9, "el9"), (8, "el8"), (7, "el7"))
LINUX_RELEASES = {
    "rhel": EL_RELEASES,
    "centos": EL_RELEASES,
    "almalinux": EL_RELEASES[:2],
    "rocky": EL_RELEASES[:2],
    "alinux": ((3, "al8"), (2.1903, "el7")),
    "alios": ((8, "el8"), (7, "el7")),
    "anolis": ((23, "el9"), (8, "el8"), (7, "el7")),
    "ubuntu": ((22.04, "el9"), (16.04, "el7")),
    "debian": ((12, "el9"), (9, "el7")),
    "fedora": ((33, "el7"),),
    "kylin": ((10, "el8"),),
    "openEuler": ((22, "el9"),),
    "opensuse-leap": ((15, "el7"),),
    "sles": ((15, "el7"),),
    "uos": ((20, "el7"),),
    "tencentos": ((3.1, "el8"),),
    "arch": ((0, "el8"),),
    "garuda": ((0, "el8"),),
}


def host_profile():
    system = platform.system()
    arch = {"amd64": "x86_64", "arm64": "aarch64"}.get(
        platform.machine().lower(), platform.machine().lower())
    if system == "Windows":
        tag = "windows." + arch
    elif system == "Darwin":
        arm = subprocess.run(["sysctl", "-n", "hw.optional.arm64"],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True)
        if arch == "aarch64" or arm.stdout.strip() == "1":
            arch = "arm64"
        major = int(subprocess.check_output(["sw_vers", "-productVersion"], universal_newlines=True).split(".")[0])
        release = next((v for v in (27, 15, 13) if major >= v), None)
        tag = "macos%s.%s" % (release, arch)
    elif system == "Linux":
        release = {}
        for line in Path("/etc/os-release").read_text().splitlines():
            key, separator, value = line.partition("=")
            if separator:
                release[key] = shlex.split(value)[0] if value else ""
        version = re.search(r"\d+(?:\.\d+)?", release.get("VERSION_ID", "0"))
        version_number = float(version.group()) if version else 0
        family = next((tag for minimum, tag in LINUX_RELEASES.get(release["ID"], ())
                       if version_number >= minimum), None)
        tag = "%s.%s" % (family, arch)
    else:
        raise ValueError("Unsupported host: " + system)
    profile = PROFILES / ("oceanbase." + tag + ".deps")
    if not profile.is_file():
        raise ValueError("No dependencies profile for this host: " + profile.name)
    return profile


def packages(profile):
    sections = {}
    for raw in profile.read_text(encoding="utf-8-sig").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = sections.setdefault(line[1:-1], [])
        else:
            section.append(line)
    repositories = {}
    for name, lines in sections.items():
        if name.startswith("target-"):
            fields = dict(line.split("=", 1) for line in lines)
            repositories[name[7:]] = fields["repo"].rstrip("/")
    result = []
    for section, lines in sections.items():
        if section.startswith("target-"):
            continue
        for line in lines:
            filename, *options = line.split()
            target = dict(option.split("=", 1) for option in options).get("target", "default")
            result.append((filename, repositories[target] + "/" + filename))
    if not result:
        raise ValueError("Empty dependencies profile: " + str(profile))
    return result


def download(url, archive):
    partial = archive.with_name(archive.name + ".download")
    try:
        with urllib.request.urlopen(url, timeout=60) as source, partial.open("wb") as output:
            shutil.copyfileobj(source, output)
        partial.replace(archive)
    finally:
        if partial.exists():
            partial.unlink()


def unpack(archive, destination):
    if archive.suffix != ".rpm":
        subprocess.run(["tar", "-xf", str(archive), "-C", str(destination),
                        "--strip-components=1"], check=True)
    else:
        with subprocess.Popen(["rpm2cpio", str(archive)], stdout=subprocess.PIPE) as rpm:
            try:
                subprocess.run(["cpio", "-di", "-u", "--quiet"], cwd=destination,
                               stdin=rpm.stdout, check=True)
            finally:
                rpm.stdout.close()
            if rpm.wait():
                raise RuntimeError("rpm2cpio failed: " + str(archive))


def remove_directory(path):
    if path.is_symlink():
        path.unlink()
    elif path.exists():
        shutil.rmtree(path)


def ready(destination, key):
    return (destination / key).is_file() and (destination / "DONE").is_file()


def populate(profile, destination, key, archive_directory):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".deps-", dir=destination.parent) as temporary:
        stage = Path(temporary) / "3rd"
        (stage / "pkg").mkdir(parents=True)
        for filename, url in packages(profile):
            print("Preparing " + filename, flush=True)
            archive = stage / "pkg" / filename
            cached = archive_directory / filename
            if cached.is_file():
                shutil.copyfile(cached, archive)
            else:
                download(url, archive)
            unpack(archive, stage)
        (stage / key).touch()
        (stage / "DONE").touch()
        remove_directory(destination)
        stage.rename(destination)


def install(profile, destination, cache_directory=None):
    # Retain the existing cache identity so initialized SDKs remain reusable.
    key = hashlib.md5(profile.read_bytes()).hexdigest()
    if ready(destination, key):
        print("Dependencies ready: " + str(destination), flush=True)
        return
    if cache_directory is None:
        populate(profile, destination, key, destination / "pkg")
        return

    import fcntl

    cache_directory.mkdir(parents=True, exist_ok=True)
    cached = cache_directory / key / "3rd"
    with (cache_directory / (key + ".lock")).open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if not ready(cached, key):
            populate(profile, cached, key, destination / "pkg")
    destination.parent.mkdir(parents=True, exist_ok=True)
    remove_directory(destination)
    destination.symlink_to(cached, target_is_directory=True)
    print("Dependencies ready: " + str(destination), flush=True)


def prepare_rust():
    if not shutil.which("rustup") or not shutil.which("cargo"):
        with tempfile.TemporaryDirectory(prefix="rustup-init.") as directory:
            if os.name == "nt":
                arch = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64", "aarch64": "aarch64"}[platform.machine().lower()]
                installer = Path(directory) / "rustup-init.exe"
                url = os.environ["RUSTUP_UPDATE_ROOT"].rstrip("/") + "/dist/" + arch + "-pc-windows-msvc/rustup-init.exe"
                command = [str(installer)]
            else:
                installer = Path(directory) / "rustup-init.sh"
                url = "https://rsproxy.cn/rustup-init.sh"
                command = ["sh", str(installer)]
            print("Installing rustup", flush=True)
            download(os.environ.get("RUSTUP_INIT_URL", url), installer)
            subprocess.run(command + ["-y", "--profile", "minimal", "--default-toolchain", "none",
                                      "--no-modify-path"], check=True)
    print("Preparing Rust toolchain from rust/rust-toolchain.toml", flush=True)
    environment = dict(os.environ, RUSTUP_AUTO_INSTALL="1")
    environment.pop("RUSTUP_TOOLCHAIN", None)
    subprocess.run(["rustup", "show", "active-toolchain"], cwd=ROOT / "rust", env=environment, check=True)


def main(arguments):
    parser = argparse.ArgumentParser(prog="build.py init", description=__doc__)
    targets = parser.add_mutually_exclusive_group()
    targets.add_argument("--android", action="store_true")
    targets.add_argument("--ios", action="store_true")
    targets.add_argument("--wasm", action="store_true")
    parser.add_argument("--simulator", action="store_true")
    options = parser.parse_args(arguments)
    if options.simulator and not options.ios:
        parser.error("--simulator requires --ios")
    if os.name == "nt" and (options.android or options.ios or options.wasm):
        parser.error("Android/iOS/Wasm dependency initialization requires a Unix host")
    if options.ios:
        sdk = "iphonesimulator" if options.simulator else "iphoneos"
        if sys.platform != "darwin" or subprocess.run(
            ["xcrun", "--sdk", sdk, "--show-sdk-path"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        ).returncode:
            raise ValueError("iOS SDK unavailable; install Xcode and set DEVELOPER_DIR to its Contents/Developer directory")

    profile = host_profile()
    prepare_rust()
    cache = os.environ.get("DEP_CACHE_DIR") if os.name != "nt" else None
    install(profile, ROOT / "deps/3rd", Path(cache).expanduser().resolve() if cache else None)
    if options.wasm:
        subprocess.run([sys.executable, str(PROFILES / "wasm/build.py")], cwd=ROOT, check=True)
    if options.android:
        install(PROFILES / "oceanbase.android.arm64.deps", ROOT / "deps/android")
    if options.ios or options.android:
        target = "aarch64-linux-android" if options.android else (
            "aarch64-apple-ios-sim" if options.simulator else "aarch64-apple-ios")
        subprocess.run(["rustup", "target", "add", target], cwd=ROOT / "rust", check=True)
    if options.ios:
        subprocess.run([
            sys.executable, str(PROFILES / "ios/build.py"), "--reuse", "--jobs", str(os.cpu_count() or 1),
        ] + (["--simulator"] if options.simulator else []), cwd=ROOT, check=True)
        subprocess.run([
            sys.executable, str(ROOT / "build.py"), "build",
            "--config=" + ("ios-simulator" if options.simulator else "ios"),
            "//bazel/sdk_sources:source_sdk",
        ], cwd=ROOT, check=True)


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print("ERROR: %s" % error, file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)
