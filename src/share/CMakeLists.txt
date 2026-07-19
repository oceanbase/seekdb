find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(INNER_TABLE_SCRIPT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/inner_table)
set(INNER_TABLE_GENERATOR ${INNER_TABLE_SCRIPT_DIR}/generate_inner_table_schema.py)
set(INNER_TABLE_DEF_FILE ${INNER_TABLE_SCRIPT_DIR}/ob_inner_table_schema_def.py)
set(INNER_TABLE_INIT_DATA ${INNER_TABLE_SCRIPT_DIR}/ob_inner_table_init_data.py)

execute_process(
  COMMAND ${Python3_EXECUTABLE} ${INNER_TABLE_GENERATOR}
  WORKING_DIRECTORY ${INNER_TABLE_SCRIPT_DIR}
  OUTPUT_VARIABLE INNER_TABLE_SCHEMA_CPP_RELATIVE_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE INNER_TABLE_SCHEMA_CPP_LIST_RESULT
)

if (NOT INNER_TABLE_SCHEMA_CPP_LIST_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to query inner table fixed schema cpp outputs")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${INNER_TABLE_GENERATOR}
  ${INNER_TABLE_DEF_FILE}
  ${INNER_TABLE_INIT_DATA}
)

ob_set_subtarget(ob_share_extra ALONE
  datum/ob_datum_funcs_compilation/ob_datum_funcs_compilation_0.cpp
  datum/ob_datum_funcs_compilation/ob_datum_funcs_compilation_1.cpp
  datum/ob_datum_funcs_compilation/ob_datum_funcs_compilation_2.cpp
  datum/ob_datum_funcs_compilation/ob_datum_funcs_compilation_3.cpp
  datum/ob_datum_funcs_compilation/ob_datum_funcs_compilation_4.cpp
  datum/ob_datum_funcs_compilation/ob_datum_funcs_compilation_5.cpp
  datum/ob_datum_funcs_compilation/ob_datum_funcs_compilation_6.cpp
  datum/ob_datum_funcs_compilation/ob_datum_funcs_compilation_7.cpp
  datum/ob_datum_funcs_compilation/ob_datum_funcs_all.cpp
  datum/ob_str_datum_funcs_compilation/ob_str_datum_funcs_compilation_0.cpp
  datum/ob_str_datum_funcs_compilation/ob_str_datum_funcs_compilation_1.cpp
  datum/ob_str_datum_funcs_compilation/ob_str_datum_funcs_compilation_2.cpp
  datum/ob_str_datum_funcs_compilation/ob_str_datum_funcs_all.cpp
  datum/ob_datum_funcs.cpp
)

ob_set_subtarget(ob_share vector
  roaringbitmap/ob_roaringbitmap.cpp
  roaringbitmap/ob_rb_utils.cpp
  roaringbitmap/ob_rb_bin.cpp
  roaringbitmap/ob_rb_memory_mgr.cpp
  ob_mysql_proc_statement.cpp
)

ob_set_subtarget(ob_share ALONE
  ob_tenant_timezone.cpp
  ob_tenant_timezone_mgr.cpp
  ob_sql_client_decorator.cpp
  ob_rpc_struct.cpp
  ob_tenant_mgr.cpp
  parameter/ob_parameter_attr.cpp
  ob_ddl_args.cpp
  ob_lonely_table_clean_rpc_struct.cpp
)

file(GLOB SCHEMA_CPPS "inner_table/ob_inner_table_schema.*.cpp")
ob_set_subtarget(ob_share SCHEMA_CPPS
  ${SCHEMA_CPPS}
)

ob_set_subtarget(ob_share LOAD_SCHEMA inner_table/ob_load_inner_table_schema.cpp)

ob_set_subtarget(ob_share DUMP_SCHEMA
  inner_table/ob_dump_inner_table_schema.cpp
)

ob_set_subtarget(ob_share cache
  cache/ob_kv_storecache.cpp
  cache/ob_kvcache_inst_map.cpp
  cache/ob_kvcache_map.cpp
  cache/ob_kvcache_store.cpp
  cache/ob_kvcache_struct.cpp
  cache/ob_kvcache_hazard_version.cpp
  cache/ob_kvcache_hazard_pointer.cpp
  cache/ob_kvcache_hazard_domain.cpp
)

ob_set_subtarget(ob_share common
  ob_autoincrement_param.cpp
  ob_autoincrement_service.cpp
  ob_cluster_role.cpp
  ob_cluster_version.cpp
  ob_column_checksum_error_operator.cpp
  ob_core_table_proxy.cpp
  ob_ddl_checksum.cpp
  ob_ddl_sim_point.cpp
  ob_fork_table_info.cpp
  ob_scheduled_job_utils.cpp
  ob_ddl_common.cpp
  ob_ddl_error_message_table_operator.cpp
  ob_ddl_task_executor.cpp
  ob_debug_sync.cpp
  ob_debug_sync_point.cpp
  ob_device_manager.cpp
  ob_dml_sql_splicer.cpp
  ob_encryption_util.cpp
  ob_errno.cpp
  ob_kv_storage.cpp
  ob_all_tenant_info.cpp
  ob_event_history_table_operator.cpp
  ob_schema_version_info.cpp
  ob_freeze_info_proxy.cpp
  ob_gais_msg.cpp
  session/ob_local_session_var.cpp
  ob_global_autoinc_service.cpp
  ob_global_merge_table_operator.cpp
  ob_global_stat_proxy.cpp
  ob_i_tablet_scan.cpp
  ob_io_device_helper.cpp
  ob_kv_parser.cpp
  ob_lease_struct.cpp
  ob_list_parser.cpp
  ob_local_device.cpp
  ob_table_range.cpp
  ob_max_id_fetcher.cpp
  ob_max_id_cache.cpp
  ob_occam_time_guard.cpp
  ob_priv_common.cpp
  ob_proposal_id.cpp
  ob_plugin_helper.cpp
  ob_resource_limit.cpp
  ob_schema_status_proxy.cpp
  ob_server_status.cpp
  ob_server_struct.cpp
  ob_share_util.cpp
  ob_simple_batch.cpp
  ob_simple_mem_limit_getter.cpp
  ob_tablet_autoincrement_param.cpp
  ob_tablet_checksum_iterator.cpp
  ob_tablet_checksum_operator.cpp
  ob_tablet_replica_checksum_operator.cpp
  ob_tablet_meta_table_compaction_operator.cpp
  ob_task_define.cpp
  ob_telemetry.cpp
  ob_tenant_role.cpp
  ob_tenant_switchover_status.cpp
  ob_tenant_memstore_info_operator.cpp
  ob_time_utility2.cpp
  ob_time_zone_info_manager.cpp
  ob_unique_index_row_transformer.cpp
  ob_unit_getter.cpp
  ob_freeze_info_manager.cpp
  ob_zone_merge_info.cpp
  ob_zone_merge_table_operator.cpp
  ob_admin_dump_helper.cpp
  ob_inner_kv_table_operator.cpp
  ob_inner_table_operator.cpp
  ob_common_id.cpp
  ob_cluster_event_history_table_operator.cpp
  scn.cpp
  ob_throttling_utils.cpp
  ob_batch_selector.cpp
  ob_internal_table_change_notifier.cpp
)

# ob_local_device.cpp defines AIO emulation types/functions for Android/macOS that
# conflict with stub definitions in ob_file.h when merged into the same translation unit.
set_source_files_properties(ob_local_device.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

ob_set_subtarget(ob_share common_mixed
  datum/ob_datum_cmp_func_def.cpp
  diagnosis/ob_sql_monitor_statname.cpp
  interrupt/ob_global_interrupt_call.cpp
  interrupt/ob_interrupt_message.cpp
  ls/ob_ls_restore_status.cpp
  ob_sys_time_zone_util.cpp
  ls/ob_restore_status.cpp
  location_cache/ob_location_struct.cpp
  inner_table/ob_inner_table_schema.vt.cpp
  inner_table/ob_inner_table_schema.lob.cpp
  inner_table/ob_inner_table_schema_misc.ipp
  object/ob_obj_cast.cpp
  object/ob_decint_scale_util.cpp
  object/ob_enumset_str_util.cpp
  object/ob_array_cast.cpp
  parameter/ob_parameter_seed.ipp
  rc/ob_context.cpp
  rc/ob_tenant_base.cpp
  sequence/ob_sequence_cache.cpp
  sequence/ob_sequence_dml_proxy.cpp
  sequence/ob_sequence_option.cpp
  sequence/ob_sequence_option_builder.cpp
  system_variable/ob_sys_var_meta.cpp
  system_variable/ob_system_variable_init.cpp
  ob_lob_access_utils.cpp
  ob_json_access_utils.cpp
  tenant_snapshot/ob_tenant_snapshot_id.cpp
  table/ob_ttl_util.cpp
  index_usage/ob_index_usage_info_mgr.cpp
  index_usage/ob_index_usage_report_task.cpp
  table/ob_table_util.cpp
  compaction/ob_compaction_time_guard.cpp
  compaction/ob_schedule_batch_size_mgr.cpp
  compaction/ob_compaction_timer_task_mgr.cpp
  compaction/ob_new_micro_info.cpp
  storage/ob_sqlite_connection.cpp
  storage/ob_sqlite_connection_pool.cpp
  storage/ob_reserved_snapshot_table_storage.cpp
  storage/ob_deadlock_event_history_table_storage.cpp
  storage/ob_server_event_history_table_storage.cpp
  storage/ob_tenant_event_history_table_storage.cpp
  storage/ob_tablet_replica_checksum_table_storage.cpp
  storage/ob_zone_merge_info_table_storage.cpp
  storage/ob_merge_info_table_storage.cpp
  storage/ob_column_checksum_error_info_table_storage.cpp
  storage/ob_rootservice_job_table_storage.cpp
  storage_cache_policy/ob_storage_cache_common.cpp
  storage_cache_policy/ob_storage_cache_partition_sql_helper.cpp
  longops_mgr/ob_i_longops.cpp
  longops_mgr/ob_longops_mgr.cpp
  ob_module_data_arg.cpp
)

ob_set_subtarget(ob_share tablet
  tablet/ob_tablet_info.cpp
  tablet/ob_tablet_table_operator.cpp
  tablet/ob_tablet_mapping_operator.cpp
  tablet/ob_tablet_table_iterator.cpp
  tablet/ob_tablet_meta_table_storage.cpp
  tablet/ob_tablet_filter.cpp
  tablet/ob_tablet_to_table_history_operator.cpp
)

ob_set_subtarget(ob_share config
  config/ob_tenant_config_mgr.cpp
  config/ob_common_config.cpp
  config/ob_config.cpp
  config/ob_config_helper.cpp
  config/ob_config_manager.cpp
  config/ob_reload_config.cpp
  config/ob_server_config.cpp
  config/ob_system_config.cpp
  config/ob_system_config_key.cpp
  config/ob_config_storage.cpp
)


ob_set_subtarget(ob_share redolog
  redolog/ob_clog_switch_write_callback.cpp
  redolog/ob_log_file_group.cpp
  redolog/ob_log_file_handler.cpp
  redolog/ob_log_file_reader.cpp
  redolog/ob_log_policy.cpp
)

ob_set_subtarget(ob_share schema
  schema/ob_col_desc.cpp
  schema/ob_column_schema.cpp
  schema/ob_constraint.cpp
  schema/ob_database_sql_service.cpp
  schema/ob_ddl_sql_service.cpp
  schema/ob_dependency_info.cpp
  schema/ob_directory_mgr.cpp
  schema/ob_directory_sql_service.cpp
  schema/ob_error_info.cpp
  schema/ob_mock_fk_parent_table_mgr.cpp
  schema/ob_multi_version_schema_service.cpp
  schema/ob_outline_mgr.cpp
  schema/ob_outline_sql_service.cpp
  schema/ob_package_info.cpp
  schema/ob_package_mgr.cpp
  schema/ob_part_mgr_util.cpp
  schema/ob_partition_sql_helper.cpp
  schema/ob_priv_mgr.cpp
  schema/ob_priv_sql_service.cpp
  schema/ob_routine_info.cpp
  schema/ob_routine_mgr.cpp
  schema/ob_routine_sql_service.cpp
  schema/ob_schema_cache.cpp
  schema/ob_schema_macro_define.cpp
  schema/ob_schema_mem_mgr.cpp
  schema/ob_schema_mgr.cpp
  schema/ob_schema_mgr_cache.cpp
  schema/ob_schema_service.cpp
  schema/ob_schema_store.cpp
  schema/ob_schema_utils.cpp
  schema/ob_sequence_mgr.cpp
  schema/ob_sys_variable_mgr.cpp
  schema/ob_sys_variable_sql_service.cpp
  schema/ob_table_iter.cpp
  schema/ob_table_sql_service.cpp
  schema/ob_tablegroup_sql_service.cpp
  schema/ob_tenant_schema_service.cpp
  schema/ob_tenant_sql_service.cpp
  schema/ob_trigger_info.cpp
  schema/ob_trigger_mgr.cpp
  schema/ob_trigger_sql_service.cpp
  schema/ob_udf.cpp
  schema/ob_udf_mgr.cpp
  schema/ob_udf_sql_service.cpp
  schema/ob_user_sql_service.cpp
  schema/ob_context_sql_service.cpp
  schema/ob_context_mgr.cpp
  schema/ob_context_ddl_proxy.cpp
  schema/ob_ddl_epoch.cpp
  schema/ob_ddl_trans_controller.cpp
  schema/ob_schema_getter_guard.cpp
  schema/ob_schema_getter_guard_priv.cpp
  schema/ob_schema_getter_guard_ccl_rule.cpp
  schema/ob_schema_struct.cpp
  schema/ob_sequence_sql_service.cpp
  schema/ob_server_schema_service.cpp
  schema/ob_table_schema.cpp
  schema/ob_latest_schema_guard.cpp
  schema/ob_list_row_values.cpp
  schema/ob_catalog_schema_struct.cpp
  schema/ob_catalog_mgr.cpp
  schema/ob_catalog_sql_service.cpp
  schema/ob_ccl_schema_struct.cpp
  schema/ob_ccl_rule_mgr.cpp
  schema/ob_ccl_rule_sql_service.cpp
  schema/ob_schema_guard_wrapper.cpp
  
  schema/ob_ai_model_mgr.cpp
  schema/ob_ai_model_sql_service.cpp
  schema/ob_location_mgr.cpp
  schema/ob_location_schema_struct.cpp
  schema/ob_location_sql_service.cpp
  schema/ob_objpriv_mysql_schema_struct.cpp
  schema/ob_schema_getter_guard_location.cpp
  schema/ob_schema_getter_guard_objpriv_mysql.cpp
  schema/ob_location_schema_struct.cpp
  schema/ob_schema_getter_guard_location.cpp
)

ob_set_subtarget(ob_share catalog
  catalog/ob_catalog_properties.cpp
  catalog/ob_external_object_ctx.cpp
  catalog/ob_external_catalog.cpp
  catalog/ob_cached_catalog_meta_getter.cpp
  catalog/ob_catalog_meta_getter.cpp
  catalog/ob_catalog_utils.cpp
)


ob_set_subtarget(ob_share io
  io/ob_backup_io_adapter.cpp
  io/ob_backup_storage_info.cpp
  io/ob_io_define.cpp
  io/io_schedule/ob_io_schedule_v2.cpp
  io/ob_io_struct.cpp
  io/ob_io_calibration.cpp
  io/ob_io_manager.cpp
)

ob_set_subtarget(ob_share unit
  unit/ob_resource_pool.cpp
  unit/ob_unit_config.cpp
  unit/ob_unit_resource.cpp
  unit/ob_unit_info.cpp
)

ob_set_subtarget(ob_share resource_limit_calculator
  resource_limit_calculator/ob_resource_commmon.cpp
  resource_limit_calculator/ob_resource_limit_calculator.cpp
)

if (OB_ERRSIM)
 ob_set_subtarget(ob_share errsim_module
   errsim_module/ob_tenant_errsim_module_mgr.cpp
   errsim_module/ob_tenant_errsim_event_mgr.cpp
   errsim_module/ob_errsim_module_interface_imp.cpp
 )
endif()

ob_set_subtarget(ob_share text_analysis
  text_analysis/ob_text_analyzer.cpp
  text_analysis/ob_token_stream.cpp
)


ob_set_subtarget(ob_share object_storage
  object_storage/ob_device_config_parser.cpp
  object_storage/ob_device_connectivity.cpp
  object_storage/ob_device_manifest.cpp
  object_storage/ob_object_storage_struct.cpp
)

ob_set_subtarget(ob_share domain_id
)

ob_set_subtarget(ob_share semistruct
  semistruct/ob_sub_column_path.cpp
)

ob_set_subtarget(ob_share geo
  geo/ob_geo_common.cpp
  geo/ob_geo_bin.cpp
  geo/ob_geo_ibin.cpp
  geo/ob_geo_bin_iter.ipp
  geo/ob_geo_tree.cpp
  geo/ob_s2adapter.cpp
  geo/ob_geo.cpp
  geo/ob_wkt_parser.cpp
  geo/ob_geo_utils.cpp
  geo/ob_srs_wkt_parser.cpp
  geo/ob_srs_info.cpp
  geo/ob_geo_visitor.cpp
  geo/ob_geo_wkb_size_visitor.cpp
  geo/ob_geo_wkb_visitor.cpp
  geo/ob_geo_coordinate_range_visitor.cpp
  geo/ob_geo_latlong_check_visitor.cpp
  geo/ob_geo_longtitude_correct_visitor.cpp
  geo/ob_geo_to_tree_visitor.cpp
  geo/ob_geo_to_s2_visitor.cpp
  geo/ob_geo_to_wkt_visitor.cpp
  geo/ob_geo_reverse_coordinate_visitor.cpp
  geo/ob_geo_normalize_visitor.cpp
  geo/ob_geo_denormalize_visitor.cpp
  geo/ob_geo_wkb_check_visitor.cpp
  geo/ob_geo_zoom_in_visitor.cpp
  geo/ob_geo_func_envelope.cpp
  geo/ob_geo_func_area.cpp
  geo/ob_geo_func_correct.cpp
  geo/ob_geo_func_disjoint.cpp
  geo/ob_geo_func_utils.cpp
  geo/ob_geo_func_distance_sphere.cpp
  geo/ob_geo_func_transform.cpp
  geo/ob_geo_func_box.cpp
  geo/ob_geo_func_buffer.cpp
  geo/ob_geo_func_isvalid.cpp
  geo/ob_geometry_cast.cpp
  geo/ob_wkb_to_json_visitor.cpp
  geo/ob_wkb_to_json_bin_visitor.cpp
  geo/ob_wkb_byte_order_visitor.cpp
  geo/ob_geo_3d.cpp
  geo/ob_geo_func_centroid.cpp
  geo/ob_geo_interior_point_visitor.cpp
  geo/ob_geo_box_clip_visitor.cpp
  geo/ob_geo_elevation_visitor.cpp
  geo/ob_geo_func_length.cpp
  geo/ob_geo_mvt_encode_visitor.cpp
  geo/ob_geo_mvt.cpp
  geo/ob_vector_tile.pb-c.c
  geo/ob_geo_affine_visitor.cpp
  geo/ob_geo_grid_visitor.cpp
  geo/ob_geo_simplify_visitor.cpp
  geo/ob_geo_box_clip_visitor.cpp
  geo/ob_geo_func_dissolve_polygon.cpp
  geo/ob_geo_close_ring_visitor.cpp
  geo/ob_geo_cache.cpp
  geo/ob_geo_vertex_collect_visitor.cpp
  geo/ob_geo_segment_collect_visitor.cpp
  geo/ob_geo_cache_polygon.cpp
  geo/ob_geo_cache_point.cpp
  geo/ob_geo_cache_linestring.cpp
  geo/ob_geo_segment_intersect_analyzer.cpp
  geo/ob_geo_topology_calculate.cpp
  geo/ob_point_location_analyzer.cpp
  geo/ob_geo_point_location_visitor.cpp
)

ob_set_subtarget(ob_share ALONE
  geo/ob_geo_func_difference1.cpp
  geo/ob_geo_func_difference2.cpp
  geo/ob_geo_func_difference3.cpp
  geo/ob_geo_func_covered_by1.cpp
  geo/ob_geo_func_covered_by2.cpp
  geo/ob_geo_func_within1.cpp
  geo/ob_geo_func_within2.cpp
  geo/ob_geo_func_union1.cpp
  geo/ob_geo_func_union2.cpp
  geo/ob_geo_func_touches1.cpp
  geo/ob_geo_func_touches2.cpp
  geo/ob_geo_func_touches3.cpp
  geo/ob_geo_func_equals1.cpp
  geo/ob_geo_func_equals2.cpp
  geo/ob_geo_func_equals3.cpp
  geo/ob_geo_func_intersects.cpp
  geo/ob_geo_func_symdifference1.cpp
  geo/ob_geo_func_symdifference2.cpp
  geo/ob_geo_func_symdifference3.cpp
  geo/ob_geo_func_distance.cpp
  geo/ob_geo_func_crosses1.cpp
  geo/ob_geo_func_crosses2.cpp
  geo/ob_geo_func_overlaps1.cpp
  geo/ob_geo_func_overlaps2.cpp
  geo/ob_geo_func_overlaps3.cpp
)

ob_add_new_object_target(ob_share ob_share)
ob_add_new_object_target(ob_share_extra ob_share_extra)

target_compile_options(ob_share PRIVATE)
#target_compile_options(ob_share PRIVATE -Wshorten-64-to-32)
target_link_libraries(ob_share PUBLIC ob_base)
target_compile_options(ob_share_extra PRIVATE)
target_link_libraries(ob_share_extra PUBLIC ob_base_without_pass)

add_library(ob_share_static
  STATIC
  EXCLUDE_FROM_ALL)

target_link_libraries(ob_share_static
  PUBLIC ob_share ob_share_extra)
