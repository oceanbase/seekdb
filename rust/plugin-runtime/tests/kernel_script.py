#!/usr/bin/env python3
"""Opt-in Linux kernel-parser regression against an already-built CMake seekdb.

Uses that build's exact compiler/link flags, replaces ONLY the main object, and
installs the real top-level plugins component into a private fixture directory.
Also builds/audits/installs the Rust text DSO and runs real SQL-expression
evaluation through the loader. Does not start a server. Run after
`cmake --build <build> --target seekdb` finishes.
This is separate from the self-contained runtime CTests. Uses real kernel code,
plus explicitly scoped transport/Root/catalog-activation fixtures; does not prove
SQL execution, authentication, transaction visibility, or rollback in a live server.
"""
import argparse
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
import tempfile


def validate_sql_build_configuration(build, entries):
    """Check actual SQL objects/flags, not the timestamp of descriptive CMake text.

    A profile-list refactor can leave all compile/link actions unchanged. That
    is a valid no-op build; touching/relinking the executable would not add
    evidence. Conversely a newly selected object or changed compiler flags
    must have been built and linked before this runner uses the executable.
    """
    binary = build / "src/observer/seekdb"
    if not binary.is_file():
        raise ValueError("production executable is missing")
    linked_at = binary.stat().st_mtime_ns
    sql_objects = 0
    for entry in entries:
        command = shlex.split(entry["command"])
        if "-o" not in command:
            continue
        output = command[command.index("-o") + 1]
        if "CMakeFiles/ob_sql.dir/" not in output:
            continue
        sql_objects += 1
        if "-DSEEKDB_WITH_EXPERIMENTAL_PLUGINS=1" not in command:
            raise ValueError("SQL target is missing the experimental-plugin definition")
        directory = pathlib.Path(entry["directory"])
        obj = directory / output
        source = directory / entry["file"]
        flags = directory / "CMakeFiles/ob_sql.dir/flags.make"
        if not obj.is_file() or not source.is_file() or not flags.is_file():
            raise ValueError("SQL object/source/flags are missing; finish the production build")
        built_at = obj.stat().st_mtime_ns
        if built_at < max(source.stat().st_mtime_ns, flags.stat().st_mtime_ns) or built_at > linked_at:
            raise ValueError("SQL object or executable is stale; finish the production build")
    link = build / "src/observer/CMakeFiles/seekdb.dir/link.txt"
    if sql_objects == 0 or not link.is_file() or link.stat().st_mtime_ns > linked_at:
        raise ValueError("SQL compile/link configuration is missing or newer than the executable")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    parser.add_argument("--overlay-only", action="store_true",
                        help="test only the owned routine overlay, not guard integration or package installation")
    parser.add_argument("--debug-on-crash", action="store_true",
                        help="after a signal failure, rerun the private fixture under gdb for a backtrace; keep the failure result")
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("this build-flag reuse runner currently supports Linux only")
    build = args.build_dir.resolve()
    source = pathlib.Path(__file__).resolve().parents[3]
    entries = json.loads((build / "compile_commands.json").read_text())
    if not args.overlay_only:
        try:
            validate_sql_build_configuration(build, entries)
        except ValueError as error:
            parser.error(str(error))
        # Catch the known layout mismatch before linking a new test main against
        # old guard objects. This timestamp gate is not a substitute for a
        # completed, dependency-correct build or proof that no build is running.
        executable = build / "src/observer/seekdb"
        guard_inputs = [source / name for name in (
            "src/observer/virtual_table/ob_show_create_procedure.cpp",
            "src/observer/mysql/obmp_query.cpp",
            "rust/plugin-runtime/src/query_operation.rs",
            "rust/plugin-runtime/src/hook.rs",
            "rust/plugin-runtime/src/hook_v2.rs",
            "rust/plugin-runtime/src/build_contract.rs",
            "rust/plugin-runtime/src/input_state.rs",
            "include/seekdb/plugin/server_dev.h",
            "include/seekdb/plugin/server_dev_planner.h",
            "include/seekdb/plugin/server_dev_executor.h",
            "src/sql/optimizer/log_plugin_custom.h",
            "src/sql/optimizer/log_plugin_custom.cpp",
            "src/sql/optimizer/plugin_path.h",
            "src/sql/optimizer/ob_join_order.h",
            "src/sql/optimizer/ob_join_order.cpp",
            "src/sql/optimizer/ob_log_join.cpp",
            "src/sql/optimizer/plugin_candidate_graph.h",
            "src/sql/optimizer/ob_log_operator_factory.h",
            "src/sql/optimizer/ob_log_operator_factory.cpp",
            "src/sql/engine/basic/plugin_custom_op.h",
            "src/sql/engine/basic/plugin_custom_op.cpp",
            "src/sql/engine/ob_operator_factory.cpp",
            "src/query/api/query/engine/ob_phy_operator_type.h",
            "src/share/plugin/custom_executor.h",
            "src/sql/optimizer/ob_log_plan.cpp",
            "src/sql/optimizer/ob_select_log_plan.cpp",
            "src/sql/optimizer/ob_log_plan.h",
            "src/sql/optimizer/ob_optimizer.h",
            "src/sql/optimizer/ob_optimizer.cpp",
            "src/share/plugin/catalog_builder.h",
            "src/sql/resolver/ddl/catalog_routine_lookup.h",
            "include/seekdb/plugin/catalog_spi.h",
            "include/seekdb/plugin/sql_spi.h",
            "include/seekdb/plugin/sql_catalog.h",
            "src/share/plugin/ob_plugin_registry.h",
            "src/share/plugin/ob_plugin_registry.cpp",
            "src/share/rc/ob_module_provider.h",
            "src/share/plugin/ob_plugin_loader.h",
            "src/share/plugin/ob_plugin_loader.cpp",
            "src/observer/ob_server.h",
            "src/observer/ob_server.cpp",
            "src/observer/ob_server_plugin_runtime.h",
            "src/observer/ob_server_plugin_runtime.cpp",
            "src/observer/virtual_table/plugin_memory_table.h",
            "src/observer/virtual_table/plugin_memory_table.cpp",
            "src/observer/virtual_table/ob_virtual_table_iterator_factory.cpp",
            "src/share/inner_table/ob_inner_table_schema_def.py",
            "src/sql/resolver/expr/ob_raw_expr.h",
            "src/sql/resolver/expr/ob_raw_expr.cpp",
            "src/sql/resolver/expr/plugin_expr_type.h",
            "src/sql/resolver/expr/ob_raw_expr_util.cpp",
            "src/sql/resolver/expr/ob_raw_expr_deduce_type.cpp",
            "src/sql/resolver/expr/ob_raw_expr_resolver_impl.cpp",
            "src/sql/resolver/expr/ob_raw_expr_info_extractor.cpp",
            "src/sql/resolver/dml/ob_dml_resolver.cpp",
            "src/sql/resolver/dml/ob_dml_stmt.cpp",
            "src/sql/resolver/ob_resolver_utils.cpp",
            "src/sql/engine/expr/plugin_function_expr.h",
            "src/sql/engine/expr/plugin_function_expr.cpp",
            "src/sql/engine/basic/ob_function_table_op.h",
            "src/sql/engine/basic/ob_function_table_op.cpp",
            "src/sql/engine/aggregate/ob_aggregate_processor.h",
            "src/sql/engine/aggregate/ob_aggregate_processor.cpp",
            "src/sql/engine/aggregate/ob_scalar_aggregate_op.cpp",
            "src/sql/engine/sort/ob_sort_op_impl.h",
            "src/sql/engine/sort/ob_sort_op_impl.cpp",
            "src/sql/engine/sort/ob_sort_basic_info.h",
            "src/sql/engine/sort/ob_sort_basic_info.cpp",
            "src/sql/engine/px/exchange/ob_row_heap.h",
            "src/sql/engine/px/exchange/ob_row_heap.cpp",
            "src/sql/engine/px/exchange/ob_px_ms_receive_op.cpp",
            "src/sql/engine/px/exchange/ob_px_receive_op.h",
            "src/sql/engine/px/exchange/ob_px_receive_op.cpp",
            "src/query/api/query/engine/ob_operator.h",
            "src/sql/engine/ob_operator.cpp",
            "src/sql/engine/px/exchange/ob_px_ms_coord_op.cpp",
            "src/sql/engine/px/exchange/ob_px_transmit_op.h",
            "src/sql/engine/px/exchange/ob_px_transmit_op.cpp",
            "src/sql/engine/px/ob_slice_calc.h",
            "src/sql/engine/px/ob_slice_calc.cpp",
            "src/sql/engine/px/ob_px_row_store.h",
            "src/sql/engine/px/ob_px_row_store.cpp",
            "src/sql/dtl/ob_dtl_basic_channel.h",
            "src/sql/dtl/ob_dtl_basic_channel.cpp",
            "src/sql/dtl/ob_dtl_channel.h",
            "src/sql/dtl/ob_dtl_channel.cpp",
            "src/sql/dtl/ob_dtl.h",
            "src/sql/dtl/ob_dtl.cpp",
            "src/sql/dtl/ob_dtl_channel_group.h",
            "src/sql/dtl/ob_dtl_channel_group.cpp",
            "src/sql/dtl/ob_dtl_utils.h",
            "src/sql/dtl/ob_dtl_utils.cpp",
            "src/sql/dtl/ob_dtl_interm_result_manager.h",
            "src/sql/dtl/ob_dtl_interm_result_manager.cpp",
            "src/sql/dtl/ob_dtl_local_channel.h",
            "src/sql/dtl/ob_dtl_local_channel.cpp",
            "src/sql/dtl/ob_dtl_channel_loop.h",
            "src/sql/dtl/ob_dtl_channel_loop.cpp",
            "src/sql/dtl/ob_dtl_fc_server.h",
            "src/sql/dtl/ob_dtl_fc_server.cpp",
            "src/sql/dtl/ob_dtl_flow_control.h",
            "src/sql/dtl/ob_dtl_flow_control.cpp",
            "src/sql/engine/px/ob_px_dtl_proc.h",
            "src/sql/engine/px/ob_px_dtl_proc.cpp",
            "src/sql/dtl/ob_dtl_linked_buffer.h",
            "src/sql/dtl/ob_dtl_linked_buffer.cpp",
            "src/sql/dtl/ob_dtl_processor.h",
            "src/sql/dtl/ob_dtl_mem_manager.h",
            "src/sql/dtl/ob_dtl_mem_manager.cpp",
            "src/sql/engine/px/ob_px_sqc_handler.h",
            "src/sql/engine/px/ob_px_sqc_handler.cpp",
            "src/query/api/query/engine/px/ob_px_tablet_range.h",
            "src/sql/engine/px/datahub/components/ob_dh_sample.h",
            "src/sql/engine/px/datahub/components/ob_dh_sample.cpp",
            "src/sql/engine/expr/ob_expr_calc_partition_id.h",
            "src/sql/engine/expr/ob_expr_calc_partition_id.cpp",
            "src/sql/das/ob_das_tablet_mapper.h",
            "src/sql/das/ob_das_tablet_mapper.cpp",
            "src/sql/engine/sort/ob_sort_op.h",
            "src/sql/engine/sort/ob_sort_op.cpp",
            "src/sql/optimizer/ob_optimizer_util.cpp",
            "src/sql/optimizer/ob_log_sort.cpp",
            "src/query/api/query/engine/ob_operator_reg.h",
            "src/sql/code_generator/ob_static_engine_cg.cpp",
            "src/sql/engine/expr/plugin_sql_context.h",
            "src/sql/engine/expr/plugin_sql_context.cpp",
            "src/sql/engine/expr/ob_expr_extra_info_factory.cpp",
            "src/sql/parser/sql_parser_mysql_mode.y",
            "src/query/api/query/parser/ob_item_type.h",
            "src/objit/include/objit/common/ob_item_type.h",
            "src/share/statement/ob_stmt_type.h",
            "src/sql/resolver/cmd/alter_extension_stmt.h",
            "src/sql/resolver/cmd/alter_extension_resolver.h",
            "src/sql/resolver/cmd/alter_extension_resolver.cpp",
            "src/sql/engine/cmd/alter_extension_executor.h",
            "src/sql/engine/cmd/alter_extension_executor.cpp",
            "src/share/schema/ob_schema_getter_guard.h",
            "src/share/schema/ob_schema_getter_guard.cpp",
            "src/share/schema/ob_schema_getter_guard_priv.cpp",
            "src/share/schema/routine_privilege_overlay.h",
            "src/share/schema/routine_catalog_savepoint.h",
            "src/share/schema/routine_catalog_transaction.h",
            "src/share/schema/routine_catalog_transaction.cpp",
            "src/rootserver/pl_ddl/routine_catalog_writer.h",
            "src/rootserver/pl_ddl/routine_cache_invalidation.h",
            "src/share/schema/ob_multi_version_schema_service.h",
            "src/share/schema/ob_multi_version_schema_service.cpp",
            "src/sql/session/ob_sql_session_info.h",
            "src/sql/session/ob_sql_session_info.cpp",
            "src/storage/tx/ob_trans_define_v4.h",
            "src/sql/session/ob_basic_session_info.cpp",
            "src/share/schema/borrowed_sql_transaction.h",
            "src/share/schema/catalog_operation_recorder.h",
            "src/rootserver/catalog_commit_preparation.h",
            "src/rootserver/ob_ddl_service.h",
            "src/rootserver/ob_ddl_service.cpp",
            "src/share/schema/ob_ddl_sql_service.cpp",
            "src/sql/engine/expr/caller_catalog_transaction.h",
            "src/sql/engine/expr/caller_catalog_transaction.cpp",
            "src/data_plane/api/data_plane/transaction/ob_tx_desc_access.h",
            "src/sql/ob_end_trans_callback.h", "src/sql/ob_end_trans_callback.cpp",
            "src/sql/ob_mysql_end_trans_cb.h", "src/observer/mysql/ob_mysql_end_trans_cb.cpp",
            "src/share/schema/routine_schema_overlay.h",
            "src/pl/pl_cache/ob_pl_cache_mgr.cpp",
            "src/pl/ob_pl_build.cpp", "src/pl/ob_pl.cpp",
            "src/pl/ob_pl_router.cpp", "src/sql/pl/ob_pl_router.h",
            "src/sql/resolver/ob_resolver_define.h",
            "src/sql/resolver/ddl/ob_create_routine_resolver.cpp",
            "src/sql/ob_sql.cpp", "src/sql/plan_cache/ob_plan_cache.cpp",
            "src/sql/plan_cache/ob_plan_cache.h",
            "src/sql/ob_spi.h", "src/sql/ob_spi.cpp",
            "src/rootserver/pl_ddl/routine_id_reservation.h",
            "src/share/plugin/extension_routine_update.h",
            "src/query/api/query/command/ob_root_command_service.h",
            "src/query/api/query/command/ob_root_service_serialization.h",
            "src/rootserver/ob_local_management_service.h",
            "src/rootserver/ob_local_management_service.cpp",
            "src/rootserver/pl_ddl/routine_version_reservation.h",
            "src/rootserver/pl_ddl/ob_pl_ddl_operator.h",
            "src/rootserver/pl_ddl/ob_pl_ddl_operator.cpp",
            "src/rootserver/pl_ddl/ob_pl_ddl_service.h",
            "src/rootserver/pl_ddl/ob_pl_ddl_service.cpp",
            "src/sql/engine/ob_exec_context.h",
            "src/sql/engine/ob_exec_context.cpp",
            "src/oblib/lib/oblog/ob_warning_buffer.h",
            "src/oblib/lib/oblog/ob_warning_buffer.cpp",
            "src/sql/resolver/ddl/extension_statement_diagnostics.h",
            "src/sql/resolver/ddl/extension_routine_resolver.h",
            "src/sql/resolver/ddl/extension_routine_batch.h",
            "src/sql/resolver/ddl/extension_routine_batch.cpp",
            "src/sql/resolver/ddl/extension_routine_resolver.cpp")]
        guard_inputs.extend(source / name for name in (
            "src/share/plugin/extension_package.h", "src/share/plugin/extension_package.cpp",
            "src/sql/resolver/ddl/extension_script.h", "src/sql/resolver/ddl/extension_script.cpp",
            "rust/plugin-runtime/include/plugin_runtime.h", "rust/plugin-runtime/src/package/source.rs"))
        guard_inputs.extend(source / name for name in (
            "rust/plugin-runtime/src/query_transaction.rs", "rust/plugin-runtime/src/memory.rs",
            "rust/plugin-runtime/src/memory_limit.rs",
            "src/observer/ob_command_line_parser.cpp", "src/observer/ob_server_options.h",
            "include/seekdb/plugin/memory_spi.h",
            "rust/plugin-runtime/src/query_transaction/invalidation_queue.rs",
            "src/data_plane/api/data_plane/transaction/ob_i_transaction_service.h",
            "src/storage/tx/ob_tx_api.h", "src/storage/tx/ob_tx_api.cpp"))
        guard_inputs.extend(source / name for name in (
            "rust/plugin-runtime/src/package.rs", "rust/plugin-runtime/src/package/versions.rs"))
        if not executable.is_file() or any(
                path.stat().st_mtime_ns > executable.stat().st_mtime_ns for path in guard_inputs):
            parser.error("production executable predates routine guard/PL/SQL inputs or is missing; "
                         "finish a full seekdb rebuild before running guard integration")
    entry, = [item for item in entries if pathlib.Path(item["file"]) == source / "src/observer/main.cpp"]
    compile_command = shlex.split(entry["command"])
    link_command = shlex.split((build / "src/observer/CMakeFiles/seekdb.dir/link.txt").read_text())
    main_indices = [i for i, arg in enumerate(link_command) if arg.endswith("/ob_main.dir/main.cpp.o")]
    if len(main_indices) != 1:
        parser.error("expected exactly one production main object in seekdb link command")
    with tempfile.TemporaryDirectory(prefix="seekdb-kernel-script-") as temporary:
        stage = pathlib.Path(temporary)
        if not args.overlay_only:
            # Compile the actual CLI with plugins disabled as well. Do not let
            # an unconditional Rust parser reference break the lightweight build.
            # This checks this object's boundary, not an entire disabled server.
            cli_entry, = [item for item in entries if pathlib.Path(item["file"]) ==
                          source / "src/observer/ob_command_line_parser.cpp"]
            disabled_cli = shlex.split(cli_entry["command"])
            if "-DSEEKDB_WITH_EXPERIMENTAL_PLUGINS=1" not in disabled_cli:
                parser.error("ob_main is missing the experimental-plugin definition; rebuild seekdb")
            # --help exits inside parsing, before directories/listeners/startup.
            # Exercise the actual linked main+parser before expensive fixtures.
            subprocess.run([str(executable), "--plugin-memory-limit=64MiB",
                            "--plugin-allocation-limit=4096", "--help"],
                           cwd=stage, check=True, timeout=30)
            disabled_obj = stage / "disabled-command-line.o"
            disabled_cli[disabled_cli.index("-o") + 1] = str(disabled_obj)
            disabled_cli.append("-USEEKDB_WITH_EXPERIMENTAL_PLUGINS")
            subprocess.run(disabled_cli, cwd=cli_entry["directory"], check=True)
            symbols = subprocess.check_output(["nm", "--undefined-only", str(disabled_obj)], text=True)
            if "seekdb_runtime_memory_parse_limit" in symbols:
                raise RuntimeError("disabled command-line parser depends on Rust plugin limit parsing")
            print("disabled CLI object compiled without a Rust memory parser dependency", flush=True)
            disabled_table = shlex.split(cli_entry["command"])
            disabled_table_obj = stage / "disabled-plugin-memory-table.o"
            disabled_table[disabled_table.index("-o") + 1] = str(disabled_table_obj)
            disabled_table[disabled_table.index("-c") + 1] = str(
                source / "src/observer/virtual_table/plugin_memory_table.cpp")
            disabled_table.append("-USEEKDB_WITH_EXPERIMENTAL_PLUGINS")
            subprocess.run(disabled_table, cwd=cli_entry["directory"], check=True)
            symbols = subprocess.check_output(["nm", "--undefined-only", str(disabled_table_obj)], text=True)
            if any(name in symbols for name in ("seekdb_runtime_", "ObPluginLoader", "ObServerPluginRuntime")):
                raise RuntimeError("disabled plugin memory table depends on plugin runtime symbols")
            print("disabled memory table compiled without a plugin runtime dependency", flush=True)
            # Build/audit the actual plugin, then install a private immutable
            # copy. Never load a cached DSO based only on its file's existence.
            subprocess.run(["cmake", "--build", str(build), "--target", "seekdb_rust_text_plugin", "-j2"], check=True)
        obj = stage / "kernel_script.o"
        binary = stage / "kernel_script"
        compile_command[compile_command.index("-o") + 1] = str(obj)
        test_source = (pathlib.Path(__file__).with_name("routine_overlay.cpp") if args.overlay_only
                       else pathlib.Path(__file__).with_suffix(".cpp"))
        compile_command[compile_command.index("-c") + 1] = str(test_source.resolve())
        subprocess.run(compile_command, cwd=entry["directory"], check=True)
        link_command[link_command.index("-o") + 1] = str(binary)
        link_command[main_indices[0]] = str(obj)
        subprocess.run(link_command, cwd=build / "src/observer", check=True)
        if args.overlay_only:
            subprocess.run([str(binary)], cwd=stage, check=True, timeout=30)
            print("owned routine schema overlay regression passed (no guard/server integration)")
            return
        installed = stage / "installed"
        # Respect non-default data directories from the actual CMake cache.
        cache = (build / "CMakeCache.txt").read_text().splitlines()
        data_dir, = [line.split("=", 1)[1] for line in cache if line.startswith("CMAKE_INSTALL_DATADIR:PATH=")]
        # GNUInstallDirs leaves this cache entry empty when it inherits
        # DATAROOTDIR; the generated install rule uses the effective value.
        if not data_dir:
            data_dir, = [line.split("=", 1)[1] for line in cache if line.startswith("CMAKE_INSTALL_DATAROOTDIR:PATH=")]
        if pathlib.Path(data_dir).is_absolute() or ".." in pathlib.Path(data_dir).parts:
            parser.error("kernel fixture installation requires a relative CMAKE_INSTALL_DATADIR")
        install_env = os.environ.copy()
        install_env.pop("DESTDIR", None)
        include_dir, = [line.split("=", 1)[1] for line in cache if line.startswith("CMAKE_INSTALL_INCLUDEDIR:PATH=")]
        lib_dir, = [line.split("=", 1)[1] for line in cache if line.startswith("CMAKE_INSTALL_LIBDIR:PATH=")]
        for directory in (include_dir, lib_dir):
            if not directory or pathlib.Path(directory).is_absolute() or ".." in pathlib.Path(directory).parts:
                parser.error("kernel SDK installation requires relative include/lib directories")
        subprocess.run(["cmake", "--install", str(build), "--prefix", str(installed),
                        "--component", "plugin-sdk"], check=True, env=install_env)
        subprocess.run([compile_command[0], "-x", "c", "-std=c11", "-Werror", "-fsyntax-only",
                        "-I", str(installed / include_dir),
                        str(pathlib.Path(__file__).with_name("memory_sdk_install.c"))], check=True)
        subprocess.run(["cmake", "--install", str(build), "--prefix", str(installed),
                        "--component", "plugins"], check=True, env=install_env)
        # Native plugin subdirectories are EXCLUDE_FROM_ALL, so explicitly
        # exercise their generated installation rules as well.
        subprocess.run(["cmake", "--install", str(build / "plugins/rust_text"), "--prefix", str(installed),
                        "--component", "plugins"], check=True, env=install_env)
        rust_artifacts = list(installed.rglob("libseekdb_rust_text.so"))
        if len(rust_artifacts) != 1:
            raise RuntimeError("expected exactly one installed Rust text DSO")
        installed_packages = installed / data_dir / "seekdb/extension"
        built_package = installed_packages / "rust_text_built"
        if not built_package.is_dir() or {p.name for p in built_package.iterdir()} != {"rust_text_built.control"}:
            raise RuntimeError("transaction-built example must install control only, without placeholder SQL")
        generated_composed = stage / "generated composed source"
        subprocess.run(["cargo", "run", "--offline", "--manifest-path",
                        str(source / "rust/cargo-seekdb/Cargo.toml"), "--", "schema",
                        "--manifest-path", str(source / "plugins/rust_text/Cargo.toml"),
                        "--example", "seekdb_composed_schema", "--output", str(generated_composed)],
                       cwd=source / "rust", check=True)
        composed_files = {"text_composed.control", "text_composed--1.0.sql", "text_composed--1.0--1.1.sql"}
        if {p.name for p in generated_composed.iterdir()} != composed_files:
            raise RuntimeError("composed schema generator emitted unexpected files or incomplete output")
        for name in ("text_composed.control", "text_composed--1.0.sql", "text_composed--1.0--1.1.sql"):
            artifact = installed_packages / "text_composed" / name
            if not artifact.is_file() or artifact.read_bytes() != (source / "plugins/sql_packages/text_composed" / name).read_bytes():
                raise RuntimeError(f"composed package missing or different from source: {name}")
            if (generated_composed / name).read_bytes() != artifact.read_bytes():
                raise RuntimeError(f"generated composed package differs from installed artifact: {name}")
        native_package = installed_packages / "rust_text_native"
        if not (native_package / "rust_text_native.control").is_file() or not (native_package / "rust_text_native--1.0--1.1.sql").is_file():
            raise RuntimeError("native-source package was not installed")
        if (native_package / "rust_text_native--1.0.sql").exists():
            raise RuntimeError("native-source regression must not use a placeholder base SQL file")
        generated_native = stage / "generated native source"
        subprocess.run(["cargo", "run", "--offline", "--manifest-path",
                        str(source / "rust/cargo-seekdb/Cargo.toml"), "--", "schema",
                        "--manifest-path", str(source / "plugins/rust_text/Cargo.toml"),
                        "--example", "seekdb_native_schema", "--output", str(generated_native)],
                       cwd=source / "rust", check=True)
        native_files = {"rust_text_native.control", "rust_text_native--1.0--1.1.sql"}
        if {p.name for p in generated_native.iterdir()} != native_files:
            raise RuntimeError("native schema generator emitted unexpected files or incomplete output")
        for name in native_files:
            if (generated_native / name).read_bytes() != (native_package / name).read_bytes():
                raise RuntimeError(f"generated native package differs from installed artifact: {name}")
        generated_package = stage / "generated rust_text_ops"
        subprocess.run(["cargo", "run", "--offline", "--manifest-path",
                        str(source / "rust/cargo-seekdb/Cargo.toml"), "--", "schema",
                        "--manifest-path", str(source / "plugins/rust_text/Cargo.toml"),
                        "--output", str(generated_package)], cwd=source / "rust", check=True)
        for name in ("rust_text_ops.control", "rust_text_ops--1.0.sql", "rust_text_ops--1.0--1.1.sql"):
            if not (installed_packages / "rust_text_ops" / name).is_file():
                raise RuntimeError(f"top-level plugins component did not install {name}")
            if (generated_package / name).read_bytes() != (installed_packages / "rust_text_ops" / name).read_bytes():
                raise RuntimeError(f"generated SQL package differs from kernel-tested installed artifact: {name}")
        for name in ("text_ops.control", "text_ops--1.0.sql", "text_ops--1.0--1.1.sql"):
            if not (installed_packages / "text_ops" / name).is_file():
                raise RuntimeError(f"top-level plugins component did not install {name}")
        packages = stage / "packages"
        packages.mkdir()
        # The old installed version is supplied by the controlled catalog. Its
        # replacement package is actually generated by the public Rust SDK and
        # inspected by the CLI/runtime reader, not handwritten test metadata.
        dependency_update = packages / "consumer"
        subprocess.run(["cargo", "run", "--offline", "--manifest-path",
                        str(source / "rust/cargo-seekdb/Cargo.toml"), "--", "schema",
                        "--manifest-path", str(source / "plugins/rust_text/Cargo.toml"),
                        "--example", "seekdb_versioned_schema", "--output", str(dependency_update)],
                       cwd=source / "rust", check=True)
        versioned_files = {"consumer.control", "consumer--1.0.sql", "consumer--1.0--middle.sql",
                           "consumer--middle--1.1.sql", "consumer--middle.control", "consumer--1.1.control"}
        if {p.name for p in dependency_update.iterdir()} != versioned_files:
            raise RuntimeError("versioned generator emitted unexpected files or incomplete output")

        def package(name, sql, control="default_version = '1'\n"):
            directory = packages / name
            directory.mkdir()
            (directory / f"{name}.control").write_text(control, encoding="utf-8")
            (directory / f"{name}--1.sql").write_text(sql, encoding="utf-8")

        package("routines", """-- a function body is not split on its inner semicolons
CREATE FUNCTION ext_value() RETURNS INT DETERMINISTIC NO SQL
BEGIN
  DECLARE n INT DEFAULT 1;
  RETURN n + CHAR_LENGTH('a;b');
END;
/* also preserve a second routine */
CREATE PROCEDURE ext_proc() BEGIN SELECT 'x;y'; SELECT 2; END;
""")
        package("bad_tail", "CREATE FUNCTION ext_ok() RETURNS INT RETURN 1;\nCREATE FUNCTION broken(")
        package("directives", "DELIMITER $$\nCREATE FUNCTION ext_ok() RETURNS INT RETURN 1$$")
        package("comments", "-- comments only\n/* no executable SQL */\n")
        package("many", "SELECT 1;\n" * 4096)
        package("too_many", "SELECT 1;\n" * 4097)
        package("general", "CREATE TABLE ext_table (v INT); SELECT 'a;b';")
        package("ignore_conflict", "CREATE FUNCTION IF NOT EXISTS ext_fn() RETURNS INT RETURN 1;")
        package("native", "CREATE FUNCTION ext_fn() RETURNS INT RETURN 1;",
                "default_version = '1'\nnative_module = 'seekdb.native'\n")
        package("requires", "CREATE FUNCTION ext_fn() RETURNS INT RETURN 1;",
                "default_version = '1'\nrequires = 'other'\n")
        package("chain", "CREATE FUNCTION ext_base() RETURNS INT RETURN 1; -- no newline")
        package("create_chain", "CREATE FUNCTION create_base(v INT) RETURNS INT DETERMINISTIC NO SQL RETURN v + 1;\n"
                "CREATE FUNCTION create_caller() RETURNS INT DETERMINISTIC NO SQL RETURN create_base(40) + 1;\n")
        package("create_warning", "CREATE FUNCTION warning_only(arg ENUM('x','x')) RETURNS INT DETERMINISTIC NO SQL RETURN 1;\n")
        package("create_wrong_arguments", "CREATE FUNCTION bad_arguments() RETURNS INT RETURN create_base();\n")
        package("create_missing", "SELECT 'base must not run';")
        (packages / "create_missing/create_missing--1--2.sql").write_text(
            "CREATE FUNCTION missing_reference() RETURNS INT RETURN absent_callee();\n"
            "CREATE FUNCTION must_not_run() RETURNS INT RETURN 1;\n", encoding="utf-8")
        (packages / "chain/chain--1--2.sql").write_text(
            "CREATE FUNCTION ext_added() RETURNS INT RETURN 2;", encoding="utf-8")
        package("chain_bad", "CREATE FUNCTION ext_base() RETURNS INT RETURN 1;")
        (packages / "chain_bad/chain_bad--1--2.sql").write_text(
            "CREATE FUNCTION broken(", encoding="utf-8")
        package("chain_tokens", "SELECT 'unfinished")
        (packages / "chain_tokens/chain_tokens--1--2.sql").write_text("'; SELECT 2;", encoding="utf-8")
        package("chain_many", "SELECT 1;\n" * 2048)
        (packages / "chain_many/chain_many--1--2.sql").write_text("SELECT 2;\n" * 2048, encoding="utf-8")
        (packages / "chain_many/chain_many--2--3.sql").write_text("SELECT 3;", encoding="utf-8")
        package("update_general", "SELECT 'base must not be replayed';")
        package("sequence_state", "SELECT 'not an installation test';")
        package("sequence_fixed_empty", "SELECT 'unused';", "default_version = '1'\nschema = 'other_db'\n")
        (packages / "sequence_fixed_empty/sequence_fixed_empty--1--2.sql").write_text("", encoding="utf-8")
        for name, tail in (("sequence_driver", "ALTER FUNCTION ext_value COMMENT 'after drop';\n"),
                           ("sequence_success", "")):
            package(name, "SELECT 'base must not run';")
            (packages / f"{name}/{name}--1--2.sql").write_text(
                "ALTER FUNCTION ext_value COMMENT 'sequence first';\n"
                "ALTER FUNCTION ext_value SQL SECURITY INVOKER;\n"
                "DROP FUNCTION ext_value;\n" + tail, encoding="utf-8")
        (packages / "sequence_state/sequence_state--1--2.sql").write_text(
            "ALTER FUNCTION ext_value COMMENT 'step one';\n"
            "ALTER FUNCTION ext_value SQL SECURITY INVOKER;\n"
            "DROP FUNCTION ext_value;\n"
            "DROP PROCEDURE IF EXISTS ext_proc;\n"
            "ALTER FUNCTION ext_value COMMENT 'after delete';\n"
            "CREATE FUNCTION IF NOT EXISTS ext_new() RETURNS INT RETURN 1;\n"
            "CREATE FUNCTION ext_new() RETURNS INT RETURN 1;\n"
            "DROP FUNCTION other_db.ext_value;\n", encoding="utf-8")
        (packages / "update_general/update_general--1--2.sql").write_text(
            "DROP FUNCTION ext_base; CREATE FUNCTION ext_new() RETURNS INT RETURN 2;", encoding="utf-8")
        for name in ("update_empty", "update_bad", "update_tokens", "update_many"):
            package(name, "SELECT 'unused base';")
        for source_version, target_version, sql in (("1", "2", ""), ("2", "3", " \n\t"),
                                                    ("3", "4", "-- comment only\n/* no object changes */")):
            (packages / f"update_empty/update_empty--{source_version}--{target_version}.sql").write_text(sql, encoding="utf-8")
        (packages / "update_bad/update_bad--1--2.sql").write_text("SELECT 1;", encoding="utf-8")
        (packages / "update_bad/update_bad--2--3.sql").write_text("CREATE FUNCTION broken(", encoding="utf-8")
        (packages / "update_tokens/update_tokens--1--2.sql").write_text("SELECT 'unfinished", encoding="utf-8")
        (packages / "update_tokens/update_tokens--2--3.sql").write_text("'; SELECT 2;", encoding="utf-8")
        (packages / "update_many/update_many--1--2.sql").write_text("SELECT 1;\n" * 2048, encoding="utf-8")
        (packages / "update_many/update_many--2--3.sql").write_text("SELECT 2;\n" * 2048, encoding="utf-8")
        (packages / "update_many/update_many--3--4.sql").write_text("SELECT 3;", encoding="utf-8")
        from candidate_plugin import build as build_candidate_plugin
        candidate_stage = stage / "candidate-plugin"
        build_candidate_plugin(binary, candidate_stage, variants=[0, 9])
        from rust_server_dev import build_for_host
        cmake_python = next((line.split("=", 1)[1] for line in cache
                             if line.startswith(("Python3_EXECUTABLE:", "_Python3_EXECUTABLE:"))), None)
        native_candidate = build_for_host(binary, stage / "rust-candidate", python=cmake_python)
        shutil.copyfile(native_candidate, candidate_stage / "candidate_native.so")
        try:
            subprocess.run([str(binary), str(packages), str(installed_packages), str(rust_artifacts[0]),
                            str(candidate_stage / "candidate_0.so")],
                           cwd=stage, check=True, timeout=30)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            # Preserve useful diagnostics before the private fixture is removed.
            for log in sorted(stage.glob("kernel_script.log*")):
                print(f"--- {log.name} (tail) ---", file=sys.stderr)
                print(log.read_text(errors="replace")[-16000:], file=sys.stderr)
            if args.debug_on_crash and isinstance(error, subprocess.CalledProcessError) and error.returncode < 0:
                try:
                    subprocess.run(["gdb", "--batch", "-ex", "set pagination off", "-ex", "run", "-ex", "bt",
                                    "--args", str(binary), str(packages), str(installed_packages), str(rust_artifacts[0]),
                                    str(candidate_stage / "candidate_0.so")],
                                   cwd=stage, check=False, timeout=60)
                except (OSError, subprocess.TimeoutExpired) as diagnostic_error:
                    print(f"debugger unavailable: {diagnostic_error}", file=sys.stderr)
            raise
    print("real Rust DSO SQL expression execution, plugin binding/evaluation and projection type propagation, routine guard overlay, Extension parsing, update identity planning, command classification, "
          "privilege extraction and admission passed")


if __name__ == "__main__":
    main()
