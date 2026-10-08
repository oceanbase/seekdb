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

"""Original WKT/EWKB input and WKT visitors versus the GIS DSO; no server/storage."""
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
    link = shlex.split((build / "src/observer/CMakeFiles/seekdb.dir/link.txt").read_text())
    main_index, = [i for i, arg in enumerate(link) if arg.endswith("/ob_main.dir/main.cpp.o")]
    subprocess.run(["cmake", "--build", str(build), "--target", "seekdb_gis_plugin", "-j2"], check=True)
    with tempfile.TemporaryDirectory(prefix="seekdb-gis-wkt-") as directory:
        stage = pathlib.Path(directory)
        # CORE_GIS=OFF has a deliberately nonfunctional type-name stub. Supply
        # the exact original helper under a test-local name, without replacing
        # any production symbol. The visitor algorithms below are unchanged.
        original = (source / "src/share/geo/ob_geo_utils.cpp").read_text()
        begin = original.index("const char *ObGeoTypeUtil::get_geo_name_by_type(")
        end = original.index("int ObGeoTypeUtil::get_st_geo_name_by_type(", begin)
        helper = "static " + original[begin:end].replace("ObGeoTypeUtil::get_geo_name_by_type", "legacy_geo_name_by_type")
        begin = original.index("ObGeoType ObGeoTypeUtil::get_geo_type_by_name(")
        end = original.index("const char *ObGeoTypeUtil::get_geo_name_by_type(", begin)
        helper += "static " + original[begin:end].replace("ObGeoTypeUtil::get_geo_type_by_name", "legacy_geo_type_by_name")
        begin = original.index("bool ObGeoTypeUtil::is_3d_geo_type(")
        end = original.index("bool ObGeoTypeUtil::is_2d_geo_type(", begin)
        helper += "static " + original[begin:end].replace("ObGeoTypeUtil::is_3d_geo_type", "legacy_is_3d_geo_type")
        (stage / "legacy_geo_name_probe.h").write_text(
            '#include "share/geo/ob_geo_utils.h"\nnamespace oceanbase { namespace common {\n' + helper + "}}\n")
        paths = [pathlib.Path(__file__).with_suffix(".cpp").resolve()]
        ewkb = (source / "src/sql/engine/expr/ob_expr_st_geomfromewkb.cpp").read_text()
        begin = ewkb.index("int ObExprPrivSTGeomFromEWKB::get_header_info_from_ewkb(")
        end = ewkb.index("int ObExprPrivSTGeomFromEWKB::create_geo_by_ewkb(", begin)
        # Keep the original header and root rewrite bodies, under a test-only
        # class name. Do not call the CORE_GIS=OFF factory stubs.
        (stage / "legacy_ewkb_header.h").write_text(
            '#include "share/geo/ob_geo_utils.h"\nnamespace oceanbase { namespace common {\n'
            'struct LegacyEwkbHeader {\n'
            'static int get_header_info_from_ewkb(const ObString &, ObGeoWkbHeader &);\n'
            'static int construct_ewkb_data(ObString &, ObString &);\n};\n'
            + ewkb[begin:end].replace("ObExprPrivSTGeomFromEWKB", "LegacyEwkbHeader") + "}}\n")
        header = (source / "src/share/geo/ob_wkt_parser.h").read_text()
        header = header.replace("ObWktParser", "LegacyWktParser").replace("private:", "public:")
        header = header.replace("OCEANBASE_LIB_GEO_OB_WKT_PARSER_", "SEEKDB_TEST_LEGACY_WKT_PARSER_")
        (stage / "legacy_wkt_parser.h").write_text(header)
        parser = (source / "src/share/geo/ob_wkt_parser.cpp").read_text()
        parser = parser.replace('"ob_wkt_parser.h"', '"legacy_wkt_parser.h"').replace("ObWktParser", "LegacyWktParser")
        parser = parser.replace("ObGeoTypeUtil::get_geo_type_by_name", "legacy_geo_type_by_name")
        parser = parser.replace("ObGeoTypeUtil::is_3d_geo_type", "legacy_is_3d_geo_type")
        parser_path = stage / "legacy_wkt_parser.cpp"
        parser_path.write_text('#include "legacy_geo_name_probe.h"\n' + parser)
        paths.append(parser_path)
        common = (source / "src/share/geo/ob_geo_common.cpp").read_text()
        begin = common.index("template<>\nvoid ObGeoWkbByteOrderUtil::write<double>")
        end = common.index("} // namespace common", begin)
        writes = stage / "legacy_wkb_write.cpp"
        writes.write_text('#include "share/geo/ob_geo_common.h"\nnamespace oceanbase { namespace common {\n'
                          + common[begin:end] + "}}\n")
        paths.append(writes)
        for name in ("ob_geo_to_wkt_visitor.cpp", "ob_geo_3d.cpp", "ob_geo_ibin.cpp", "ob_geo_bin.cpp", "ob_geo_visitor.cpp", "ob_geo_wkb_check_visitor.cpp"):
            path = source / "src/share/geo" / name
            if name in ("ob_geo_to_wkt_visitor.cpp", "ob_geo_3d.cpp"):
                text = path.read_text().replace("ObGeoTypeUtil::get_geo_name_by_type", "legacy_geo_name_by_type")
                text = text.replace("ObGeoTypeUtil::is_3d_geo_type", "legacy_is_3d_geo_type")
                copied = stage / name
                copied.write_text('#include "legacy_geo_name_probe.h"\n' + text)
                path = copied
            paths.append(path)
        objects = []
        for index, path in enumerate(paths):
            obj = stage / (str(index) + ".o")
            command = shlex.split(entry["command"])
            command = [arg.replace("SEEKDB_ENABLE_CORE_GIS=0", "SEEKDB_ENABLE_CORE_GIS=1") for arg in command]
            command[command.index("-o") + 1] = str(obj)
            command[command.index("-c") + 1] = str(path)
            command.extend(["-I", str(stage), "-I", str(source / "src/share/geo")])
            subprocess.run(command, cwd=entry["directory"], check=True)
            objects.append(str(obj))
        binary = stage / "gis_wkt"
        link[link.index("-o") + 1] = str(binary)
        link[main_index:main_index + 1] = objects
        subprocess.run(link, cwd=build / "src/observer", check=True)
        artifact = stage / "seekdb_gis.so"
        shutil.copy2(build / "plugins/gis/seekdb_gis.so", artifact)
        subprocess.run([str(binary), str(artifact)], cwd=stage, timeout=120, check=True)


if __name__ == "__main__":
    main()
