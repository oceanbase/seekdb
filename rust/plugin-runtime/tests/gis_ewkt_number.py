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
"""Compare plugin EWKT formatting with original dtoa/ObNumber using kernel objects.

Does not start a server. Extracts the original precision formatter rather than
maintaining a handwritten expected-value implementation.
"""
import argparse
import json
import pathlib
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    source = pathlib.Path(__file__).resolve().parents[3]
    build = args.build_dir.resolve()
    entries = json.loads((build / "compile_commands.json").read_text())
    entry, = [item for item in entries if pathlib.Path(item["file"]) == source / "src/observer/main.cpp"]
    link = shlex.split((build / "src/observer/CMakeFiles/seekdb.dir/link.txt").read_text())
    main_index, = [i for i, arg in enumerate(link) if arg.endswith("/ob_main.dir/main.cpp.o")]
    with tempfile.TemporaryDirectory(prefix="seekdb-gis-ewkt-number-") as directory:
        stage = pathlib.Path(directory)
        original = (source / "src/share/geo/ob_geo_to_wkt_visitor.cpp").read_text()
        begin = original.index("const double NOSCI_MIN_DOUBLE")
        end = original.index("// need to reserve buff before", begin)
        oracle = original[begin:end].replace(
            "int ObGeoToWktVisitor::append_double_with_prec(",
            "static int legacy_append_double_with_prec(", 1)
        (stage / "legacy_ewkt_number_probe.h").write_text(oracle)
        objects = []
        for name, path in (
                ("probe", pathlib.Path(__file__).with_suffix(".cpp").resolve()),
                ("formatter", source / "plugins/gis/number_format.cpp")):
            obj = stage / (name + ".o")
            command = shlex.split(entry["command"])
            command[command.index("-o") + 1] = str(obj)
            command[command.index("-c") + 1] = str(path)
            command.extend(["-I", str(stage)])
            subprocess.run(command, cwd=entry["directory"], check=True)
            objects.append(str(obj))
        binary = stage / "gis_ewkt_number"
        link[link.index("-o") + 1] = str(binary)
        link[main_index:main_index + 1] = objects
        subprocess.run(link, cwd=build / "src/observer", check=True)
        subprocess.run([str(binary)], cwd=stage, timeout=120, check=True)


if __name__ == "__main__":
    main()
