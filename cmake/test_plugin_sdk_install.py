#!/usr/bin/env python3
"""Install the production public SDK and compile isolated C/C++ consumers.

No server build or source-tree include path is used by the consumers. Each
public header is compiled separately to catch missing transitive includes.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def run(*args):
    subprocess.run(args, check=True, timeout=60)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc")
    parser.add_argument("--cxx")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    headers = sorted((source / "include/seekdb/plugin").glob("*.h"))
    if not headers:
        raise RuntimeError("public SDK header inventory is empty")
    with tempfile.TemporaryDirectory(prefix="seekdb-sdk-install-") as temporary:
        stage = Path(temporary)
        producer = stage / "producer"
        consumer = stage / "consumer"
        prefix = stage / "installed"
        producer.mkdir()
        consumer.mkdir()
        # Exercise the real install/export rules without configuring the server.
        (producer / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            'project(SDKInstallProbe VERSION 1.0.0 LANGUAGES C CXX)\n'
            'set(PROJECT_SOURCE_DIR "${SEEKDB_SOURCE_DIR}")\n'
            'include("${SEEKDB_SOURCE_DIR}/cmake/Plugin.cmake")\n')
        compilers = []
        if args.cc:
            compilers.append(f"-DCMAKE_C_COMPILER={args.cc}")
        if args.cxx:
            compilers.append(f"-DCMAKE_CXX_COMPILER={args.cxx}")
        run("cmake", "-S", str(producer), "-B", str(stage / "producer-build"),
            f"-DSEEKDB_SOURCE_DIR={source}", *compilers)
        run("cmake", "--install", str(stage / "producer-build"),
            "--prefix", str(prefix), "--component", "plugin-sdk")
        targets = []
        for index, header in enumerate(headers):
            installed = prefix / "include/seekdb/plugin" / header.name
            if not installed.is_file() or installed.read_bytes() != header.read_bytes():
                raise RuntimeError(f"SDK header missing or stale: {header.name}")
            for suffix in ("c", "cpp"):
                name = f"header_{index}_{suffix}"
                (consumer / f"{name}.{suffix}").write_text(
                    f'#include <seekdb/plugin/{header.name}>\n')
                targets.append(
                    f"add_library({name} OBJECT {name}.{suffix})\n"
                    f"target_link_libraries({name} PRIVATE seekdb::plugin_sdk)\n")
        (consumer / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            'project(SDKConsumer LANGUAGES C CXX)\n'
            'find_package(SeekDBPluginSDK 1.0.0 EXACT REQUIRED CONFIG\n'
            '  PATHS "${SDK_PREFIX}" NO_DEFAULT_PATH)\n' + "".join(targets))
        # Do not allow ambient include paths to mask an incomplete installation.
        for name in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH"):
            os.environ.pop(name, None)
        run("cmake", "-S", str(consumer), "-B", str(stage / "consumer-build"),
            f"-DSDK_PREFIX={prefix}", *compilers)
        run("cmake", "--build", str(stage / "consumer-build"), "--parallel", "2")
        print(f"Installed SDK: {len(headers)} headers verified independently in C and C++")


if __name__ == "__main__":
    main()
