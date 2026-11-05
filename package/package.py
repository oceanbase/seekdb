#!/usr/bin/env python3
"""Stage Bazel release artifacts and build native seekdb packages."""

import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
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


def stage(destination, artifacts, dependencies, layout, version, release, component="server", timezone=None):
    if component == "libs":
        for name in ("libaio.so", "libaio.so.1", "libaio.so.1.0.1"):
            source = dependencies / "usr/local/oceanbase/deps/devel/lib" / name
            target = destination / "usr/libexec/seekdb/lib" / name
            if source.is_symlink():
                target.parent.mkdir(parents=True, exist_ok=True)
                target.symlink_to(os.readlink(source))
            else:
                copy(source, target, True)
        return
    paths = {
        "linux": ("usr/bin", "etc/seekdb", "usr/share/seekdb", "usr/libexec/seekdb"),
        "macos": ("opt/seekdb/bin", "opt/seekdb/etc/seekdb", "opt/seekdb/share/seekdb", "opt/seekdb/libexec/seekdb"),
        "windows": ("bin", "etc", "share", "bin"),
    }
    timezone = timezone or Path(os.environ.get("SEEKDB_TIMEZONE_FILE", ROOT / "tools/timezone_V1.log"))
    if not timezone.is_file():
        raise ValueError("required timezone data is missing: %s; supply --timezone-file or SEEKDB_TIMEZONE_FILE" % timezone)
    binary, config, share, helpers = [destination / path for path in paths[layout]]
    executable = "seekdb.exe" if layout == "windows" else "seekdb"
    sql = sorted((artifacts / SYSPACK).glob("*.sql"))
    if not sql:
        raise ValueError("missing generated SQL in %s; build //src/share/inner_table/sys_package:syspack_release" % (artifacts / SYSPACK))
    output = artifacts / "src/observer"
    if layout == "windows":
        output /= "seekdb"
        for name in json.loads((output / "seekdb.exe.runtime.json").read_text()):
            copy(output / name, binary / name, True)
    copy(output / executable, binary / executable, True)
    for source in sql:
        copy(source, share / "admin" / source.name)
    copy(timezone, share / "timezone/timezone_V1.log")
    copy(ROOT / "tools/default_srs_data_mysql.sql", share / "srs/default_srs_data_mysql.sql")
    variables = json.loads((ROOT / "src/share/system_variable/ob_system_variable_init.json").read_text())
    def clear_links(value):
        if isinstance(value, dict):
            return {key: "" if key == "ref_url" else clear_links(item) for key, item in value.items()}
        if isinstance(value, list):
            return [clear_links(item) for item in value]
        return value
    config.mkdir(parents=True, exist_ok=True)
    (config / "ob_system_variable_init.json").write_text(json.dumps(clear_links(variables), indent=2) + "\n")
    if layout == "windows":
        copy(ROOT / "package/msi/seekdb.cnf", config / "seekdb.cnf")
        return
    for script in ("import_time_zone_info.py", "import_srs_data.py"):
        copy(ROOT / "tools" / script, helpers / script, True)
    profile = ROOT / ("package/linux/profile" if layout == "linux" else "package/pkg/launchd/profile")
    copy(profile / "seekdb.cnf", config / "seekdb.cnf")
    if layout == "linux":
        copy(dependencies / "home/admin/oceanbase/bin/obshell", binary / "obshell", True)
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


def deb(payload, output, version, release):
    architecture = subprocess.check_output(["dpkg", "--print-architecture"], text=True).strip()
    control = payload / "DEBIAN"
    control.mkdir()
    (control / "control").write_text(
        "Package: seekdb\nVersion: %s-%s\nSection: database\nPriority: optional\n"
        "Architecture: %s\nMaintainer: OceanBase\nDepends: systemd\n"
        "Description: seekdb single-node database\n" % (version, release, architecture))
    (control / "conffiles").write_text("/etc/seekdb/seekdb.cnf\n")
    for name, script in LIFECYCLE.items():
        copy(payload / "usr/libexec/seekdb/scripts" / (script + ".sh"), control / name, True)
    target = output / ("seekdb_%s-%s_%s.deb" % (version, release, architecture))
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(payload), str(target)], check=True)


def rpm(payload, libs, work, output, version, release, dependencies):
    for directory in ("BUILD", "BUILDROOT", "RPMS", "SOURCES", "SPECS", "SRPMS"):
        (work / directory).mkdir()
    strip = dependencies / "usr/local/oceanbase/devtools/bin/llvm-strip"
    spec = work / "SPECS/seekdb.spec"
    content = (ROOT / "package/rpm/seekdb.spec.in").read_text()
    values = {"STRIP": str(strip), "VERSION": version, "RELEASE": release,
              "PAYLOAD": str(payload), "LIBS": str(libs)}
    for name, script in (("PRE", "pre_install"), ("POST", "post_install"),
                         ("PREUN", "pre_uninstall"), ("POSTUN", "post_uninstall")):
        source = payload / "usr/libexec/seekdb/scripts" / (script + ".sh")
        values[name] = source.read_text().replace("%", "%%")
    for name, value in values.items():
        content = content.replace("@" + name + "@", value)
    spec.write_text(content)
    subprocess.run(["rpmbuild", "-bb", "--define", "_topdir " + str(work), str(spec)], check=True)
    for package in (work / "RPMS").rglob("*.rpm"):
        copy(package, output / package.name)


def tgz(payload, output, version, release, component, layout):
    system = "linux" if layout == "linux" else "macos" + platform.mac_ver()[0].split(".")[0]
    name = "seekdb" + ("-libs" if component == "libs" else "")
    target = output / ("%s-%s-%s-%s-%s.tar.gz" % (name, version, release, system, platform.machine()))
    def root_owner(info):
        info.uid = info.gid = 0
        info.uname = info.gname = "root"
        return info
    with tarfile.open(target, "w:gz") as archive:
        for item in sorted(payload.iterdir()):
            archive.add(item, arcname=item.name, filter=root_owner)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("format", choices=("stage", "rpm", "deb", "tgz"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--release", required=True)
    parser.add_argument("--artifacts", type=Path, default=ROOT / "build/bin",
                        help="Bazel bin directory from a --config=package build on Linux")
    parser.add_argument("--dependencies", type=Path, default=ROOT / "deps/3rd")
    parser.add_argument("--timezone-file", type=Path)
    parser.add_argument("--layout", choices=("linux", "macos", "windows"), default="linux")
    parser.add_argument("--component", choices=("server", "libs"), default="server")
    parser.add_argument("--output", type=Path, required=True)
    options = parser.parse_args()
    if not all(re.fullmatch(r"[0-9A-Za-z.+~_]+", value) for value in (options.version, options.release)):
        parser.error("version and release must contain only letters, digits, '.', '+', '~', '_'")
    if options.component == "libs" and options.layout != "linux":
        parser.error("the libs component is Linux-only")
    if options.format in ("rpm", "deb") and (options.layout != "linux" or options.component != "server"):
        parser.error("RPM and DEB require the Linux server payload")
    if options.format == "tgz" and options.layout == "windows":
        parser.error("TGZ supports Linux and macOS payloads")
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="seekdb-package-") as temporary:
        work = Path(temporary)
        payload = output if options.format == "stage" else work / "server"
        stage(payload, options.artifacts.resolve(), options.dependencies.resolve(), options.layout,
              options.version, options.release, options.component, options.timezone_file)
        if options.format == "rpm":
            libs = work / "libs"
            stage(libs, options.artifacts.resolve(), options.dependencies.resolve(), "linux",
                  options.version, options.release, "libs")
            rpm(payload, libs, work, output, options.version, options.release, options.dependencies.resolve())
        elif options.format == "deb":
            stage(payload, options.artifacts.resolve(), options.dependencies.resolve(), "linux",
                  options.version, options.release, "libs")
            deb(payload, output, options.version, options.release)
        elif options.format == "tgz":
            tgz(payload, output, options.version, options.release, options.component, options.layout)
            if options.layout == "linux" and options.component == "server":
                libs = work / "libs"
                stage(libs, options.artifacts.resolve(), options.dependencies.resolve(), "linux",
                      options.version, options.release, "libs")
                tgz(libs, output, options.version, options.release, "libs", "linux")


if __name__ == "__main__":
    main()
