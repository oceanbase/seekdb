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
"""Opt-in GIS expression/LOB and native UDF regression using a production build.

Links real kernel objects and loads the actual GIS DSO. Activation metadata and
LOB storage transport, routine schemas, grants and column rows are controlled
fixtures; no server is started or modified. All GIS function descriptors in the
actual DSO are implementation-only; SQL calls use database routine declarations
from the package, including helper functions in native-call tests. Includes native CREATE resolution,
SHOW CREATE roundtrip, SQL-package statement resolution, SELECT execution and
PL caller body/dependency analysis, but not durable native creation, PL bytecode
execution or committed extension installation.
"""
import argparse
import json
import pathlib
import re
import shlex
import shutil
import subprocess
import tempfile

from kernel_script import validate_sql_build_configuration


def validate_declaration_inventory(descriptors, declarations):
    """Keep the SQL package exhaustive against the current typed GIS surface.

    This checks names/arity/element types, not catalog installation. The C++
    fixture below performs real parser, CREATE and exact DSO-binding checks.
    """
    rows = re.findall(
        r'GIS_SQL_FUNCTION\("([^"]+)", "([^"]+)",\s*"[^"]+", '
        r'(\d+), (\d+), (?:"[^"]+"|GIS_\w+), (\w+), ([^)]+)\)', descriptors)
    if not rows or len(rows) != descriptors.count('GIS_SQL_FUNCTION('):
        raise ValueError("unrecognized GIS descriptor syntax")
    statements = re.findall(
        r'CREATE FUNCTION `([^`]+)`\(([^\n]*)\)\nRETURNS [^\n]+\n'
        r'DETERMINISTIC NO SQL SQL SECURITY INVOKER\n'
        r"AS 'MODULE_PATHNAME', '([^']+)' LANGUAGE C;", declarations)
    if len(statements) != declarations.count('CREATE FUNCTION '):
        raise ValueError("unrecognized GIS SQL declaration syntax")
    by_name = {}
    implementations = {row[0] for row in rows if '.alias.' not in row[0]}
    for name, arguments, implementation in statements:
        if implementation not in implementations:
            raise ValueError("SQL declaration does not bind a canonical GIS implementation")
        parts = arguments.split(', ') if arguments else []
        types = []
        variadic = False
        for i, part in enumerate(parts):
            match = re.fullmatch(r'(VARIADIC )?arg\d+ (GEOMETRY|DOUBLE|BIGINT|LONGBLOB)(\[\])?', part)
            if not match or bool(match[1]) != bool(match[3]) or (match[1] and i != len(parts) - 1):
                raise ValueError("invalid GIS argument declaration")
            types.append(match[2])
            variadic = bool(match[1])
        by_name.setdefault(name, []).append((types, variadic))
    if len(rows) != len({row[1] for row in rows}) or set(by_name) != {row[1] for row in rows}:
        raise ValueError("GIS SQL names differ from the module inventory")
    mapping = {'g': 'GEOMETRY', 'd': 'DOUBLE', 'u': 'BIGINT', 'b': 'LONGBLOB'}
    for object_id, name, lo, hi, pattern, flags in rows:
        expected_id = object_id
        if '.alias.' in object_id:
            leaf = {'point': 'st_point', '_st_point': 'st_makepoint',
                    '_st_makepoint': 'st_makepoint', '_st_makeenvelope': 'st_makeenvelope',
                    'area': 'st_area', 'centroid': 'st_centroid'}.get(
                        name, name[1:] if name.startswith('_') else 'st_' + name)
            expected_id = 'org.seekdb.gis.function.' + leaf
        if any(implementation != expected_id for sql_name, _, implementation in statements if sql_name == name):
            raise ValueError("GIS SQL binds the wrong implementation: " + name)
        lo, hi = int(lo), int(hi)
        repeating = flags.strip() != '0'
        for types, variadic in by_name[name]:
            if not lo <= len(types) <= hi or (variadic and not repeating):
                raise ValueError("SQL declaration arity does not fit " + name)
        for count in range(lo, hi + 1):
            matches = [(types, variadic) for types, variadic in by_name[name]
                       if count == len(types) or (variadic and count > len(types))]
            if len(matches) != 1:
                raise ValueError("missing or ambiguous GIS arity: {}({})".format(name, count))
            types, variadic = matches[0]
            actual = types + [types[-1]] * (count - len(types))
            expected = [mapping[pattern[min(i, len(pattern) - 1)]] for i in range(count)]
            if actual != expected:
                raise ValueError("GIS SQL element types differ: " + name)
    return len(by_name), len(statements)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    source = pathlib.Path(__file__).resolve().parents[3]
    descriptor_source = (source / "plugins/gis/seekdb_gis_plugin.c").read_text()
    descriptor_source = descriptor_source.split('static const seekdb_plugin_function_descriptor_v2_t gis_functions[] = {', 1)[1].split('\n};', 1)[0]
    names, declarations = validate_declaration_inventory(
        descriptor_source, (source / "plugins/gis/sql/gis--1.0.sql").read_text())
    print("GIS declaration inventory: {} names, {} declarations".format(names, declarations), flush=True)
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
        packages = stage / "packages"
        shutil.copytree(source / "plugins/gis/sql", packages)
        for index, statement in enumerate((
                "GRANT EXECUTE ON FUNCTION native_db.area_alias(GEOMETRY) TO 'caller'@'localhost'",
                "REVOKE EXECUTE ON FUNCTION native_db.area_alias(GEOMETRY) FROM 'caller'@'localhost'",
                "GRANT EXECUTE ON FUNCTION native_db.area_alias(GEOMETRY) TO 'caller'@'localhost', 'owner'@'localhost' WITH GRANT OPTION",
                "REVOKE GRANT OPTION FOR EXECUTE ON FUNCTION native_db.area_alias(GEOMETRY) FROM 'owner'@'localhost', 'caller'@'localhost' CASCADE")):
            name = f"dcl_update_{index}"
            (packages / f"{name}.control").write_text("default_version = '2'\nnative_module = 'org.seekdb.gis'\n")
            (packages / f"{name}--1--2.sql").write_text(statement + ";\n")
        result = subprocess.run([str(binary), str(artifact), str(packages)],
                                cwd=stage, timeout=120)
        if result.returncode:
            for name in ("gis_sql.log", "gis_sql.log.wf"):
                log = stage / name
                if log.exists():
                    print(log.read_text(errors="replace")[-16000:])
        result.check_returncode()


if __name__ == "__main__":
    main()
