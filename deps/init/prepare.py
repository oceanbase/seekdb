#!/usr/bin/env python3
"""Prepare seekdb dependencies for native and cross-platform builds."""

import argparse
import json
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


def locked_packages(lock_file, system, architecture):
    if not lock_file.is_file():
        return []
    lock = json.loads(lock_file.read_text(encoding="utf-8"))
    if lock.get("schema_version") != 1:
        raise ValueError("Unsupported dependency lock schema: " + str(lock_file))

    platform_name = {"Darwin": "macos", "Windows": "windows"}.get(system, system.lower())
    prefix = "%s-%s" % (platform_name, architecture)
    matches = [package for package in lock.get("packages", [])
               if package.get("artifact_type") == "binary" and
               package.get("target", "").startswith(prefix)]
    if system == "Linux":
        libc = platform.libc_ver()[1]
        version = re.match(r"(\d+)\.(\d+)", libc)
        if version:
            libc_version = int(version.group(1)) * 100 + int(version.group(2))
            compatible = []
            for package in matches:
                target = re.fullmatch(r"linux-[^-]+-glibc(\d+)", package["target"])
                if target and int(target.group(1)) <= libc_version:
                    compatible.append((int(target.group(1)), package))
            if compatible:
                best = max(version for version, _ in compatible)
                matches = [package for version, package in compatible if version == best]
            else:
                matches = [package for package in matches
                           if package["target"] == prefix]
    elif system == "Darwin":
        version = re.match(r"(\d+)", platform.mac_ver()[0])
        if version:
            compatible = []
            for package in matches:
                target = re.fullmatch(r"macos-[^-]+-min(\d+)", package["target"])
                if target and int(target.group(1)) <= int(version.group(1)):
                    compatible.append((int(target.group(1)), package))
            if compatible:
                best = max(minimum for minimum, _ in compatible)
                matches = [package for minimum, package in compatible if minimum == best]
            else:
                matches = [package for package in matches if package["target"] == prefix]
    elif system == "Android":
        compatible = []
        for package in matches:
            target = re.fullmatch(r"android-[^-]+-v8a-api(\d+)", package["target"])
            if target and int(target.group(1)) <= 26:
                compatible.append((int(target.group(1)), package))
        if compatible:
            best = max(api for api, _ in compatible)
            matches = [package for api, package in compatible if api == best]
        else:
            matches = [package for package in matches if package["target"] == prefix]
    elif system == "Windows":
        matches = [package for package in matches if package["target"] == prefix + "-md"]
    else:
        matches = [package for package in matches if package["target"] == prefix]

    names = [package.get("name") for package in matches]
    if len(names) != len(set(names)):
        raise ValueError("Multiple locked binary packages match this host: " + str(lock_file))
    return matches


def download(url, archive):
    descriptor, partial_name = tempfile.mkstemp(
        prefix="." + archive.name + ".", suffix=".download", dir=archive.parent)
    os.close(descriptor)
    partial = Path(partial_name)
    try:
        with urllib.request.urlopen(url, timeout=60) as source, partial.open("wb") as output:
            shutil.copyfileobj(source, output)
        partial.replace(archive)
    finally:
        if partial.exists():
            partial.unlink()


def locked_download_url(url):
    if url.startswith("https://github.com/"):
        prefix = os.environ.get("SEEKDB_DEPS_GITHUB_PROXY_PREFIX", "https://gh-proxy.com/")
        if prefix:
            return prefix.rstrip("/") + "/" + url
    return url


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def locked_archive(package, archive_directory, staging_directory):
    url = package.get("url")
    checksum = package.get("sha256", "").lower()
    if not url or not re.fullmatch(r"[0-9a-f]{64}", checksum) or not package.get("version"):
        raise ValueError("Incomplete locked dependency: " + str(package.get("name")))

    cached = archive_directory / checksum
    archive = staging_directory / checksum
    if cached.is_file() and sha256_file(cached) == checksum:
        shutil.copyfile(cached, archive)
        return archive
    if cached.is_file():
        cached.unlink()
    download(locked_download_url(url), archive)
    if sha256_file(archive) != checksum:
        archive.unlink()
        raise ValueError("SHA256 mismatch for " + url.rsplit("/", 1)[-1])
    return archive


def unpack(archive, destination, *, ignore_owner=False):
    if archive.suffix != ".rpm":
        command = ["tar"]
        if ignore_owner:
            command.append("--no-same-owner")
        command.extend(["-xf", str(archive), "-C", str(destination),
                        "--strip-components=1"])
        subprocess.run(command, check=True)
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


def populate_profile(profile, stage, archive_directory):
    for filename, url in packages(profile):
        print("Preparing " + filename, flush=True)
        archive = stage / "pkg" / filename
        cached = archive_directory / filename
        if cached.is_file():
            shutil.copyfile(cached, archive)
        else:
            download(url, archive)
        unpack(archive, stage)


def populate_lock(lock_file, stage, archive_directory, system, architecture):
    for package in locked_packages(lock_file, system, architecture):
        archive = locked_archive(package, archive_directory, stage / "pkg")
        print("Preparing locked package " + package["name"] + " " + package["version"], flush=True)
        unpack(archive, stage, ignore_owner=system == "Linux")


def populate(profile, destination, key, archive_directory, lock_file, system, architecture):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".deps-", dir=destination.parent) as temporary:
        stage = Path(temporary) / "3rd"
        (stage / "pkg").mkdir(parents=True)
        populate_profile(profile, stage, archive_directory)
        populate_lock(lock_file, stage, archive_directory, system, architecture)
        (stage / key).touch()
        (stage / "DONE").touch()
        remove_directory(destination)
        stage.rename(destination)


def install(profile, destination, cache_directory, lock_file, system, architecture):
    lock_packages = locked_packages(lock_file, system, architecture)
    identity = profile.read_bytes() + json.dumps(lock_packages, sort_keys=True).encode("utf-8")
    key = hashlib.sha256(identity).hexdigest()
    if ready(destination, key):
        print("Dependencies ready: " + str(destination), flush=True)
        return
    if cache_directory is None:
        populate(profile, destination, key, destination / "pkg", lock_file, system, architecture)
        return

    import fcntl

    cache_directory.mkdir(parents=True, exist_ok=True)
    cached = cache_directory / key / "3rd"
    with (cache_directory / (key + ".lock")).open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if not ready(cached, key):
            populate(profile, cached, key, destination / "pkg", lock_file, system, architecture)
    destination.parent.mkdir(parents=True, exist_ok=True)
    remove_directory(destination)
    destination.symlink_to(cached, target_is_directory=True)
    print("Dependencies ready: " + str(destination), flush=True)


def prepare_rust(target=None):
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
    print("Preparing Rust toolchain from rust-toolchain.toml", flush=True)
    environment = dict(os.environ, RUSTUP_AUTO_INSTALL="1")
    environment.pop("RUSTUP_TOOLCHAIN", None)
    subprocess.run(["rustup", "show", "active-toolchain"], cwd=ROOT, env=environment, check=True)
    if target:
        subprocess.run(["rustup", "target", "add", target], cwd=ROOT, env=environment, check=True)


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
    system = platform.system()
    architecture = platform.machine().lower()
    if system == "Darwin":
        architecture = {"aarch64": "arm64", "amd64": "x86_64"}.get(architecture, architecture)
    else:
        architecture = {"amd64": "x86_64", "arm64": "aarch64"}.get(architecture, architecture)
    rust_target = None
    if options.android:
        rust_target = "aarch64-linux-android"
    elif options.ios:
        rust_target = "aarch64-apple-ios-sim" if options.simulator else "aarch64-apple-ios"
    prepare_rust(rust_target)
    cache = os.environ.get("DEP_CACHE_DIR") if os.name != "nt" else None
    cache_directory = Path(cache).expanduser().resolve() if cache else None
    install(profile, ROOT / "deps/3rd", cache_directory, ROOT / "deps.lock.json",
            system, architecture)
    if options.wasm:
        subprocess.run([sys.executable, str(PROFILES / "wasm/build.py")], cwd=ROOT, check=True)
    if options.android:
        install(PROFILES / "oceanbase.android.arm64.deps", ROOT / "deps/android", None,
                ROOT / "deps.lock.json", "Android", "arm64")
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
