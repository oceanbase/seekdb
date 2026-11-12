#!/usr/bin/env python3
"""Stage Bazel release artifacts and build native seekdb packages."""

import argparse
import json
import os
from datetime import datetime, timezone
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SYSPACK = Path("src/share/inner_table/sys_package/bazel_generated/syspack_release")
LIFECYCLE = {"preinst": "pre_install", "postinst": "post_install",
             "prerm": "pre_uninstall", "postrm": "post_uninstall"}


def copy(source, destination, executable=False):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    destination.chmod(0o755 if executable else 0o644)


def normalize_permissions(root):
    for path in [root] + list(root.rglob("*")):
        mode = path.lstat().st_mode
        if stat.S_ISDIR(mode):
            path.chmod(0o755)
        elif stat.S_ISREG(mode):
            path.chmod(0o755 if mode & 0o111 else 0o644)


def stage(destination, artifacts, dependencies, layout, version, release, component="server", timezone=None,
          binary_package=None, seekdb_bin=None, obshell_bin=None, portable=False):
    if component == "libs":
        if binary_package:
            shutil.copytree(binary_package / "lib/seekdb", destination / "usr/libexec/seekdb/lib",
                            dirs_exist_ok=True)
            normalize_permissions(destination)
            return
        for name in ("libaio.so", "libaio.so.1", "libaio.so.1.0.1"):
            source = dependencies / "usr/local/oceanbase/deps/devel/lib" / name
            target = destination / "usr/libexec/seekdb/lib" / name
            if source.is_symlink():
                target.parent.mkdir(parents=True, exist_ok=True)
                target.symlink_to(os.readlink(source))
            else:
                copy(source, target, True)
        normalize_permissions(destination)
        return
    paths = {
        "linux": ("usr/bin", "etc/seekdb", "usr/share/seekdb", "usr/libexec/seekdb"),
        "macos": ("opt/seekdb/bin", "opt/seekdb/etc/seekdb", "opt/seekdb/share/seekdb", "opt/seekdb/libexec/seekdb"),
        "windows": ("bin", "etc", "share", "bin"),
        "android": ("bin", "etc/seekdb", "share/seekdb", "libexec/seekdb"),
    }
    directories = ("bin", "etc/seekdb", "share/seekdb", "libexec/seekdb") if portable else paths[layout]
    binary, config, share, helpers = [destination / path for path in directories]
    executable = "seekdb.exe" if layout == "windows" else "seekdb"
    if binary_package:
        for source, target in (("bin", binary), ("etc/seekdb", config), ("share/seekdb", share),
                               ("libexec/seekdb", helpers)):
            shutil.copytree(binary_package / source, target, dirs_exist_ok=True)
        if layout == "macos":
            shutil.copytree(binary_package / "lib", destination / "opt/seekdb/lib", dirs_exist_ok=True)
        elif layout == "android":
            shutil.copytree(binary_package / "lib", destination / "lib", dirs_exist_ok=True)
        (binary / executable).chmod(0o755)
    else:
        timezone = timezone or Path(os.environ.get("SEEKDB_TIMEZONE_FILE", ROOT / "tools/timezone_V1.log"))
        if not timezone.is_file():
            raise ValueError("required timezone data is missing: %s; supply --timezone-file or SEEKDB_TIMEZONE_FILE" % timezone)
        sql = sorted((artifacts / SYSPACK).glob("*.sql"))
        if not sql:
            raise ValueError("missing generated SQL in %s; build //src/share/inner_table/sys_package:syspack_release" % (artifacts / SYSPACK))
        output = artifacts / "src/observer"
        if layout == "windows":
            output /= "seekdb"
        source_binary = seekdb_bin or output / executable
        if layout == "windows":
            for name in json.loads(source_binary.with_name("seekdb.exe.runtime.json").read_text()):
                if Path(name).name != name:
                    raise ValueError("invalid runtime DLL name: %s" % name)
                copy(source_binary.parent / name, binary / name, True)
        copy(source_binary, binary / executable, True)
        for source in sql:
            copy(source, share / "admin" / source.name)
        copy(timezone, share / "timezone/timezone_V1.log")
        copy(ROOT / "tools/default_srs_data_mysql.sql", share / "srs/default_srs_data_mysql.sql")
        variables = json.loads((ROOT / "src/share/system_variable/ob_system_variable_init.json").read_text(encoding="utf-8"))
        def clear_links(value):
            if isinstance(value, dict):
                return {key: "" if key == "ref_url" else clear_links(item) for key, item in value.items()}
            if isinstance(value, list):
                return [clear_links(item) for item in value]
            return value
        config.mkdir(parents=True, exist_ok=True)
        (config / "ob_system_variable_init.json").write_text(json.dumps(clear_links(variables), indent=2) + "\n")
        if layout != "windows":
            for script in ("import_time_zone_info.py", "import_srs_data.py"):
                copy(ROOT / "tools" / script, helpers / script, True)
    if layout == "windows":
        if not binary_package:
            copy(ROOT / "package/msi/seekdb.cnf", config / "seekdb.cnf")
        helpers.mkdir(parents=True, exist_ok=True)
        return
    profile = ROOT / ("package/linux/profile" if layout in ("linux", "android") else "package/pkg/launchd/profile")
    if not binary_package:
        copy(profile / "seekdb.cnf", config / "seekdb.cnf")
    if portable:
        return
    if layout == "linux":
        copy(obshell_bin or dependencies / "home/admin/oceanbase/bin/obshell", binary / "obshell", True)
        copy(profile / "seekdb.service", destination / "usr/lib/systemd/system/seekdb.service")
        for script in ("seekdb_systemd_start", "seekdb_systemd_stop"):
            copy(profile / script, helpers / "scripts" / script, True)
        for name in ("oceanbase-pre.json", "telemetry-pre.json"):
            copy(profile / name, config / name)
        for script in list(LIFECYCLE.values()) + ["telemetry"]:
            content = (profile / (script + ".sh.template")).read_text()
            for key, value in {"NAME": "seekdb", "VERSION": version, "RELEASE": release}.items():
                content = content.replace("@" + key + "@", value)
            target = helpers / "scripts" / (script + ".sh")
            target.write_text(content)
            target.chmod(0o755)
        normalize_permissions(destination)


def deb(payload, output, version, release, architecture=None):
    architecture = architecture or subprocess.check_output(["dpkg", "--print-architecture"], text=True).strip()
    multiarch = {"amd64": "x86_64-linux-gnu", "arm64": "aarch64-linux-gnu"}[architecture]
    # Both supported architectures have 64-bit time_t. Let the private SONAME
    # alias use libaio1t64; if absent, the loader falls back to system libaio1.
    (payload / "usr/libexec/seekdb/lib/libaio.so.1").symlink_to(
        "/usr/lib/%s/libaio.so.1t64" % multiarch)
    control = payload / "DEBIAN"
    control.mkdir()
    (control / "control").write_text(
        "Package: seekdb\nVersion: %s-%s\nSection: database\nPriority: optional\n"
        "Architecture: %s\nMaintainer: OceanBase\nDepends: libaio1 | libaio1t64, systemd\n"
        "Description: seekdb single-node database\n" % (version, release, architecture))
    (control / "conffiles").write_text("/etc/seekdb/seekdb.cnf\n")
    for name, script in LIFECYCLE.items():
        copy(payload / "usr/libexec/seekdb/scripts" / (script + ".sh"), control / name, True)
    target = output / ("seekdb_%s-%s_%s.deb" % (version, release, architecture))
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(payload), str(target)], check=True)


def rpm(payload, work, output, version, release, dependencies, architecture=None):
    for directory in ("BUILD", "BUILDROOT", "RPMS", "SOURCES", "SPECS", "SRPMS"):
        (work / directory).mkdir()
    strip = dependencies / "usr/local/oceanbase/devtools/bin/llvm-strip"
    if not strip.is_file():
        strip = shutil.which("llvm-strip") or shutil.which("strip")
        if not strip:
            raise ValueError("RPM packaging requires llvm-strip or strip")
    spec = work / "SPECS/seekdb.spec"
    content = (ROOT / "package/rpm/seekdb.spec.in").read_text()
    libraries = payload / "usr/libexec/seekdb/lib"
    binaries = [payload / "usr/bin/seekdb"] + [path for path in libraries.rglob("*")
                                               if path.is_file() and not path.is_symlink()]
    debug_info = any(re.search(r"\.(?:z)?debug_info\b", subprocess.check_output(
        ["readelf", "-SW", str(path)], text=True)) for path in binaries)
    values = {"STRIP": str(strip), "VERSION": version, "RELEASE": release,
              "PAYLOAD": str(payload),
              "WITH_DEBUGINFO": "1" if debug_info else "0"}
    for name, script in (("PRE", "pre_install"), ("POST", "post_install"),
                         ("PREUN", "pre_uninstall"), ("POSTUN", "post_uninstall")):
        source = payload / "usr/libexec/seekdb/scripts" / (script + ".sh")
        values[name] = source.read_text().replace("%", "%%")
    for name, value in values.items():
        content = content.replace("@" + name + "@", value)
    spec.write_text(content)
    target = ["--target", architecture] if architecture else []
    subprocess.run(["rpmbuild", "-bb", *target, "--define", "_topdir " + str(work), str(spec)], check=True)
    for package in (work / "RPMS").rglob("*.rpm"):
        copy(package, output / package.name)


def tgz(payload, output, version, release, component, layout, architecture=None):
    system = "linux" if layout == "linux" else "macos" + platform.mac_ver()[0].split(".")[0]
    name = "seekdb" + ("-libs" if component == "libs" else "")
    target = output / ("%s-%s-%s-%s-%s.tar.gz" % (name, version, release, system, architecture or platform.machine()))
    def root_owner(info):
        info.uid = info.gid = 0
        info.uname = info.gname = "root"
        return info
    with tarfile.open(target, "w:gz") as archive:
        for item in sorted(payload.iterdir()):
            archive.add(item, arcname=item.name, filter=root_owner)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("format", choices=("binary", "stage", "rpm", "deb", "tgz"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--package-timestamp", "--release", dest="release",
                        default=datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"),
                        help="UTC package timestamp, YYYYMMDDTHHMMSSZ (default: current UTC time)")
    parser.add_argument("--artifacts", type=Path, default=ROOT / "build/bin",
                        help="Bazel bin directory from a --config=package build on Linux")
    parser.add_argument("--dependencies", type=Path, default=ROOT / "deps/3rd")
    parser.add_argument("--timezone-file", type=Path)
    parser.add_argument("--layout", choices=("linux", "macos", "windows", "android"),
                        help="target layout; inferred from --binary-package, otherwise Linux (native OS for binary)")
    parser.add_argument("--component", choices=("server", "libs"), default="server")
    inputs = parser.add_mutually_exclusive_group()
    inputs.add_argument("--binary-package", type=Path, help="verified seekdb-binary directory or tar archive; never builds sources")
    inputs.add_argument("--seekdb-bin", type=Path, help="existing native executable; resources still come from --artifacts")
    parser.add_argument("--runtime-lib-dir", type=Path, action="append", default=[],
                        help="additional Linux library search directory (repeatable)")
    parser.add_argument("--extra-library", type=Path, action="append", default=[],
                        help="additional Linux library loaded dynamically (repeatable)")
    parser.add_argument("--obshell-bin", type=Path, help="existing obshell for Linux system packages")
    parser.add_argument("--source-commit", help="full seekdb source commit recorded in a binary distribution")
    parser.add_argument("--glibc-version", help="Linux binary distribution ABI baseline, e.g. 2.28")
    parser.add_argument("--build-config", default="package", help="production build configuration recorded in the binary manifest")
    parser.add_argument("--output", type=Path, required=True)
    options = parser.parse_args()
    if options.format != "stage":
        try:
            timestamp = datetime.strptime(options.release, "%Y%m%dT%H%M%SZ")
            if timestamp.strftime("%Y%m%dT%H%M%SZ") != options.release:
                raise ValueError("noncanonical timestamp")
        except ValueError:
            parser.error("--package-timestamp must be a UTC timestamp in YYYYMMDDTHHMMSSZ format")
    if options.glibc_version and not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", options.glibc_version):
        parser.error("--glibc-version must be a numeric glibc version")
    if options.format == "binary":
        if options.binary_package or options.component != "server":
            parser.error("binary creates a distribution from build outputs or --seekdb-bin")
        if not options.source_commit or not re.fullmatch(r"[0-9a-f]{40}", options.source_commit):
            parser.error("binary requires --source-commit with the full seekdb source commit")
        options.layout = options.layout or {"Linux": "linux", "Darwin": "macos", "Windows": "windows"}.get(platform.system())
        if options.layout == "linux" and not options.glibc_version:
            parser.error("Linux binary distributions require --glibc-version")
    if options.binary_package and (options.runtime_lib_dir or options.extra_library or options.source_commit or options.glibc_version):
        parser.error("--binary-package supplies its own libraries and build metadata")
    if options.format != "binary" and options.source_commit:
        parser.error("--source-commit is recorded when creating a binary distribution")
    if options.format != "binary" and not options.seekdb_bin and (options.runtime_lib_dir or options.extra_library or options.glibc_version):
        parser.error("library collection options require binary or --seekdb-bin")
    if options.extra_library and options.layout not in (None, "linux"):
        parser.error("--extra-library supports Linux only")
    if not all(re.fullmatch(r"[0-9A-Za-z.+~_]+", value) for value in (options.version, options.release)):
        parser.error("version and release must contain only letters, digits, '.', '+', '~', '_'")
    if options.component == "libs" and options.layout not in (None, "linux"):
        parser.error("the libs component is Linux-only")
    if options.format in ("rpm", "deb") and (options.layout not in (None, "linux") or options.component != "server"):
        parser.error("RPM and DEB require the Linux server payload")
    if options.format == "tgz" and options.layout not in (None, "linux", "macos"):
        parser.error("TGZ supports Linux and macOS payloads")
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="seekdb-package-") as temporary:
        work = Path(temporary)
        binary_package = None
        metadata = None
        if options.binary_package:
            import binary
            binary_package, manifest = binary.load(options.binary_package.resolve(), work / "binary", options.version)
            metadata = manifest["platform"]
            if options.layout and options.layout != metadata["os"]:
                parser.error("binary package OS does not match --layout")
            options.layout = metadata["os"]
        options.layout = options.layout or "linux"
        if options.format in ("rpm", "deb") and options.layout != "linux":
            parser.error("RPM and DEB require a Linux binary package")
        if options.format == "tgz" and options.layout not in ("linux", "macos"):
            parser.error("TGZ supports Linux and macOS payloads")
        if options.component == "libs" and options.layout != "linux":
            parser.error("the libs component is Linux-only")
        if options.extra_library and options.layout != "linux":
            parser.error("--extra-library supports Linux only")
        if options.layout != "linux" and (options.runtime_lib_dir or options.glibc_version):
            parser.error("--runtime-lib-dir and --glibc-version support Linux only")
        if options.format == "binary" or options.seekdb_bin:
            import binary
            binary_package = work / "binary"
            stage(binary_package, options.artifacts.resolve(), options.dependencies.resolve(), options.layout,
                  options.version, options.release, timezone=options.timezone_file,
                  seekdb_bin=options.seekdb_bin, portable=True)
            original = options.seekdb_bin or options.artifacts / "src/observer" / (
                "seekdb/seekdb.exe" if options.layout == "windows" else "seekdb")
            directories = [path.resolve() for path in options.runtime_lib_dir]
            directories += [options.dependencies.resolve() / path for path in (
                "usr/local/oceanbase/devtools/lib64", "usr/local/oceanbase/deps/devel/lib",
                "usr/local/oceanbase/deps/devel/lib/vsag_lib")]
            metadata = binary.prepare(binary_package, original.resolve(), options.layout, directories,
                                      options.extra_library, options.glibc_version)
            if options.format == "binary":
                for name in ("LICENSE", "NOTICE"):
                    copy(ROOT / name, binary_package / "licenses" / name)
                metadata["build_config"] = options.build_config
                binary.write(binary_package, output, options.version, options.release, options.source_commit, metadata)
                return
        payload = output if options.format == "stage" else work / "server"
        stage(payload, options.artifacts.resolve(), options.dependencies.resolve(), options.layout,
              options.version, options.release, options.component, options.timezone_file,
              binary_package=binary_package, obshell_bin=options.obshell_bin)
        if metadata and options.layout == "linux" and options.component == "server":
            if binary.architecture(payload / "usr/bin/obshell", "linux") != metadata["arch"]:
                raise ValueError("obshell architecture does not match the seekdb binary")
            required = binary.glibc_requirement([payload / "usr/bin/obshell"])
            if binary.version_tuple(required) > binary.version_tuple(metadata["glibc"]):
                raise ValueError("obshell requires GLIBC_%s, exceeding the binary package baseline %s; "
                                 "rebuild obshell for the target glibc" % (required, metadata["glibc"]))
        if options.format in ("rpm", "deb"):
            libraries = payload / "usr/libexec/seekdb/lib"
            libraries.mkdir(parents=True, exist_ok=True)
            if binary_package:
                # System packages use the distribution's libaio, as in previous releases.
                shutil.copytree(binary_package / "lib/seekdb", libraries, dirs_exist_ok=True,
                                ignore=shutil.ignore_patterns("libaio.so", "libaio.so.*"))
            normalize_permissions(payload)
        if options.format == "rpm":
            rpm(payload, work, output, options.version, options.release, options.dependencies.resolve(),
                metadata["arch"] if metadata else None)
        elif options.format == "deb":
            deb(payload, output, options.version, options.release,
                {"x86_64": "amd64", "aarch64": "arm64"}[metadata["arch"]] if metadata else None)
        elif options.format == "tgz":
            tgz(payload, output, options.version, options.release, options.component, options.layout,
                metadata["arch"] if metadata else None)
            if options.layout == "linux" and options.component == "server":
                libs = work / "libs"
                stage(libs, options.artifacts.resolve(), options.dependencies.resolve(), "linux",
                      options.version, options.release, "libs", binary_package=binary_package)
                tgz(libs, output, options.version, options.release, "libs", "linux",
                    metadata["arch"] if metadata else None)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
