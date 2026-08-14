"""Fine-grained semantic owners for Share's externally reachable headers.

The package-private implementation header aggregate in BUILD.bazel is
deliberately not an owner.
Every header in SHARE_PUBLIC_HEADER_ROOTS or SHARE_INTERFACE_CLOSURE_HEADERS
must instead be exported by exactly one target listed below.
"""

load(
    "//bazel:defs.bzl",
    cc_library = "seekdb_cc_library",
)
load(
    ":share_header_inventory.bzl",
    "SHARE_INTERFACE_CLOSURE_HEADERS",
    "SHARE_PUBLIC_HEADER_ROOTS",
)

_PUBLIC_VISIBILITY = [
    "//src:__subpackages__",
    "//unittest:__subpackages__",
]

# Targets declared directly in BUILD.bazel because they also own implementation
# sources or predate this registry.  The validation below reads their real hdrs
# attributes; this list does not duplicate their header declarations.
_EXISTING_SEMANTIC_API_TARGETS = [
    "ai_endpoint_interface",
    "cache",
    "cluster_version_interface",
    "config",
    "config_parser_interface",
    "config_value_support",
    "core_types",
    "datum",
    "debug_sync_interface",
    "delegate_interface",
    "device_manager_interface",
    "error_codes",
    "errsim_interface",
    "est_row_count_record",
    "geo_cast_interface",
    "geo_types",
    "id_generator",
    "io",
    "io_device_interface",
    "json_access",
    "lib_cache_namespace",
    "lob_diff_interface",
    "lob_read_service",
    "lob_runtime",
    "lob_read_context",
    "location",
    "log_protocol",
    "ls_restore_status",
    "object_cast",
    "object_cast_runtime_interface",
    "pl_integer_type",
    "resource_limit_calculator",
    "resource_limit_interface",
    "roaringbitmap",
    "schema_foundation_types",
    "schema_model_interface",
    "schema_partition_iteration",
    "scn_interface",
    "semistruct",
    "sequence_option",
    "sqlite_storage_interface",
    "srs_provider_interface",
    "statement_type",
    "storage_cache_policy",
    "system_variable_metadata",
    "table_load_protocol",
    "table_lock_priority",
    "tablet_autoincrement_admin_interface",
    "tablet_autoincrement_interface",
    "tablet_autoincrement_types",
    "tablet_replica_types",
    "task_control",
    "tenant_module_init",
    "tenant_runtime",
    "text_analysis",
    "thread_pool_interface",
    "time_wheel",
    "transaction_id",
    "ttl_schedule_interface",
    "unit_config_interface",
    "vector",
]

_SHARE_SEMANTIC_HEADER_TARGETS = {
    "seekdb_runtime_foundation": struct(
        hdrs = [
            "ob_compatibility_control.h",
            "ob_server_info.h",
            "ob_server_switchover_status.h",
            "ob_telemetry.h",
            "ob_timezone_mgr.h",
            "schema/ob_schema_runtime_service.h",
            "storage/ob_tablet_local_checksum_table_storage.h",
        ],
        deps = [
            ":cluster_topology",
            ":config",
            ":core_types",
            ":scn_interface",
            ":sqlite_storage_interface",
            ":tablet_replica_types",
            ":tenant_runtime",
            ":timezone_runtime",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "server_snapshot_id": struct(
        hdrs = ["server_snapshot/ob_server_snapshot_id.h"],
        deps = ["//src/oblib:oblib_foundation"],
    ),
    "admin_job_storage_interface": struct(
        hdrs = ["storage/ob_admin_job_table_storage.h"],
        deps = [
            ":sqlite_storage_interface",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "server_resource_types": struct(
        hdrs = [
            "resource/ob_server_resource.h",
            "resource/ob_server_resource_config.h",
            "resource/ob_server_runtime_config.h",
        ],
        deps = [
            "//src/oblib:oblib_foundation",
        ],
    ),
    "debug_sync_broadcaster_interface": struct(
        hdrs = ["ob_i_debug_sync_broadcaster.h"],
        deps = [],
    ),
    "autoincrement_runtime": struct(
        hdrs = [
            "ob_autoincrement_param.h",
            "ob_autoincrement_service.h",
        ],
        deps = [
            ":core_types",
            ":rpc_message_types",
            ":schema_access",
            ":schema_model_interface",
            ":sequence_option",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "max_id_allocation": struct(
        hdrs = [
            "ob_i_max_id_cache.h",
            "ob_max_id_cache.h",
            "ob_max_id_fetcher.h",
        ],
        deps = [
            ":core_types",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "catalog_access": struct(
        hdrs = [
        ],
        deps = [
            ":schema_access",
            ":schema_entities",
            ":schema_model_interface",
            ":schema_registry",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "checksum_persistence": struct(
        hdrs = [
            "ob_column_checksum_error_operator.h",
            "ob_tablet_checksum_operator.h",
            "ob_tablet_local_checksum_operator.h",
            "ob_tablet_meta_table_compaction_operator.h",
            "storage/ob_column_checksum_error_info_table_storage.h",
            "storage/ob_deadlock_event_history_table_storage.h",
        ],
        deps = [
            ":_generated_inner_table_headers",
            ":compaction_runtime",
            ":runtime_context",
            ":schema_model_interface",
            ":schema_registry",
            ":scn_interface",
            ":sqlite_storage_interface",
            ":tablet_replica_types",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "cluster_metadata_persistence": struct(
        hdrs = [
            "ob_freeze_info_proxy.h",
            "ob_global_merge_table_operator.h",
            "ob_global_stat_proxy.h",
            "ob_scheduled_job_utils.h",
            "ob_schema_status_proxy.h",
            "ob_schema_version_info.h",
            "ob_snapshot_table_proxy.h",
        ],
        deps = [
            ":cluster_version_interface",
            ":core_types",
            ":schema_model_interface",
            ":scn_interface",
            ":sql_persistence_support",
            "//src/oblib:oblib_common",
        ],
    ),
    "cluster_topology": struct(
        hdrs = [
            "ob_lease_struct.h",
            "ob_server_status.h",
            "ob_share_util.h",
        ],
        deps = [
            ":config",
            ":core_types",
            ":id_generator",
            ":scn_interface",
            ":tablet_replica_types",
            ":tenant_runtime",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "collection_types": struct(
        hdrs = [
            "ob_batch_selector.h",
            "ob_common_id.h",
            "ob_display_list.h",
            "ob_light_hashmap.h",
            "ob_simple_batch.h",
            "ob_truncated_string.h",
        ],
        deps = [
            ":config",
            ":error_codes",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "compaction_runtime": struct(
        hdrs = [
            "compaction/ob_array_with_map.h",
            "compaction/ob_compaction_info_param.h",
            "compaction/ob_compaction_time_guard.h",
            "compaction/ob_compaction_timer_task_mgr.h",
            "compaction/ob_new_micro_info.h",
            "compaction/ob_schedule_batch_size_mgr.h",
        ],
        deps = [
            ":delegate_interface",
            ":tenant_runtime",
            ":worker_runtime",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "ddl_coordination": struct(
        hdrs = [
            "ob_ddl_checksum.h",
            "ob_ddl_error_message_table_operator.h",
            "ob_ddl_sim_point.h",
            "ob_ddl_task_executor.h",
            "truncate_info/ob_truncate_info_util.h",
        ],
        deps = [
            ":config",
            ":ddl_protocol",
            ":error_codes",
            ":location",
            ":rpc_message_types",
            ":schema_entities",
            ":schema_model_interface",
            ":schema_registry",
            ":sql_persistence_support",
            ":thread_pool_interface",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "ddl_protocol": struct(
        hdrs = [
            "ob_ddl_args.h",
            "ob_ddl_common.h",
            "ob_ddl_task_serialize_field.h",
            "ob_ddl_sim_point_define.h",
            "ob_fork_table_info.h",
            "ob_lonely_table_clean_rpc_struct.h",
            "ob_unique_index_row_transformer.h",
        ],
        deps = [
            ":collection_types",
            ":config",
            ":core_types",
            ":location",
            ":schema_access",
            ":schema_model_interface",
            ":schema_registry",
            ":tablet_metadata",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "diagnostics": struct(
        hdrs = [
            "diagnosis/ob_sql_monitor_statname.h",
            "leak_checker/ob_leak_checker.h",
            "longops_mgr/ob_i_longops.h",
            "longops_mgr/ob_longops_mgr.h",
            "ob_structured_event_logger.h",
        ],
        deps = ["//src/oblib:oblib_foundation"],
    ),
    "execution_contracts": struct(
        hdrs = [
            "aggregate/ob_pushdown_aggregate_protocol.h",
            "ob_table_range.h",
        ],
        deps = [
            ":scn_interface",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "external_storage": struct(
        hdrs = [
        ],
        deps = [
            ":core_types",
            ":io",
            ":schema_access",
            ":task_control",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "freeze_coordination": struct(
        hdrs = [
            "ob_freeze_info_manager.h",
            "ob_merge_info.h",
        ],
        deps = [
            ":cluster_metadata_persistence",
            ":rpc_message_types",
            ":scn_interface",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "geo_algorithms": struct(
        hdrs = [
            "geo/ob_geo_dispatcher.h",
            "geo/ob_geo_func_area.h",
            "geo/ob_geo_func_box.h",
            "geo/ob_geo_func_buffer.h",
            "geo/ob_geo_func_centroid.h",
            "geo/ob_geo_func_common.h",
            "geo/ob_geo_func_correct.h",
            "geo/ob_geo_func_covered_by.h",
            "geo/ob_geo_func_crosses.h",
            "geo/ob_geo_func_difference.h",
            "geo/ob_geo_func_disjoint.h",
            "geo/ob_geo_func_dissolve_polygon.h",
            "geo/ob_geo_func_distance.h",
            "geo/ob_geo_func_distance_sphere.h",
            "geo/ob_geo_func_equals.h",
            "geo/ob_geo_func_intersects.h",
            "geo/ob_geo_func_isvalid.h",
            "geo/ob_geo_func_length.h",
            "geo/ob_geo_func_overlaps.h",
            "geo/ob_geo_func_register.h",
            "geo/ob_geo_func_symdifference.h",
            "geo/ob_geo_func_touches.h",
            "geo/ob_geo_func_transform.h",
            "geo/ob_geo_func_union.h",
            "geo/ob_geo_func_utils.h",
            "geo/ob_geo_func_within.h",
        ],
        deps = [
            ":geo_cast_interface",
            ":tenant_runtime",
            "//src/oblib:oblib_foundation",
            "@seekdb_3rd_headers//:boost_headers",
        ],
    ),
    "geo_serialization": struct(
        hdrs = [
            "geo/ob_geo_3d.h",
            "geo/ob_geo_mvt.h",
            "geo/ob_srs_wkt_parser.h",
            "geo/ob_vector_tile.pb-c.h",
        ],
        deps = [
            ":geo_cast_interface",
            ":geo_types",
            ":geo_visitors",
            "//src/oblib:oblib_foundation",
            "@seekdb_3rd_headers//:protobuf_c_headers",
        ],
    ),
    "geo_visitors": struct(
        hdrs = [
            "geo/ob_geo_check_empty_visitor.h",
            "geo/ob_geo_coordinate_range_visitor.h",
            "geo/ob_geo_denormalize_visitor.h",
            "geo/ob_geo_elevation_visitor.h",
            "geo/ob_geo_interior_point_visitor.h",
            "geo/ob_geo_latlong_check_visitor.h",
            "geo/ob_geo_normalize_visitor.h",
            "geo/ob_geo_reverse_coordinate_visitor.h",
            "geo/ob_geo_to_wkt_visitor.h",
            "geo/ob_geo_wkb_check_visitor.h",
            "geo/ob_geo_wkb_size_visitor.h",
            "geo/ob_geo_wkb_visitor.h",
            "geo/ob_geo_zoom_in_visitor.h",
            "geo/ob_wkb_byte_order_visitor.h",
            "geo/ob_wkb_to_json_bin_visitor.h",
        ],
        deps = [
            ":geo_cast_interface",
            ":geo_types",
        ],
    ),
    "inner_table_schema_io": struct(
        hdrs = [
            "inner_table/ob_dump_inner_table_schema.h",
            "inner_table/ob_load_inner_table_schema.h",
        ],
        deps = [
            ":_generated_inner_table_headers",
            ":schema_registry",
            ":sql_persistence_support",
        ],
    ),
    "module_data_protocol": struct(
        hdrs = [
        ],
        deps = [
            "//src/oblib:oblib_foundation",
        ],
    ),
    "interrupt_runtime": struct(
        hdrs = [
            "interrupt/ob_global_interrupt_call.h",
            "interrupt/ob_interrupt_message.h",
        ],
        deps = [
            ":config",
            ":runtime_context",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "io_bench_controller_interface": struct(
        hdrs = ["io/ob_i_io_bench_controller.h"],
        deps = [],
    ),
    "local_device_space_interface": struct(
        hdrs = ["ob_i_local_device_space_provider.h"],
        deps = [],
    ),
    "memory_support": struct(
        hdrs = ["allocator/ob_reserve_arena.h"],
        deps = ["//src/oblib:oblib_foundation"],
    ),
    "redo_log_storage": struct(
        hdrs = [
            "ob_local_device.h",
            "redolog/ob_log_definition.h",
            "redolog/ob_log_file_group.h",
            "redolog/ob_log_file_handler.h",
            "redolog/ob_log_policy.h",
        ],
        deps = [
            ":io_device_interface",
            ":local_device_space_interface",
            "//src/oblib:oblib_foundation",
            "@seekdb_3rd_headers//:libaio_headers",
        ],
    ),
    "rpc_dispatch": struct(
        hdrs = [
            "ob_ex_rpc.h",
            "rpc/ob_server_task.h",
        ],
        deps = [
            ":runtime_context",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "rpc_message_types": struct(
        hdrs = ["ob_rpc_struct.h"],
        deps = [
            ":ai_endpoint_interface",
            ":cache",
            ":cluster_metadata_persistence",
            ":cluster_topology",
            ":cluster_version_interface",
            ":collection_types",
            ":config",
            ":ddl_protocol",
            ":debug_sync_interface",
            ":est_row_count_record",
            ":external_storage",
            ":inner_table_schema_io",
            ":io",
            ":lib_cache_namespace",
            ":log_protocol",
            ":resource_limit_calculator",
            ":schema_access",
            ":schema_entities",
            ":schema_foundation_types",
            ":schema_registry",
            ":scn_interface",
            ":sequence_runtime",
            ":session_state",
            ":statement_type",
            ":table_lock_priority",
            ":tablet_autoincrement_types",
            ":transaction_id",
            ":unit_topology",
            ":utility_types",
            "//src/oblib:oblib_db_values_base",
            "//src/oblib:oblib_db_values_primitives",
            "//src/oblib:oblib_db_values_runtime",
            "//src/oblib:oblib_db_values_services",
        ],
    ),
    "runtime_context": struct(
        hdrs = ["ob_server_struct.h"],
        deps = [
            ":cluster_topology",
            ":config",
            ":rpc_message_types",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "schema_publish_signal": struct(
        hdrs = ["schema/ob_schema_publish_signal.h"],
        deps = ["//src/oblib:oblib_foundation"],
    ),
    "schema_access": struct(
        hdrs = [
            "schema/ob_dependency_info.h",
            "schema/ob_latest_schema_guard.h",
            "schema/ob_multi_version_schema_service.h",
            "schema/ob_schema_getter_guard.h",
            "schema/ob_schema_guard_wrapper.h",
            "schema/ob_schema_service.h",
            "schema/ob_server_schema_service.h",
        ],
        deps = [
            ":_generated_inner_table_headers",
            ":core_types",
            ":error_codes",
            ":schema_entities",
            ":schema_model_interface",
            ":schema_registry",
            ":schema_transaction_control",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "schema_entities": struct(
        hdrs = [
            "schema/ob_col_desc.h",
            "schema/ob_column_schema.h",
            "schema/ob_constraint.h",
            "schema/ob_error_info.h",
            "schema/ob_objpriv_mysql_schema_struct.h",
            "schema/ob_part_mgr_util.h",
            "schema/ob_schema_utils.h",
        ],
        deps = [
            ":config",
            ":core_types",
            ":error_codes",
            ":schema_model_interface",
            ":schema_partition_iteration",
            ":session_state",
            ":system_variable_metadata",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "schema_persistence": struct(
        hdrs = [
            "schema/ob_ai_model_sql_service.h",
            "schema/ob_database_sql_service.h",
            "schema/ob_ddl_sql_service.h",
            "schema/ob_outline_sql_service.h",
            "schema/ob_priv_sql_service.h",
            "schema/ob_routine_sql_service.h",
            "schema/ob_sys_variable_sql_service.h",
            "schema/ob_table_sql_service.h",
            "schema/ob_trigger_sql_service.h",
            "schema/ob_user_sql_service.h",
        ],
        deps = [
            ":rpc_message_types",
            ":schema_access",
            ":schema_entities",
            ":schema_foundation_types",
            ":sql_persistence_support",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "schema_registry": struct(
        hdrs = [
            "schema/ob_ai_model_mgr.h",
            "schema/ob_mock_fk_parent_table_mgr.h",
            "schema/ob_outline_mgr.h",
            "schema/ob_package_info.h",
            "schema/ob_package_mgr.h",
            "schema/ob_priv_mgr.h",
            "schema/ob_routine_info.h",
            "schema/ob_routine_mgr.h",
            "schema/ob_schema_cache.h",
            "schema/ob_schema_mem_mgr.h",
            "schema/ob_schema_mgr.h",
            "schema/ob_schema_mgr_cache.h",
            "schema/ob_schema_store.h",
            "schema/ob_sys_variable_mgr.h",
            "schema/ob_table_schema.h",
            "schema/ob_trigger_info.h",
            "schema/ob_trigger_mgr.h",
        ],
        deps = [
            ":ai_endpoint_interface",
            ":cache",
            ":core_types",
            ":pl_integer_type",
            ":schema_entities",
            ":schema_model_interface",
            ":session_state",
            ":storage_cache_policy",
            ":tenant_runtime",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "schema_transaction_control": struct(
        hdrs = [
            "schema/ob_ddl_epoch.h",
            "schema/ob_ddl_trans_controller.h",
        ],
        deps = [
            "//src/oblib:oblib_foundation",
            "//src/oblib:oblib_mysql_client_services",
        ],
    ),
    "sequence_runtime": struct(
        hdrs = [
        ],
        deps = [
            ":sequence_option",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "session_state": struct(
        hdrs = ["session/ob_local_session_var.h"],
        deps = [
            ":system_variable_metadata",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "sql_persistence_support": struct(
        hdrs = [
            "ob_core_table_proxy.h",
            "ob_dml_sql_splicer.h",
            "ob_lock_metadata_session.h",
            "ob_sql_client_decorator.h",
        ],
        deps = [
            "//src/oblib:oblib_foundation",
        ],
    ),
    "table_access_runtime": struct(
        hdrs = ["ob_table_access_helper.h"],
        deps = [
            ":core_types",
            ":error_codes",
            ":runtime_context",
            ":tenant_runtime",
            ":worker_runtime",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "table_runtime": struct(
        hdrs = [
            "table/ob_table_util.h",
        ],
        deps = [
            ":rpc_message_types",
            ":schema_access",
            ":schema_registry",
            ":ttl_schedule_interface",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "tablet_metadata": struct(
        hdrs = [
            "tablet/ob_tablet_mapping_operator.h",
            "tablet/ob_tablet_meta_table_storage.h",
            "tablet/ob_tablet_read_mode.h",
            "tablet/ob_tablet_table_iterator.h",
            "tablet/ob_tablet_table_operator.h",
            "tablet/ob_tablet_to_table_history_operator.h",
        ],
        deps = [
            ":compaction_runtime",
            ":sqlite_storage_interface",
            ":tablet_replica_types",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "timezone_runtime": struct(
        hdrs = [
            "ob_sys_time_zone_util.h",
            "ob_time_zone_info_manager.h",
        ],
        deps = [
            ":cluster_topology",
            ":tenant_runtime",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "unit_topology": struct(
        hdrs = [
        ],
        deps = [
            ":core_types",
            ":tenant_runtime",
            ":unit_config_interface",
            ":worker_runtime",
            "//src/oblib:oblib_foundation",
        ],
    ),
    "utility_types": struct(
        hdrs = [
            "ob_admin_dump_helper.h",
            "ob_time_utility2.h",
            "ob_version.h",
        ],
        deps = [
            ":core_types",
            "//src/oblib:oblib_foundation",
            "@seekdb_3rd_headers//:rapidjson_headers",
        ],
    ),
    "worker_runtime": struct(
        hdrs = [
            "ob_check_stop_provider.h",
            "ob_occam_thread_pool.h",
            "ob_occam_time_guard.h",
            "ob_occam_timer.h",
            "ob_shared_timer.h",
            "ob_throttling_utils.h",
        ],
        deps = [
            ":core_types",
            ":delegate_interface",
            ":tenant_runtime",
            ":thread_pool_interface",
            ":time_wheel",
            "//src/oblib:oblib_foundation",
        ],
    ),
}

SHARE_SEMANTIC_API_TARGETS = (
    _EXISTING_SEMANTIC_API_TARGETS +
    sorted(_SHARE_SEMANTIC_HEADER_TARGETS.keys())
)

def declare_share_semantic_header_targets():
    """Declare the header-only semantic owner targets."""
    for name, spec in _SHARE_SEMANTIC_HEADER_TARGETS.items():
        cc_library(
            name = name,
            hdrs = spec.hdrs,
            deps = spec.deps,
            features = ["layering_check"],
            tags = ["manual"],
            visibility = _PUBLIC_VISIBILITY,
        )

def _package_header(label):
    value = str(label)
    package_prefix = "//src/share:"
    if value.startswith(package_prefix):
        return value[len(package_prefix):]
    if value.startswith(":"):
        return value[1:]
    return value

def validate_share_semantic_header_ownership():
    """Fail package loading unless each externally reachable header has one owner."""
    expected = {}
    for header in SHARE_PUBLIC_HEADER_ROOTS + SHARE_INTERFACE_CLOSURE_HEADERS:
        if header in expected:
            fail("duplicate header in Share interface inventory: %s" % header)
        expected[header] = True

    rules = native.existing_rules()
    owners = {}
    for target in SHARE_SEMANTIC_API_TARGETS:
        if target not in rules:
            fail("missing Share semantic owner target: %s" % target)
        rule = rules[target]
        for attribute in ["hdrs", "textual_hdrs"]:
            for label in rule.get(attribute, []):
                header = _package_header(label)
                if header not in expected:
                    fail(
                        "Share semantic target %s exposes non-interface header %s" %
                        (target, header),
                    )
                if header in owners:
                    fail(
                        "Share interface header %s has multiple semantic owners: %s and %s" %
                        (header, owners[header], target),
                    )
                owners[header] = target

    missing = sorted([
        header
        for header in expected
        if header not in owners
    ])
    if missing:
        fail(
            "Share interface headers without semantic owners (%d): %s" %
            (len(missing), missing),
        )
