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
    platforms = lock.get("platforms", {})
    if not isinstance(platforms, dict):
        raise ValueError("Invalid dependency lock platforms: " + str(lock_file))
    platform_name = {"Darwin": "macos", "Windows": "windows"}.get(system, system.lower())
    prefix = "%s-%s" % (platform_name, architecture)
    pattern = None
    version = None
    if system == "Linux":
        libc = platform.libc_ver()[1]
        found = re.match(r"(\d+)\.(\d+)", libc)
        if found:
            version = int(found.group(1)) * 100 + int(found.group(2))
        pattern = re.escape(prefix) + r"-glibc(\d+)"
    elif system == "Darwin":
        found = re.match(r"(\d+)", platform.mac_ver()[0])
        if found:
            version = int(found.group(1))
        pattern = re.escape(prefix) + r"-min(\d+)"
    elif system == "Android":
        version = 26
        pattern = re.escape(prefix) + r"-v8a-api(\d+)"

    target = prefix + "-md" if system == "Windows" else prefix
    if pattern:
        candidates = []
        for name in platforms:
            match = re.fullmatch(pattern, name)
            if match:
                candidates.append((int(match.group(1)), name))
        if candidates:
            if version is None:
                raise ValueError("Cannot determine platform version for locked dependencies: " + prefix)
            compatible = [candidate for candidate in candidates if candidate[0] <= version]
            if compatible:
                target = max(compatible)[1]
            elif target not in platforms:
                raise ValueError("No compatible locked dependency platform: " + prefix)

    packages = platforms.get(target, {})
    if not isinstance(packages, dict):
        raise ValueError("Invalid dependency lock platform: " + target)
    result = []
    for name, package in sorted(packages.items()):
        if not isinstance(package, dict):
            raise ValueError("Invalid locked dependency: " + name)
        result.append(dict(package, name=name))
    return result


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


def locked_cache_directory(system, cache_directory):
    if cache_directory is not None:
        return cache_directory / "pkg"
    if system == "Windows":
        local_appdata = os.environ.get("LOCALAPPDATA")
        if not local_appdata or not Path(local_appdata).is_absolute():
            raise ValueError("LOCALAPPDATA is missing or invalid; set DEP_CACHE_DIR for dependency packages")
        cache_home = Path(local_appdata)
    elif system == "Darwin":
        cache_home = Path.home() / "Library" / "Caches"
    else:
        xdg_cache = os.environ.get("XDG_CACHE_HOME")
        if xdg_cache and Path(xdg_cache).is_absolute():
            cache_home = Path(xdg_cache)
        else:
            cache_home = Path.home() / ".cache"
    return cache_home / "seekdb" / "deps" / "pkg"


def locked_archive(package, archive_directory):
    url = package.get("url")
    checksum = package.get("sha256", "")
    if checksum is None:
        checksum = ""
    if not isinstance(url, str) or not url or not isinstance(checksum, str):
        raise ValueError("Incomplete locked dependency: " + str(package.get("name")))
    checksum = checksum.lower()
    if checksum and not re.fullmatch(r"[0-9a-f]{64}", checksum):
        raise ValueError("Incomplete locked dependency: " + str(package.get("name")))

    archive_directory.mkdir(parents=True, exist_ok=True)
    index = None
    if checksum:
        cached = archive_directory / checksum
        if cached.is_file() and sha256_file(cached) == checksum:
            return cached
    else:
        index = archive_directory / "urls" / hashlib.sha256(url.encode("utf-8")).hexdigest()
        if index.is_file():
            cached_checksum = index.read_text(encoding="ascii").strip()
            if re.fullmatch(r"[0-9a-f]{64}", cached_checksum):
                cached = archive_directory / cached_checksum
                if cached.is_file():
                    return cached

    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".package.", suffix=".tmp", dir=archive_directory)
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        download(locked_download_url(url), temporary)
        actual_checksum = sha256_file(temporary)
        if checksum and actual_checksum != checksum:
            raise ValueError("SHA256 mismatch for " + url.rsplit("/", 1)[-1])
        cached = archive_directory / actual_checksum
        temporary.replace(cached)
    finally:
        if temporary.exists():
            temporary.unlink()

    if index is not None:
        index.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary_name = tempfile.mkstemp(prefix=".url.", suffix=".tmp", dir=index.parent)
        temporary = Path(temporary_name)
        try:
            with os.fdopen(descriptor, "w", encoding="ascii") as output:
                output.write(actual_checksum + "\n")
            temporary.replace(index)
        finally:
            if temporary.exists():
                temporary.unlink()
    return cached


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


def populate(profile, destination, key, archive_directory):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".deps-", dir=destination.parent) as temporary:
        stage = Path(temporary) / "3rd"
        (stage / "pkg").mkdir(parents=True)
        populate_profile(profile, stage, archive_directory)
        (stage / key).touch()
        (stage / "DONE").touch()
        remove_directory(destination)
        stage.rename(destination)


def install(profile, destination, cache_directory):
    key = hashlib.sha256(profile.read_bytes()).hexdigest()
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


def install_locked(lock_file, destination, cache_directory, system, architecture):
    packages = locked_packages(lock_file, system, architecture)
    key = hashlib.sha256(json.dumps(packages, sort_keys=True).encode("utf-8")).hexdigest()
    if ready(destination, key):
        print("Locked dependencies ready: " + str(destination), flush=True)
        return

    destination.parent.mkdir(parents=True, exist_ok=True)
    archive_directory = locked_cache_directory(platform.system(), cache_directory) if packages else None
    with tempfile.TemporaryDirectory(prefix=".deps-", dir=destination.parent) as temporary:
        stage = Path(temporary) / "prebuilt"
        stage.mkdir()
        for package in packages:
            archive = locked_archive(package, archive_directory)
            print("Preparing locked package " + package["name"], flush=True)
            unpack(archive, stage, ignore_owner=platform.system() == "Linux")
        (stage / key).touch()
        (stage / "DONE").touch()
        remove_directory(destination)
        stage.rename(destination)
    print("Locked dependencies ready: " + str(destination), flush=True)


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
    cache = os.environ.get("DEP_CACHE_DIR")
    cache_directory = Path(cache).expanduser().resolve() if cache else None
    install(profile, ROOT / "deps/3rd", cache_directory if os.name != "nt" else None)
    install_locked(ROOT / "deps.lock.json", ROOT / "deps/prebuilt/native", cache_directory,
                   system, architecture)
    if options.wasm:
        subprocess.run([sys.executable, str(PROFILES / "wasm/build.py")], cwd=ROOT, check=True)
    if options.android:
        install(PROFILES / "oceanbase.android.arm64.deps", ROOT / "deps/android", None)
        install_locked(ROOT / "deps.lock.json", ROOT / "deps/prebuilt/android", cache_directory,
                       "Android", "arm64")
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
