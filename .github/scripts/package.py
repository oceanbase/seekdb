#!/usr/bin/env python3
"""Build and package the selected GitHub Actions target."""

import hashlib
import json
from datetime import datetime, timezone as datetime_timezone
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
    binary_run_id = os.environ.get("PACKAGE_BINARY_RUN_ID", "").strip()
    if binary_run_id:
        if not re.fullmatch(r"[1-9][0-9]*", binary_run_id):
            raise ValueError("binary_run_id must be a workflow run ID")
        if selected not in ("linux-x86_64", "linux-aarch64") or os.environ["PACKAGE_FORMATS"] == "binary":
            raise ValueError("binary reuse requires a Linux platform and RPM/DEB formats")
    version = os.environ.get("PACKAGE_VERSION") or ("" if binary_run_id else re.search(
        r'^VERSION = "([^"]+)"', (ROOT / "bazel/compile_config.bzl").read_text(), re.MULTILINE).group(1))
    release = datetime.now(datetime_timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    if version and not re.fullmatch(r"[0-9A-Za-z.+~_]+", version):
        raise ValueError("invalid version")
    if glibc not in ("2.17", "2.28", "both"):
        raise ValueError("unsupported glibc baseline")
    targets = []
    for arch, runner in (("x86_64", "ubuntu-24.04"), ("aarch64", "ubuntu-24.04-arm")):
        for baseline in (("2.17", "2.28") if glibc == "both" else (glibc,)):
            targets.append(dict(id="linux-" + arch + "-glibc" + baseline, os="linux", arch=arch,
                                platform="linux-" + arch, runner=runner, glibc=baseline,
                                profile="el7" if baseline == "2.17" else "el8",
                                conda_spec=CONFIG["linux_sysroots"][arch][baseline]["spec"],
                                micromamba=CONFIG["micromamba"]))
    targets += [
        dict(id="macos13-arm64", platform="macos-arm64", os="macos", arch="arm64", runner="macos-15", glibc="", profile=""),
        dict(id="windows-x86_64-md", platform="windows-x86_64", os="windows", arch="x86_64", runner="windows-2022", glibc="", profile=""),
        dict(id="android-arm64-api26", platform="android-arm64", os="android", arch="aarch64", runner="ubuntu-24.04", glibc="", profile=""),
    ]
    for sdk, platform, variant in (("iphoneos", "ios-arm64", ""),
                                   ("iphonesimulator", "ios-simulator-arm64", "-simulator")):
        targets.append(dict(id="ios" + CONFIG["ios_minimum_os"] + variant + "-arm64",
                            platform=platform, os="ios", arch="arm64", runner="macos-15",
                            glibc="", profile="", sdk=sdk))
    targets = [target for target in targets if selected == "all" or target["platform"] == selected
               or (selected == "ios" and target["os"] == "ios")]
    if not targets:
        raise ValueError("unsupported package platform: " + selected)
    output("matrix", json.dumps({"include": targets}, separators=(",", ":")))
    linux = [target for target in targets if target["os"] == "linux"]
    # An unused placeholder keeps the job matrix valid when this job is skipped.
    output("linux_matrix", json.dumps({"include": linux or [targets[0]]}, separators=(",", ":")))
    output("has_linux", str(bool(linux)).lower())
    output("has_ios_pair", str(sum(target["os"] == "ios" for target in targets) == 2).lower())
    output("version", version)
    output("release", release)
    output("binary_run_id", binary_run_id)
    source_commit = os.environ["GITHUB_SHA"]
    if binary_run_id:
        request = urllib.request.Request(
            "%s/repos/%s/actions/runs/%s" % (os.environ["GITHUB_API_URL"], os.environ["GITHUB_REPOSITORY"], binary_run_id),
            headers={"Authorization": "Bearer " + os.environ["GITHUB_TOKEN"],
                     "Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2026-03-10"})
        with urllib.request.urlopen(request, timeout=60) as response:
            source_commit = json.load(response)["head_sha"]
    output("binary_source_commit", source_commit)


def environment():
    CACHE.mkdir(parents=True, exist_ok=True)
    target = os.environ["PACKAGE_OS"]
    arch = os.environ["PACKAGE_ARCH"]
    if target == "linux":
        profiles = ["oceanbase.%s.%s.deps" % (os.environ["LINUX_PROFILE"], arch)]
    elif target == "ios":
        profiles = ["oceanbase.ios-host.arm64.deps"]
    elif target == "macos":
        profiles = ["oceanbase.macos13.arm64.deps"]
    elif target == "windows":
        profiles = ["oceanbase.windows.x86_64.deps"]
    else:
        profiles = ["oceanbase.el9.x86_64.deps", "oceanbase.android.arm64.deps"]
    sdk = hashlib.sha256((ROOT / "deps/init/prepare.py").read_bytes())
    for name in profiles:
        sdk.update((ROOT / "deps/init" / name).read_bytes())
    if target == "ios":
        developer = os.environ.get("DEVELOPER_DIR") or subprocess.check_output(
            ["xcode-select", "-p"], text=True).strip()
        os.environ["DEVELOPER_DIR"] = developer
        with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as stream:
            stream.write("DEVELOPER_DIR=" + developer + "\n")
            stream.write("IPHONEOS_DEPLOYMENT_TARGET=" + CONFIG["ios_minimum_os"] + "\n")
        sdk.update((ROOT / "deps/init/ios/build.py").read_bytes())
        sdk.update(CONFIG["ios_minimum_os"].encode())
        sdk.update(os.environ["PACKAGE_APPLE_SDK"].encode())
        for command in (["xcodebuild", "-version"],
                        ["xcrun", "--sdk", os.environ["PACKAGE_APPLE_SDK"], "--show-sdk-version"]):
            identity = subprocess.check_output(command)
            print(identity.decode().strip(), flush=True)
            sdk.update(identity)
    output("sdk", sdk.hexdigest()[:24])


def sysroot():
    arch = os.environ["PACKAGE_ARCH"]
    metadata = CONFIG["linux_sysroots"][arch][os.environ["PACKAGE_GLIBC"]]
    name, version, build = metadata["spec"].split("=")
    prefix = Path(os.environ["CONDA_ENVIRONMENT"])
    record = json.loads((prefix / "conda-meta" / (name + "-" + version + "-" + build + ".json")).read_text())
    if (record["name"], record["version"], record["build"], record.get("sha256")) != (
            name, version, build, metadata["sha256"]):
        raise ValueError("Conda sysroot does not match the pinned package")
    destination = prefix / (arch + "-conda-linux-gnu") / "sysroot"
    for filename in ("usr/include/stdlib.h", "usr/include/linux/version.h", "usr/lib64/libc.so"):
        if not (destination / filename).is_file():
            raise ValueError("incomplete Conda sysroot: " + str(destination / filename))
    with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as stream:
        stream.write("SEEKDB_SYSROOT=" + destination.as_posix() + "\n")
    print("Using Conda glibc " + version + " sysroot: " + str(destination))


def initialize():
    if os.environ["PACKAGE_OS"] in ("macos", "ios"):
        packages = ["autoconf", "automake", "libtool", "pkg-config"]
        if os.environ["PACKAGE_OS"] == "ios":
            packages.append("gettext")
        run(["brew", "install", *packages])
    if os.environ["PACKAGE_OS"] == "ios":
        sys.path.insert(0, str(ROOT / "deps/init"))
        import prepare
        simulator = os.environ["PACKAGE_APPLE_SDK"] == "iphonesimulator"
        prepare.prepare_rust("aarch64-apple-ios-sim" if simulator else "aarch64-apple-ios")
        prepare.install(ROOT / "deps/init/oceanbase.ios-host.arm64.deps", ROOT / "deps/3rd")
        # Build the base dependencies here; Bazel builds source_sdk with the
        # engine below, using the CI downloader configuration and parallelism.
        run([sys.executable, "deps/init/ios/build.py", "--reuse", "--jobs", build_jobs(),
             "--deployment-target", CONFIG["ios_minimum_os"], *(["--simulator"] if simulator else [])])
        return
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
    if os.environ["PACKAGE_OS"] == "linux" and not os.environ.get("SEEKDB_SYSROOT"):
        raise ValueError("Linux packaging requires a configured Conda sysroot")
    direct = CACHE / "direct-downloads.cfg"
    direct.write_text("allow *\n")
    return ["--downloader_config=" + str(direct)]


def build_jobs():
    jobs = os.environ["BUILD_JOBS"]
    if jobs == "0":
        jobs = "1" if os.environ["PACKAGE_OS"] in ("macos", "ios") else "2"
    return jobs


def build():
    arguments = [sys.executable, "build.py", "build", *bazel_arguments(),
                 "--jobs=" + build_jobs(), "--config=O2", "--config=line_tables_only", "--config=package",
                 "--lockfile_mode=error"]
    if os.environ["PACKAGE_OS"] == "ios":
        configuration = "ios-simulator" if os.environ["PACKAGE_APPLE_SDK"] == "iphonesimulator" else "ios"
        run(arguments + ["--config=" + configuration, "--ios_minimum_os=" + CONFIG["ios_minimum_os"],
                         "//src/observer:ios_framework"])
        return
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
    if os.environ["PACKAGE_OS"] == "ios":
        run([sys.executable, "package/ios/distribution.py", "framework",
             "--input", ROOT / "build/bin/src/observer/framework/SeekDB.framework",
             "--sdk", os.environ["PACKAGE_APPLE_SDK"], *ios_package_arguments()])
        return
    arguments = [sys.executable, "package/package.py", "binary", "--version", os.environ["PACKAGE_VERSION"],
                 "--package-timestamp", os.environ["PACKAGE_RELEASE"], "--layout", os.environ["PACKAGE_OS"],
                 "--source-commit", os.environ["GITHUB_SHA"], "--timezone-file", timezone(),
                 "--build-config", "O2,line_tables_only,package", "--output", ROOT / "dist"]
    if os.environ["PACKAGE_OS"] == "linux":
        arguments += ["--glibc-version", os.environ["PACKAGE_GLIBC"]]
    run(arguments)
    if os.environ["PACKAGE_OS"] == "linux":
        helper = ROOT / "deps/3rd/home/admin/oceanbase/bin/obshell"
        staged = CACHE / "obshell"
        staged.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(helper, staged / helper.name)
        (staged / helper.name).chmod(0o755)
        sys.path.insert(0, str(ROOT / "package"))
        import symbols
        symbols.strip_tree(staged, "linux")
        run(["tar", "-czf", ROOT / "dist/obshell.tar.gz", "-C", staged, helper.name])


def ios_package_arguments():
    return ["--version", os.environ["PACKAGE_VERSION"], "--source-commit", os.environ["GITHUB_SHA"],
            "--minimum-os", CONFIG["ios_minimum_os"], "--output", ROOT / "dist"]


def xcframework():
    run([sys.executable, "package/ios/distribution.py", "xcframework",
         "--input", ROOT / "input", *ios_package_arguments()])


def system_packages():
    sys.path.insert(0, str(ROOT / "package"))
    import binary
    archives = list((ROOT / "input").glob("seekdb-binary-*.tar.gz"))
    if len(archives) != 1:
        raise ValueError("expected exactly one Linux binary distribution")
    with tempfile.TemporaryDirectory(prefix="system-packages-", dir=CACHE) as temporary:
        work = Path(temporary)
        bundle, manifest = binary.load(archives[0], work / "binary", os.environ["PACKAGE_VERSION"] or None)
        if manifest["source_commit"] != os.environ["BINARY_SOURCE_COMMIT"]:
            raise ValueError("binary distribution does not match the source run's commit")
        target = manifest["platform"]
        if (target["os"], target["arch"], target.get("glibc")) != (
                "linux", os.environ["PACKAGE_ARCH"], os.environ["PACKAGE_GLIBC"]):
            raise ValueError("binary distribution does not match the selected Linux architecture/glibc")
        run(["tar", "-xzf", ROOT / "input/obshell.tar.gz", "-C", work, "--", "obshell"])
        formats = ("rpm", "deb") if os.environ["PACKAGE_FORMATS"] == "all" else (os.environ["PACKAGE_FORMATS"],)
        for kind in formats:
            run([sys.executable, "package/package.py", kind, "--version", manifest["version"],
                 "--package-timestamp", os.environ["PACKAGE_RELEASE"], "--binary-package", bundle,
                 "--obshell-bin", work / "obshell", "--output", ROOT / "dist"])
    (ROOT / "dist/packaging-info.json").write_text(json.dumps({
        "binary_run_id": os.environ["BINARY_RUN_ID"],
        "binary_source_commit": manifest["source_commit"],
        "binary_built_at": manifest.get("built_at"),
        "packaging_commit": os.environ["GITHUB_SHA"],
        "version": manifest["version"],
        "release": os.environ["PACKAGE_RELEASE"],
        "platform": target,
    }, indent=2) + "\n")
    for artifact in (ROOT / "dist").iterdir():
        if artifact.suffix in (".rpm", ".deb"):
            digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
            artifact.with_name(artifact.name + ".sha256").write_text(digest + "  " + artifact.name + "\n")


def shutdown():
    if (ROOT / "build").is_dir():
        run([sys.executable, "build.py", "shutdown"])


if __name__ == "__main__":
    commands = {"matrix": matrix, "sysroot": sysroot, "environment": environment, "initialize": initialize,
                "build": build, "export": export, "xcframework": xcframework,
                "system": system_packages, "shutdown": shutdown}
    os.chdir(ROOT)
    commands[sys.argv[1]]()
