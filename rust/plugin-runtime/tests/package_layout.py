#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Install actual SQL packages, then read them through the C++/Rust adapter."""
import os
import pathlib
import subprocess
import sys
import tempfile


def main():
    binary, source, cmake = sys.argv[1:]
    source = pathlib.Path(source).resolve()
    with tempfile.TemporaryDirectory(prefix="seekdb-extension-layout-") as temporary:
        temporary = pathlib.Path(temporary)
        # Include the production installation rules in a no-language harness;
        # no optional native module or whole-server build is needed.
        harness = temporary / "source"
        harness.mkdir()
        (harness / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\n"
            "project(ExtensionLayout NONE)\n"
            'set(CMAKE_INSTALL_LIBDIR "lib")\n'
            'set(CMAKE_INSTALL_DATADIR "share")\n'
            'add_subdirectory("${PACKAGE_SOURCE}" packages)\n', encoding="utf-8")
        build = temporary / "build"
        prefix = temporary / "installed"
        subprocess.run([cmake, "-S", str(harness), "-B", str(build),
                        "-DPACKAGE_SOURCE=" + str(source),
                        "-DCMAKE_INSTALL_PREFIX=" + str(prefix)], check=True)
        # Do not accidentally publish into a caller's packaging DESTDIR.
        environment = dict(os.environ)
        environment.pop("DESTDIR", None)
        subprocess.run([cmake, "--install", str(build), "--component", "plugins"],
                       check=True, env=environment)
        root = prefix / "share/seekdb/extension"
        expected = [path for path in source.rglob("*")
                    if path.suffix in (".control", ".sql")]
        expected.extend((source.parent / "gis/sql").glob("gis*"))
        if len({path.name for path in expected}) != len(expected):
            raise AssertionError("source packages have conflicting flat artifact names")
        if {path.name for path in root.iterdir()} != {path.name for path in expected}:
            raise AssertionError("installation is not the complete flat control/SQL layout")
        for path in expected:
            if (root / path.name).read_bytes() != path.read_bytes():
                raise AssertionError("installed bytes differ: " + path.name)
        subprocess.run([sys.executable, str(pathlib.Path(__file__).with_name("package_source.py")),
                        binary, str(root)], check=True, timeout=30)
        print("PASS: flat CMake delivery and C++/Rust package reads; no server installation claimed")


if __name__ == "__main__":
    main()
