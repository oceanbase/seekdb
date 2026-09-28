# Copyright (c) 2025 OceanBase.
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
"""Compare plugin-owned SRS parse records with the real core parser adapter.

Uses production compilation/link flags and archives, no server or catalog
writes. This verifies parse records, factory metadata and coordinate-unit
normalization, not projection transforms or live catalog refresh. The host SRS
bridge is exercised separately by gis_sql.py.
"""
import argparse
import json
import pathlib
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    options = parser.parse_args()
    source = pathlib.Path(__file__).resolve().parents[3]
    build = options.build_dir.resolve()
    entries = json.loads((build / "compile_commands.json").read_text())
    entry, = [item for item in entries if pathlib.Path(item["file"]) == source / "src/observer/main.cpp"]
    compile_command = shlex.split(entry["command"])
    link_command = shlex.split((build / "src/observer/CMakeFiles/seekdb.dir/link.txt").read_text())
    main_index, = [i for i, arg in enumerate(link_command) if arg.endswith("/ob_main.dir/main.cpp.o")]
    with tempfile.TemporaryDirectory(prefix="seekdb-gis-srs-") as directory:
        stage = pathlib.Path(directory)
        obj, binary = stage / "srs.o", stage / "srs"
        compile_command[compile_command.index("-o") + 1] = str(obj)
        compile_command[compile_command.index("-c") + 1] = str(source / "rust/plugin-runtime/tests/gis_srs_parser_probe.cpp")
        compile_command.append("-DSEEKDB_TEST_CORE_SRS=1")
        subprocess.run(compile_command, cwd=entry["directory"], check=True)
        link_command[link_command.index("-o") + 1] = str(binary)
        link_command[main_index] = str(obj)
        subprocess.run(link_command, cwd=build / "src/observer", check=True)
        subprocess.run([str(binary), str(source / "tools/obtest/sql/default_srs_data_mysql.sql")],
                       cwd=stage, check=True)
    print("PASS: actual core parser/factory and plugin metadata/normalization parity; no live SRS-service claims")


if __name__ == "__main__":
    main()
