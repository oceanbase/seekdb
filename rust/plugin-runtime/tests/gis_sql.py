#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
#
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
"""Opt-in GIS expression/LOB regression using a completed production build.

Links real kernel objects and loads the actual GIS DSO. Activation metadata and
LOB storage transport are controlled fixtures; no server is started or modified.
"""
import argparse
import json
import pathlib
import shlex
import shutil
import subprocess
import tempfile

from kernel_script import validate_sql_build_configuration


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    source = pathlib.Path(__file__).resolve().parents[3]
    build = args.build_dir.resolve()
    entries = json.loads((build / "compile_commands.json").read_text())
    validate_sql_build_configuration(build, entries)
    entry, = [item for item in entries if pathlib.Path(item["file"]) == source / "src/observer/main.cpp"]
    compile_command = shlex.split(entry["command"])
    link_command = shlex.split((build / "src/observer/CMakeFiles/seekdb.dir/link.txt").read_text())
    main_index, = [i for i, arg in enumerate(link_command) if arg.endswith("/ob_main.dir/main.cpp.o")]
    subprocess.run(["cmake", "--build", str(build), "--target", "seekdb_gis_plugin", "-j2"], check=True)
    with tempfile.TemporaryDirectory(prefix="seekdb-gis-sql-") as directory:
        stage = pathlib.Path(directory)
        obj, binary = stage / "gis_sql.o", stage / "gis_sql"
        compile_command[compile_command.index("-o") + 1] = str(obj)
        compile_command[compile_command.index("-c") + 1] = str(pathlib.Path(__file__).with_suffix(".cpp").resolve())
        subprocess.run(compile_command, cwd=entry["directory"], check=True)
        link_command[link_command.index("-o") + 1] = str(binary)
        link_command[main_index] = str(obj)
        subprocess.run(link_command, cwd=build / "src/observer", check=True)
        artifact = stage / "seekdb_gis.so"
        shutil.copy2(build / "plugins/gis/seekdb_gis.so", artifact)
        result = subprocess.run([str(binary), str(artifact)], cwd=stage, timeout=120)
        if result.returncode:
            log = stage / "gis_sql.log"
            if log.exists():
                print(log.read_text(errors="replace")[-16000:])
        result.check_returncode()


if __name__ == "__main__":
    main()
