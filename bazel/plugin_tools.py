#!/usr/bin/env python3
"""Build-time plugin validation and reproducible distribution archives."""

import argparse
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).absolute().parents[1] / "cmake"))
from plugin_profile import profile
from plugin_boundary_check import check_plugin_includes


def write_archive(output, files):
    """files maps installation-relative names to bytes or source Paths."""
    with tarfile.open(output, "w") as archive:
        for name, source in sorted(files.items()):
            if name.startswith("/") or ".." in name.split("/"):
                raise ValueError("unsafe installation path: " + name)
            data = source.read_bytes() if isinstance(source, Path) else source
            member = tarfile.TarInfo(name)
            member.size = len(data)
            member.mode = 0o755 if name.endswith((".so", ".dll", ".dylib")) else 0o644
            archive.addfile(member, io.BytesIO(data))


def audit(args):
    source = Path.cwd()
    # Bazel materializes declared inputs as symlinks into the source tree.
    # Check a private, dereferenced copy so the strict realpath boundary
    # checks see the same tree layout as a normal checkout.
    with tempfile.TemporaryDirectory() as directory:
        staged = Path(directory)
        relative_manifest = args.manifest.absolute().relative_to(source)
        plugin_dir = relative_manifest.parent
        shutil.copytree(source / plugin_dir, staged / plugin_dir)
        if args.server_headers:
            shutil.copytree(source / "src", staged / "src")
        manifest = staged / relative_manifest
        selected = profile(manifest, staged)
        errors = []
        check_plugin_includes(staged, errors)
        if errors:
            raise ValueError("\n".join(errors))
    if selected["profile"] != args.profile or sorted(selected["exports"]) != sorted(args.exports):
        raise ValueError("Bazel profile/exports do not match plugin.toml")
    if sorted(selected["headers"]) != sorted(args.server_headers):
        raise ValueError("Bazel server_headers do not match plugin.toml")
    command = [sys.executable, str(Path(__file__).absolute().parents[1] / "cmake/plugin_binary_check.py"),
               "--binary", str(args.binary), "--nm", args.nm]
    for symbol in selected["exports"]:
        command.extend(["--allow-export", symbol])
    subprocess.run(command, check=True)
    # Only expose an installable binary after every gate has passed.
    shutil.copyfile(args.binary, args.output)


def package(args):
    prefix = "lib/seekdb/plugins/" + args.name + "/"
    files = {prefix + args.binary.name: args.binary, prefix + "plugin.toml": args.manifest}
    for source in args.sql:
        name = "share/seekdb/extension/" + source.name
        if name in files:
            raise ValueError("duplicate extension package file: " + name)
        files[name] = source
    write_archive(args.output, files)


def sdk(args):
    files = {"include/seekdb/plugin/" + path.name: path for path in args.headers}
    prefix = "lib/cmake/SeekDBPluginSDK/"
    files[prefix + "SeekDBPluginSDKConfig.cmake"] = b'''# Public ABI only; no server source tree or library dependency.
get_filename_component(_seekdb_sdk_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET seekdb::plugin_sdk)
  add_library(seekdb::plugin_sdk INTERFACE IMPORTED)
  set_target_properties(seekdb::plugin_sdk PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_seekdb_sdk_root}/include"
    INTERFACE_COMPILE_FEATURES "c_std_99;cxx_std_11")
endif()
unset(_seekdb_sdk_root)
'''
    files[prefix + "SeekDBPluginSDKConfigVersion.cmake"] = (
        'set(PACKAGE_VERSION "' + args.version + '")\n'
        'set(PACKAGE_VERSION_COMPATIBLE FALSE)\n'
        'if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)\n'
        '  set(PACKAGE_VERSION_COMPATIBLE TRUE)\n'
        '  set(PACKAGE_VERSION_EXACT TRUE)\n'
        'endif()\n').encode()
    write_archive(args.output, files)


def sql_package(args):
    files = {}
    for source in args.sql:
        name = "share/seekdb/extension/" + source.name
        if name in files:
            raise ValueError("duplicate extension package file: " + name)
        files[name] = source
    write_archive(args.output, files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("audit")
    check.add_argument("--manifest", type=Path, required=True)
    check.add_argument("--binary", type=Path, required=True)
    check.add_argument("--output", type=Path, required=True)
    check.add_argument("--nm", required=True)
    check.add_argument("--profile", default="public")
    check.add_argument("--exports", nargs="*", default=[])
    check.add_argument("--server-headers", nargs="*", default=[])
    bundle = commands.add_parser("package")
    bundle.add_argument("--name", required=True)
    bundle.add_argument("--manifest", type=Path, required=True)
    bundle.add_argument("--binary", type=Path, required=True)
    bundle.add_argument("--output", type=Path, required=True)
    bundle.add_argument("--sql", type=Path, nargs="*", default=[])
    headers = commands.add_parser("sdk")
    headers.add_argument("--output", type=Path, required=True)
    headers.add_argument("--version", required=True)
    headers.add_argument("--headers", type=Path, nargs="+", required=True)
    scripts = commands.add_parser("sql")
    scripts.add_argument("--output", type=Path, required=True)
    scripts.add_argument("--sql", type=Path, nargs="+", required=True)
    args = parser.parse_args()
    {"audit": audit, "package": package, "sdk": sdk, "sql": sql_package}[args.command](args)


if __name__ == "__main__":
    main()
