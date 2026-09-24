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
"""Test native declarations, catalog fields (--catalog-only), or DDL writer (--writer-only).

Uses an existing Linux server build's compiler/link flags and unchanged support
archives. Default mode recompiles every PL parser object (token numbers can
change together), the routine resolver, and a private test main. Catalog/writer
modes compile only their test main and link the real production implementations.
All modes require a complete coherent server build, never replace production
objects or install/start a server, and do not claim live SQL transaction coverage.
"""
import argparse
import concurrent.futures
import json
import os
import pathlib
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--catalog-only", action="store_true",
                        help="test native routine schema/reader/writer against a freshly completed server build")
    modes.add_argument("--writer-only", action="store_true",
                       help="test real routine DDL writer with controlled SQL/module transport, without auxiliary DSO builds")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    source = pathlib.Path(__file__).resolve().parents[3]
    entries = json.loads((build / "compile_commands.json").read_text())
    server = build / "src/observer/seekdb"
    critical_sources = [
        "include/seekdb/plugin/extension_spi.h", "include/seekdb/plugin/execution_spi.h",
        "src/share/schema/ob_routine_info.h", "src/share/schema/ob_routine_info.cpp",
        "src/share/schema/ob_routine_sql_service.h", "src/share/schema/ob_routine_sql_service.cpp",
        "src/observer/schema/ob_schema_retrieve_utils.ipp",
        "src/share/inner_table/ob_inner_table_schema_def.py",
        "src/share/rc/ob_module_provider.h", "src/observer/ob_server_plugin_runtime.cpp",
        "src/share/plugin/ob_plugin_catalog.h", "src/share/plugin/ob_plugin_catalog.cpp",
        "src/rootserver/pl_ddl/ob_pl_ddl_service.cpp",
        "src/rootserver/pl_ddl/ob_pl_ddl_service.h",
        "src/rootserver/pl_ddl/native_routine_privilege_transaction.h",
        "src/rootserver/pl_ddl/native_routine_grant_plan.h",
        "src/rootserver/pl_ddl/native_routine_acl_version_reservation.h",
        "src/rootserver/pl_ddl/routine_ddl_invalidation.h",
        "src/rootserver/ob_ddl_service.h", "src/rootserver/ob_ddl_service.cpp",
        "src/observer/ob_server.h", "src/observer/ob_server.cpp",
        "src/rootserver/ob_ddl_operator.cpp",
        "src/share/schema/native_routine_admission.h",
        "src/share/schema/native_routine_create_slot.h",
        "src/share/schema/native_routine_signature.h",
        "src/share/schema/routine_schema_overlay.h",
        "src/share/schema/routine_privilege_overlay.h", "src/share/schema/routine_catalog_savepoint.h",
        "src/share/schema/ob_priv_mgr.h",
        "src/share/schema/ob_routine_mgr.h", "src/share/schema/ob_routine_mgr.cpp",
        "src/share/schema/ob_schema_getter_guard.h", "src/share/schema/ob_schema_getter_guard.cpp",
        "src/share/schema/ob_schema_getter_guard_priv.cpp", "src/share/schema/ob_priv_mgr.cpp",
        "src/share/schema/ob_priv_sql_service.cpp", "src/share/schema/ob_priv_sql_service.h",
        "src/sql/resolver/dcl/ob_grant_resolver.h", "src/sql/resolver/dcl/ob_grant_resolver.cpp",
        "src/sql/resolver/dcl/ob_revoke_resolver.cpp", "src/sql/resolver/dcl/ob_grant_stmt.h",
        "src/sql/resolver/dcl/ob_revoke_stmt.h", "src/sql/engine/cmd/ob_dcl_executor.cpp",
        "src/rootserver/ob_ddl_service.cpp", "src/rootserver/ob_local_management_service.cpp",
        "src/sql/resolver/ob_resolver_utils.cpp",
        "src/sql/resolver/native_routine_overload.h",
        "src/sql/engine/expr/ob_expr_udf.cpp",
        "src/pl/parser/pl_non_reserved_keywords_mysql_mode.c",
        "src/sql/engine/expr/plugin_function_expr.cpp",
        "src/sql/resolver/ddl/ob_create_routine_resolver.cpp",
        "src/sql/resolver/ddl/ob_alter_routine_resolver.cpp", "src/sql/resolver/ddl/native_routine_ddl.h",
        "src/sql/resolver/ddl/ob_drop_routine_resolver.cpp", "src/share/ob_rpc_struct.h", "src/share/ob_rpc_struct.cpp",
        "src/sql/resolver/ddl/native_function_default.h",
        "src/sql/resolver/expr/ob_raw_expr_util.cpp",
        "src/pl/parser/pl_parser_mysql_mode.y",
        "src/sql/parser/sql_parser_mysql_mode.y",
        "src/sql/resolver/expr/ob_raw_expr_resolver_impl.cpp",
        "src/sql/resolver/ddl/extension_routine_resolver.cpp",
        "src/sql/resolver/ddl/native_routine_dcl_request.h",
        "src/sql/privilege_check/ob_privilege_check.cpp",
        "src/share/schema/ob_schema_struct.h", "src/share/schema/ob_schema_struct.cpp",
        "src/sql/printer/ob_schema_printer.cpp",
        "src/query/api/query/parser/ob_item_type.h",
    ]
    if (not server.exists() or any(server.stat().st_mtime < (source / path).stat().st_mtime
                                   for path in critical_sources)):
        parser.error("complete cmake --build <build-dir> --target seekdb before testing: catalog/ABI sources changed")
    direct_link = args.catalog_only or args.writer_only
    if args.writer_only:
        from kernel_script import validate_sql_build_configuration
        validate_sql_build_configuration(build, entries)
    if not direct_link:
        environment = dict(os.environ, NEED_PARSER_CACHE="ON")
        subprocess.run(["bash", "gen_parser.sh"], cwd=source / "src/pl/parser",
                       env=environment, check=True)
    main_entry, = [e for e in entries if pathlib.Path(e["file"]) == source / "src/observer/main.cpp"]
    overlays = [e for e in entries if pathlib.Path(e["file"]).parent == source / "src/pl/parser"]
    if len(overlays) != 5:
        parser.error("expected all five standalone PL parser compilation entries")
    resolver_path = source / "src/sql/resolver/ddl/ob_create_routine_resolver.cpp"
    resolver_entries = [e for e in entries if pathlib.Path(e["file"]) == resolver_path]
    if not resolver_entries:
        # Rebuild the entire owning unity object, otherwise linking its old
        # neighbors would pull duplicate old routine-resolver definitions.
        resolver_entries = [e for e in entries if pathlib.Path(e["file"]).suffix == ".cxx"
                            and str(resolver_path) in pathlib.Path(e["file"]).read_text()]
    resolver, = resolver_entries
    with tempfile.TemporaryDirectory(prefix="seekdb-native-declaration-") as temporary:
        stage = pathlib.Path(temporary)

        def compile_object(item):
            entry, input_path, name = item
            command = shlex.split(entry["command"])
            obj = stage / name
            command[command.index("-c") + 1] = str(input_path)
            command[command.index("-o") + 1] = str(obj)
            subprocess.run(command, cwd=entry["directory"], check=True)
            return obj

        items = [] if direct_link else [
            (e, pathlib.Path(e["file"]), pathlib.Path(e["file"]).name + ".o")
            for e in overlays + [resolver]]
        test_source = (source / "rust/plugin-runtime/tests/native_routine_writer.cpp" if args.writer_only else
                       source / "rust/plugin-runtime/tests/native_routine_catalog.cpp" if args.catalog_only else
                       pathlib.Path(__file__).resolve().with_suffix(".cpp"))
        items.insert(0, (main_entry, test_source, "test-main.o"))
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as workers:
            objects = list(workers.map(compile_object, items))
        link = shlex.split((build / "src/observer/CMakeFiles/seekdb.dir/link.txt").read_text())
        main_index, = [i for i, value in enumerate(link) if value.endswith("/ob_main.dir/main.cpp.o")]
        binary = stage / "native-declaration"
        link[link.index("-o") + 1] = str(binary)
        link[main_index:main_index + 1] = [str(obj) for obj in objects]
        subprocess.run(link, cwd=build / "src/observer", check=True)
        try:
            subprocess.run([str(binary)], cwd=stage, check=True, timeout=60)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
            for log in sorted(stage.glob("native_routine_writer.log*")):
                print(f"--- {log.name} (tail) ---", flush=True)
                print(log.read_text(errors="replace")[-16000:], flush=True)
            raise


if __name__ == "__main__":
    main()
