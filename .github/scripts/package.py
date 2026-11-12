#!/usr/bin/env python3
"""Build and package the selected GitHub Actions target using shared caches."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / ".ci-cache"
CONFIG = json.loads((ROOT / ".github/packaging.json").read_text())


def run(arguments, **kwargs):
    subprocess.run([str(value) for value in arguments], cwd=ROOT, check=True, **kwargs)


def output(name, value):
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as stream:
        stream.write(name + "=" + str(value) + "\n")


def matrix():
    selected = os.environ["PACKAGE_PLATFORM"]
    glibc = os.environ["PACKAGE_GLIBC"]
    version = os.environ.get("PACKAGE_VERSION") or re.search(
        r'^VERSION = "([^"]+)"', (ROOT / "bazel/compile_config.bzl").read_text(), re.MULTILINE).group(1)
    release = os.environ["PACKAGE_RELEASE"]
    jobs = int(os.environ["BUILD_JOBS"])
    if not 0 <= jobs <= 64 or not all(re.fullmatch(r"[0-9A-Za-z.+~_]+", x) for x in (version, release)):
        raise ValueError("invalid version/release or jobs (expected 0..64)")
    if glibc not in ("2.17", "2.28", "both"):
        raise ValueError("unsupported glibc baseline")
    targets = []
    for arch, runner in (("x86_64", "ubuntu-24.04"), ("aarch64", "ubuntu-24.04-arm")):
        for baseline in (("2.17", "2.28") if glibc == "both" else (glibc,)):
            targets.append(dict(id="linux-" + arch + "-glibc" + baseline, os="linux", arch=arch,
                                platform="linux-" + arch, runner=runner, glibc=baseline,
                                profile="el7" if baseline == "2.17" else "el8"))
    targets += [
        dict(id="macos13-arm64", platform="macos-arm64", os="macos", arch="arm64", runner="macos-15", glibc="", profile=""),
        dict(id="windows-x86_64-md", platform="windows-x86_64", os="windows", arch="x86_64", runner="windows-2022", glibc="", profile=""),
        dict(id="android-arm64-api26", platform="android-arm64", os="android", arch="aarch64", runner="ubuntu-24.04", glibc="", profile=""),
    ]
    targets = [target for target in targets if selected == "all" or target["platform"] == selected]
    if not targets:
        raise ValueError("unsupported package platform: " + selected)
    output("matrix", json.dumps({"include": targets}, separators=(",", ":")))
    linux = [target for target in targets if target["os"] == "linux"]
    # An unused placeholder keeps the job matrix valid when this job is skipped.
    output("linux_matrix", json.dumps({"include": linux or [targets[0]]}, separators=(",", ":")))
    output("has_linux", str(bool(linux)).lower())
    output("version", version)
    output("release", release)
    output("jobs", jobs)


def environment():
    for name in ("cargo", "rustup", "launcher", "downloads", "actions", "homebrew", "home"):
        (CACHE / name).mkdir(parents=True, exist_ok=True)
    values = {"CARGO_HOME": CACHE / "cargo", "RUSTUP_HOME": CACHE / "rustup",
              "HOMEBREW_CACHE": CACHE / "homebrew", "DEP_DOWNLOAD_DIR": CACHE / "sdk-archives"}
    with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as stream:
        for name, value in values.items():
            stream.write(name + "=" + value.as_posix() + "\n")
    with Path(os.environ["GITHUB_PATH"]).open("a", encoding="utf-8") as stream:
        stream.write((CACHE / "cargo/bin").as_posix() + "\n")
    target = os.environ["PACKAGE_OS"]
    arch = os.environ["PACKAGE_ARCH"]
    if target == "linux":
        profiles = ["oceanbase.%s.%s.deps" % (os.environ["LINUX_PROFILE"], arch)]
    elif target == "macos":
        profiles = ["oceanbase.macos13.arm64.deps"]
    elif target == "windows":
        profiles = ["oceanbase.windows.x86_64.deps"]
    else:
        profiles = ["oceanbase.el9.x86_64.deps", "oceanbase.android.arm64.deps"]
    sdk = hashlib.sha256((ROOT / "deps/init/prepare.py").read_bytes())
    for name in profiles:
        sdk.update((ROOT / "deps/init" / name).read_bytes())
    sdk.update((ROOT / ".github/packaging.json").read_bytes())
    output("sdk", sdk.hexdigest()[:24])
    toolchain = hashlib.sha256(sdk.digest())
    for name in ("rust-toolchain.toml", ".bazelversion", "bazel/binaries.json"):
        toolchain.update((ROOT / name).read_bytes())
    if target == "windows":
        for name in ("VCToolsVersion", "WindowsSDKVersion"):
            if not os.environ.get(name):
                raise ValueError("Visual Studio developer environment is missing " + name)
            toolchain.update(os.environ[name].encode())
    elif target == "macos":
        toolchain.update(subprocess.check_output(["xcodebuild", "-version"]))
    output("toolchain", toolchain.hexdigest()[:24])


def container_image():
    base = CONFIG["linux_images"][os.environ["PACKAGE_ARCH"]]
    recipe = "FROM %s\nRUN yum -y install cpio && yum clean all\n" % base
    tag = "seekdb-package-%s:%s" % (os.environ["PACKAGE_ARCH"], hashlib.sha256(recipe.encode()).hexdigest()[:16])
    found = subprocess.run(["docker", "image", "inspect", tag], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if found.returncode:
        run(["docker", "build", "--tag", tag, "-"], input=recipe.encode())
    return tag


def in_container(command):
    if os.environ["PACKAGE_OS"] != "linux" or os.environ.get("PACKAGE_IN_CONTAINER"):
        return False
    arguments = ["docker", "run", "--rm", "--user", "%s:%s" % (os.getuid(), os.getgid()),
                 "-v", str(ROOT) + ":/work", "-w", "/work", "-e", "PACKAGE_IN_CONTAINER=1",
                 "-e", "HOME=/work/.ci-cache/home", "-e", "CARGO_HOME=/work/.ci-cache/cargo",
                 "-e", "RUSTUP_HOME=/work/.ci-cache/rustup", "-e", "LC_ALL=C.UTF-8"]
    arguments += ["-e", "DEP_DOWNLOAD_DIR=/work/.ci-cache/sdk-archives"]
    for name in ("PACKAGE_OS", "PACKAGE_ARCH", "PACKAGE_GLIBC", "PACKAGE_VERSION", "PACKAGE_RELEASE",
                 "PACKAGE_FORMATS", "LINUX_PROFILE", "BUILD_JOBS", "GITHUB_SHA",
                 "RUSTUP_DIST_SERVER", "RUSTUP_UPDATE_ROOT", "RUSTUP_INIT_URL"):
        if name in os.environ:
            arguments += ["-e", name]
    arguments += [container_image(),
                  "/opt/python/cp311-cp311/bin/python", ".github/scripts/package.py", command]
    run(arguments)
    return True


def sysroot():
    if os.environ["PACKAGE_OS"] != "linux" or os.environ["PACKAGE_GLIBC"] != "2.17":
        return
    destination = CACHE / "sysroot"
    image = CONFIG["sysroot_images"][os.environ["PACKAGE_ARCH"]]
    marker = destination / "image.txt"
    if marker.is_file() and marker.read_text() == image:
        sysroot_layout(destination)
        return
    shutil.rmtree(destination, ignore_errors=True)
    destination.mkdir(parents=True)
    # Export package-owned files only, without recursively archiving /usr/lib64.
    script = '''set -euo pipefail
rpm -ql glibc glibc-devel glibc-headers kernel-headers | sort -u | while IFS= read -r path; do
  case "$path" in /usr/include/*|/usr/lib64/*|/lib64/*|/usr/lib/*|/lib/*)
    if [ -f "$path" ] || [ -L "$path" ]; then printf '%s\\0' "${path#/}"; fi ;;
  esac
done | tar -C / --null -T - -czf -
'''
    with subprocess.Popen(["docker", "run", "--rm", image, "bash", "-c", script], stdout=subprocess.PIPE) as archive:
        try:
            run(["tar", "-xzf", "-", "-C", destination], stdin=archive.stdout)
        finally:
            archive.stdout.close()
        if archive.wait():
            raise RuntimeError("failed to export the glibc 2.17 sysroot")
    sysroot_layout(destination)
    marker.write_text(image)


def sysroot_layout(destination):
    # Clang probes lib/../lib64. Keep the intermediate directories in Bazel's
    # sandbox too, where empty directories alone are not declared inputs.
    for name in ("lib", "usr/lib"):
        directory = destination / name
        directory.mkdir(parents=True, exist_ok=True)
        (directory / ".layout").touch()


def initialize():
    sysroot()
    if in_container("initialize"):
        return
    if os.environ["PACKAGE_OS"] == "macos":
        run(["brew", "install", "autoconf", "automake", "libtool", "pkg-config"])
    arguments = [sys.executable, "build.py", "init"]
    if os.environ["PACKAGE_OS"] == "linux":
        arguments += ["--linux-profile", os.environ["LINUX_PROFILE"]]
    if os.environ["PACKAGE_OS"] == "android":
        sdk = Path(os.environ["ANDROID_HOME"])
        manager = sdk / "cmdline-tools/latest/bin/sdkmanager"
        local = CACHE / "android-sdk"
        shutil.copytree(sdk / "licenses", local / "licenses", dirs_exist_ok=True)
        ndk = local / "ndk" / CONFIG["android_ndk"]
        if not (ndk / "source.properties").is_file():
            run([manager, "--sdk_root=" + str(local), "ndk;" + CONFIG["android_ndk"]])
        os.environ["ANDROID_NDK_HOME"] = str(ndk)
        with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as stream:
            stream.write("ANDROID_NDK_HOME=" + ndk.as_posix() + "\n")
        arguments.append("--android")
    run(arguments)


def bazel_arguments():
    if os.environ["PACKAGE_OS"] == "linux" and os.environ["PACKAGE_GLIBC"] == "2.17":
        os.environ["SEEKDB_SYSROOT"] = str(CACHE / "sysroot")
    direct = CACHE / "direct-downloads.cfg"
    direct.write_text("allow *\n")
    return ["--launcher_cache_dir=" + str(CACHE / "launcher"),
            "--disk_cache=" + str(CACHE / "actions"), "--repository_cache=" + str(CACHE / "downloads"),
            "--downloader_config=" + str(direct), "--experimental_disk_cache_gc_max_size=2G",
            "--experimental_disk_cache_gc_idle_delay=0"]


def build():
    if in_container("build"):
        return
    jobs = int(os.environ["BUILD_JOBS"]) or (1 if os.environ["PACKAGE_OS"] == "macos" else 2)
    arguments = [sys.executable, "build.py", "build", *bazel_arguments(),
                 "--jobs=" + str(jobs), "--config=O2", "--config=line_tables_only", "--config=package",
                 "--lockfile_mode=error"]
    if os.environ["PACKAGE_OS"] == "android":
        arguments.append("--config=android")
    run(arguments + ["//src/observer:seekdb", "//src/share/inner_table/sys_package:syspack_release"])


def timezone():
    destination = CACHE / "timezone_V1.log"
    metadata = CONFIG["timezone"]
    if not destination.is_file() or hashlib.sha256(destination.read_bytes()).hexdigest() != metadata["sha256"]:
        with urllib.request.urlopen(metadata["url"], timeout=60) as source:
            data = source.read()
        if hashlib.sha256(data).hexdigest() != metadata["sha256"]:
            raise ValueError("timezone checksum mismatch")
        destination.write_bytes(data)
    return destination


def export():
    if in_container("export"):
        return
    arguments = [sys.executable, "package/package.py", "binary", "--version", os.environ["PACKAGE_VERSION"],
                 "--release", os.environ["PACKAGE_RELEASE"], "--layout", os.environ["PACKAGE_OS"],
                 "--source-commit", os.environ["GITHUB_SHA"], "--timezone-file", timezone(),
                 "--build-config", "O2,line_tables_only,package", "--output", ROOT / "dist"]
    if os.environ["PACKAGE_OS"] == "linux":
        arguments += ["--glibc-version", os.environ["PACKAGE_GLIBC"]]
    run(arguments)
    if os.environ["PACKAGE_OS"] == "linux":
        helper = ROOT / "deps/3rd/home/admin/oceanbase/bin/obshell"
        run(["tar", "--dereference", "-czf", ROOT / "dist/obshell.tar.gz", "-C", helper.parent, helper.name])


def system_packages():
    sys.path.insert(0, str(ROOT / "package"))
    import binary
    archives = list((ROOT / "input").glob("seekdb-binary-*.tar.gz"))
    if len(archives) != 1:
        raise ValueError("expected exactly one Linux binary distribution")
    with tempfile.TemporaryDirectory(prefix="system-packages-", dir=CACHE) as temporary:
        work = Path(temporary)
        bundle, manifest = binary.load(archives[0], work / "binary", os.environ["PACKAGE_VERSION"])
        if manifest["source_commit"] != os.environ["GITHUB_SHA"]:
            raise ValueError("binary distribution does not match this workflow's source commit")
        run(["tar", "-xzf", ROOT / "input/obshell.tar.gz", "-C", work, "--", "obshell"])
        formats = ("rpm", "deb") if os.environ["PACKAGE_FORMATS"] == "all" else (os.environ["PACKAGE_FORMATS"],)
        for kind in formats:
            run([sys.executable, "package/package.py", kind, "--version", os.environ["PACKAGE_VERSION"],
                 "--release", os.environ["PACKAGE_RELEASE"], "--binary-package", bundle,
                 "--obshell-bin", work / "obshell", "--output", ROOT / "dist"])
    for artifact in (ROOT / "dist").iterdir():
        if artifact.suffix in (".rpm", ".deb"):
            digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
            artifact.with_name(artifact.name + ".sha256").write_text(digest + "  " + artifact.name + "\n")


def shutdown():
    if in_container("shutdown"):
        return
    if (ROOT / "build").is_dir():
        run([sys.executable, "build.py", "shutdown", "--launcher_cache_dir=" + str(CACHE / "launcher")])


if __name__ == "__main__":
    commands = {"matrix": matrix, "environment": environment, "initialize": initialize,
                "build": build, "export": export, "system": system_packages, "shutdown": shutdown}
    os.chdir(ROOT)
    commands[sys.argv[1]]()
