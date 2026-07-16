ob_set_subtarget(ob_server ALONE
  ob_server.cpp
  ob_service.cpp
)

ob_set_subtarget(ob_server schema
  schema/ob_schema_service_sql_impl.cpp
  schema/ob_schema_retrieve_utils.ipp
)

ob_set_subtarget(ob_server common
  ob_standby_schema_refresh_trigger.cpp
  ob_dump_task_generator.cpp
  ob_inner_sql_transmit_struct.cpp
  ob_inner_sql_connection.cpp
  ob_inner_sql_connection_pool.cpp
  ob_resource_inner_sql_connection_pool.cpp
  ob_inner_sql_read_context.cpp
  ob_inner_sql_result.cpp
  ob_req_time_service.cpp
  ob_root_service_monitor.cpp
  ob_server_event_history_table_operator.cpp
  ob_server_reload_config.cpp
  ob_server_schema_updater.cpp
  ob_server_utils.cpp
  ob_signal_handle.cpp
  ob_srv_deliver.cpp
  ob_srv_network_frame.cpp
  ob_srv_xlator.cpp
  ob_ex_rpc.cpp
  ob_tenant_duty_task.cpp
  ob_sys_tenant_load_sys_package_service.cpp
  ob_sys_tenant_load_sys_package_task.cpp
  ob_uniq_task_queue.cpp
  ob_startup_accel_task_handler.cpp
)

ob_set_subtarget(ob_server common_mixed
  report/ob_tablet_table_updater.cpp
  report/ob_tenant_meta_checker.cpp
)

ob_set_subtarget(ob_server dbms_job
  dbms_job/ob_dbms_job_executor.cpp
  dbms_job/ob_dbms_job_master.cpp
  dbms_job/ob_dbms_job_utils.cpp
  dbms_scheduler/ob_dbms_sched_job_utils.cpp
  dbms_scheduler/ob_dbms_sched_job_executor.cpp
  dbms_scheduler/ob_dbms_sched_job_master.cpp
  dbms_scheduler/ob_dbms_sched_table_operator.cpp
  dbms_scheduler/ob_dbms_sched_service.cpp
 )

ob_set_subtarget(ob_server mysql
  mysql/ob_async_cmd_driver.cpp
  mysql/ob_async_plan_driver.cpp
  mysql/ob_mysql_end_trans_cb.cpp
  mysql/ob_mysql_result_set.cpp
  mysql/ob_query_driver.cpp
  mysql/ob_query_response_time.cpp
  mysql/ob_query_retry_ctrl.cpp
  mysql/ob_sync_cmd_driver.cpp
  mysql/ob_sync_plan_driver.cpp
  mysql/obmp_base.cpp
  mysql/obmp_change_user.cpp
  mysql/obmp_connect.cpp
  mysql/obmp_disconnect.cpp
  mysql/obmp_init_db.cpp
  mysql/obmp_packet_sender.cpp
  mysql/obmp_ping.cpp
  mysql/obmp_query.cpp
  mysql/obmp_process_info.cpp
  mysql/obmp_process_kill.cpp
  mysql/obmp_debug.cpp
  mysql/obmp_refresh.cpp
  mysql/obmp_reset_connection.cpp
  mysql/obmp_statistic.cpp
  mysql/obmp_stmt_close.cpp
  mysql/obmp_stmt_execute.cpp
  mysql/obmp_stmt_fetch.cpp
  mysql/obmp_stmt_prepare.cpp
  mysql/obmp_stmt_reset.cpp
  mysql/obmp_stmt_send_long_data.cpp
  mysql/obmp_stmt_send_piece_data.cpp
  mysql/obmp_utils.cpp
  mysql/obmp_auth_response.cpp
  mysql/obsm_conn_callback.cpp
  mysql/obsm_row.cpp
  mysql/obsm_utils.cpp
  mysql/obmp_set_option.cpp
)

ob_set_subtarget(ob_server net
  net/ob_ingress_bw_alloc_service.cpp
  net/ob_net_endpoint_ingress_rpc_struct.cpp
  net/ob_net_queue_traver.cpp
)

ob_set_subtarget(ob_server omt
  omt/ob_multi_tenant.cpp
  omt/ob_retry_queue.cpp
  omt/ob_tenant.cpp
  omt/ob_tenant_meta.cpp
  omt/ob_th_worker.cpp
  omt/ob_worker_processor.cpp
  omt/ob_tenant_srs.cpp
  omt/ob_tenant_ai_service.cpp
)

ob_set_subtarget(ob_server ai_service
  ai_service/ob_ai_service_struct.cpp
  ai_service/ob_ai_service_proxy.cpp
  ai_service/ob_ai_service_executor.cpp
)

ob_set_subtarget(ob_server change_stream
  change_stream/ob_change_stream_fetcher.cpp
  change_stream/ob_change_stream_dispatcher.cpp
  change_stream/ob_change_stream_worker.cpp
  change_stream/ob_change_stream_plugin.cpp
  change_stream/ob_change_stream_mgr.cpp
  change_stream/ob_cs_plugin_async_index.cpp
)

ob_set_subtarget(ob_server scheduler
  scheduler/ob_dag_warning_history_mgr.cpp
  scheduler/ob_independent_dag.cpp
  scheduler/ob_sys_task_stat.cpp
  scheduler/ob_tenant_dag_scheduler.cpp
  scheduler/ob_tenant_ddl_count_guard.cpp
)

ob_set_subtarget(ob_server vector_index
  vector_index/ob_hybrid_vector_refresh_task.cpp
  vector_index/ob_ivf_async_task.cpp
  vector_index/ob_ivf_async_task_executor.cpp
  vector_index/ob_json_helper.cpp
  vector_index/ob_plugin_vector_index_adaptor.cpp
  vector_index/ob_plugin_vector_index_scheduler.cpp
  vector_index/ob_plugin_vector_index_serialize.cpp
  vector_index/ob_plugin_vector_index_service.cpp
  vector_index/ob_plugin_vector_index_util.cpp
  vector_index/ob_plugin_vector_index_utils.cpp
  vector_index/ob_tenant_vector_index_async_task.cpp
  vector_index/ob_vector_embedding_handler.cpp
  vector_index/ob_vector_index_async_task.cpp
  vector_index/ob_vector_index_async_task_util.cpp
  vector_index/ob_vector_index_i_task_executor.cpp
  vector_index/ob_vector_index_ivf_cache_mgr.cpp
  vector_index/ob_vector_index_ivf_cache_util.cpp
  vector_index/ob_vector_index_param.cpp
  vector_index/ob_vector_index_util.cpp
  vector_index/ob_vector_kmeans_ctx.cpp
)


ob_set_subtarget(ob_server virtual_table
  virtual_table/ob_all_concurrency_object_pool.cpp
  virtual_table/ob_all_data_type_class_table.cpp
  virtual_table/ob_all_data_type_table.cpp
  virtual_table/ob_all_disk_stat.cpp
  virtual_table/ob_all_plan_cache_stat.cpp
  virtual_table/ob_all_virtual_bad_block_table.cpp
  virtual_table/ob_all_virtual_compaction_diagnose_info.cpp
  virtual_table/ob_all_virtual_server_compaction_event_history.cpp
  virtual_table/ob_all_virtual_compaction_suggestion.cpp
  virtual_table/ob_all_virtual_tablet_compaction_info.cpp
  virtual_table/ob_virtual_table_tablet_iter.cpp
  virtual_table/ob_all_virtual_dag.cpp
  virtual_table/ob_all_virtual_dag_warning_history.cpp
  virtual_table/ob_all_virtual_diag_index_scan.cpp
  virtual_table/ob_all_virtual_dtl_channel.cpp
  virtual_table/ob_all_virtual_dtl_first_cached_buffer.cpp
  virtual_table/ob_all_virtual_dtl_memory.cpp
  virtual_table/ob_all_virtual_dump_tenant_info.cpp
  virtual_table/ob_all_virtual_engine_table.cpp
  virtual_table/ob_all_virtual_files_table.cpp
  virtual_table/ob_all_virtual_id_service.cpp
  virtual_table/ob_all_virtual_io_stat.cpp
  virtual_table/ob_all_virtual_kvcache_store_memblock.cpp
  virtual_table/ob_all_virtual_load_data_stat.cpp
  virtual_table/ob_all_virtual_lock_wait_stat.cpp
  virtual_table/ob_all_virtual_long_ops_status.cpp
  virtual_table/ob_all_virtual_ls_info.cpp
  virtual_table/ob_all_virtual_timestamp_service.cpp
  virtual_table/ob_all_virtual_tracepoint_info.cpp
  virtual_table/ob_all_virtual_transaction_freeze_checkpoint.cpp
  virtual_table/ob_all_virtual_transaction_checkpoint.cpp
  virtual_table/ob_all_virtual_checkpoint.cpp
  virtual_table/ob_all_virtual_macro_block_marker_status.cpp
  virtual_table/ob_all_virtual_tablet_buffer_info.cpp
  virtual_table/ob_all_virtual_mds_node_stat.cpp
  virtual_table/ob_all_virtual_malloc_sample_info.cpp
  virtual_table/ob_all_virtual_mds_event_history.cpp
  virtual_table/ob_all_virtual_memory_context_stat.cpp
  virtual_table/ob_all_virtual_memory_info.cpp
  virtual_table/ob_all_virtual_memstore_info.cpp
  virtual_table/ob_all_virtual_minor_freeze_info.cpp
  virtual_table/ob_all_virtual_obj_lock.cpp
  virtual_table/ob_all_virtual_storage_meta_memory_status.cpp
  virtual_table/ob_all_virtual_tablet_pointer_status.cpp
  virtual_table/ob_all_virtual_tablet_sstable_macro_info.cpp
  virtual_table/ob_all_virtual_tablet_store_stat.cpp
  virtual_table/ob_all_virtual_ps_item_info.cpp
  virtual_table/ob_all_virtual_ps_stat.cpp
  virtual_table/ob_all_virtual_px_target_monitor.cpp
  virtual_table/ob_all_virtual_px_worker_stat.cpp
  virtual_table/ob_all_virtual_px_p2p_datahub.cpp
  virtual_table/ob_all_virtual_dtl_interm_result_monitor.cpp
  virtual_table/ob_all_virtual_raid_stat.cpp
  virtual_table/ob_all_virtual_server_compaction_progress.cpp
  virtual_table/ob_all_virtual_tablet_stat.cpp
  virtual_table/ob_all_virtual_server_object_pool.cpp
  virtual_table/ob_all_virtual_server_schema_info.cpp
  virtual_table/ob_all_virtual_schema_memory.cpp
  virtual_table/ob_all_virtual_schema_slot.cpp
  virtual_table/ob_all_virtual_session_info.cpp
  virtual_table/ob_all_virtual_sql_workarea_active.cpp
  virtual_table/ob_all_virtual_sql_workarea_histogram.cpp
  virtual_table/ob_all_virtual_sql_workarea_history_stat.cpp
  virtual_table/ob_all_virtual_sql_workarea_memory_info.cpp
  virtual_table/ob_all_virtual_sys_parameter_stat.cpp
  virtual_table/ob_all_virtual_sys_task_status.cpp
  virtual_table/ob_all_virtual_table_mgr.cpp
  virtual_table/ob_all_virtual_unit.cpp
  virtual_table/ob_all_virtual_tablet_ddl_kv_info.cpp
  virtual_table/ob_all_virtual_ddl_diagnose_info.cpp
  virtual_table/ob_all_virtual_ddl_sim_point_stat.cpp
  virtual_table/ob_all_virtual_tablet_info.cpp
  virtual_table/ob_all_virtual_server.cpp
  virtual_table/ob_all_virtual_server_storage.cpp
  virtual_table/ob_all_virtual_sqlite_tables.cpp
  virtual_table/ob_all_virtual_tablet_compaction_history.cpp
  virtual_table/ob_all_virtual_tablet_compaction_progress.cpp
  virtual_table/ob_all_virtual_io_status.cpp
  virtual_table/ob_all_virtual_tenant_ctx_memory_info.cpp
  virtual_table/ob_all_virtual_tenant_memstore_allocator_info.cpp
  virtual_table/ob_all_virtual_tx_stat.cpp
  virtual_table/ob_all_virtual_tx_lock_stat.cpp
  virtual_table/ob_all_virtual_tx_scheduler_stat.cpp
  virtual_table/ob_all_virtual_tx_ctx_mgr_stat.cpp
  virtual_table/ob_all_virtual_tx_data_table.cpp
  virtual_table/ob_all_virtual_tx_data.cpp
  virtual_table/ob_all_virtual_log_stat.cpp
  virtual_table/ob_all_virtual_apply_stat.cpp
  virtual_table/ob_all_virtual_replay_stat.cpp
  virtual_table/ob_all_virtual_ha_diagnose.cpp
  virtual_table/ob_global_variables.cpp
  virtual_table/ob_gv_sql.cpp
  virtual_table/ob_all_virtual_tenant_memstore_info.cpp
  virtual_table/ob_information_check_constraints_table.cpp
  virtual_table/ob_information_columns_table.cpp
  virtual_table/ob_information_global_status_table.cpp
  virtual_table/ob_information_kvcache_table.cpp
  virtual_table/ob_information_parameters_table.cpp
  virtual_table/ob_information_partitions_table.cpp
  virtual_table/ob_information_referential_constraints_table.cpp
  virtual_table/ob_information_schema_privileges_table.cpp
  virtual_table/ob_information_session_status_table.cpp
  virtual_table/ob_information_session_variables_table.cpp
  virtual_table/ob_information_table_constraints_table.cpp
  virtual_table/ob_information_table_privileges_table.cpp
  virtual_table/ob_information_triggers_table.cpp
  virtual_table/ob_information_user_privileges_table.cpp
  virtual_table/ob_information_query_response_time.cpp
  virtual_table/ob_information_schema_enable_roles_table.cpp
  virtual_table/ob_mem_leak_checker_info.cpp
  virtual_table/ob_mysql_db_table.cpp
  virtual_table/ob_mysql_proc_table.cpp
  virtual_table/ob_mysql_user_table.cpp
  virtual_table/ob_plan_cache_plan_explain.cpp
  virtual_table/ob_session_variables.cpp
  virtual_table/ob_show_create_catalog.cpp
  virtual_table/ob_show_create_database.cpp
  virtual_table/ob_show_create_location.cpp
  virtual_table/ob_show_create_procedure.cpp
  virtual_table/ob_show_create_table.cpp
  virtual_table/ob_show_create_tablegroup.cpp
  virtual_table/ob_show_create_trigger.cpp
  virtual_table/ob_show_database_status.cpp
  virtual_table/ob_show_grants.cpp
  virtual_table/ob_show_processlist.cpp
  virtual_table/ob_show_tenant_status.cpp
  virtual_table/ob_table_columns.cpp
  virtual_table/ob_table_index.cpp
  virtual_table/ob_tenant_all_tables.cpp
  virtual_table/ob_tenant_show_tables.cpp
  virtual_table/ob_tenant_virtual_charset.cpp
  virtual_table/ob_tenant_virtual_privilege.cpp
  virtual_table/ob_tenant_virtual_collation.cpp
  virtual_table/ob_tenant_virtual_concurrent_limit_sql.cpp
  virtual_table/ob_tenant_virtual_current_tenant.cpp
  virtual_table/ob_tenant_virtual_get_object_definition.cpp
  virtual_table/ob_tenant_virtual_outline.cpp
  virtual_table/ob_tenant_virtual_warning.cpp
  virtual_table/ob_virtual_data_access_service.cpp
  virtual_table/ob_virtual_open_cursor_table.cpp
  virtual_table/ob_virtual_sql_monitor.cpp
  virtual_table/ob_virtual_sql_monitor_statname.cpp
  virtual_table/ob_virtual_sql_plan_monitor.cpp
  virtual_table/ob_virtual_sql_plan_statistics.cpp
  virtual_table/ob_virtual_table_iterator.cpp
  virtual_table/ob_virtual_table_iterator_factory.cpp
  virtual_table/ob_virtual_table_projector.cpp
  virtual_table/ob_virtual_table_scanner_iterator.cpp
  virtual_table/ob_all_virtual_dml_stats.cpp
  virtual_table/ob_virtual_show_trace.cpp
  virtual_table/ob_all_virtual_sql_plan.cpp
  virtual_table/ob_all_virtual_opt_stat_gather_monitor.cpp
  virtual_table/ob_all_virtual_thread.cpp
  virtual_table/ob_all_virtual_tenant_resource_limit.cpp
  virtual_table/ob_all_virtual_tenant_resource_limit_detail.cpp
  virtual_table/ob_all_virtual_activity_metrics.cpp
  virtual_table/ob_all_virtual_nic_info.cpp
  virtual_table/ob_all_virtual_sys_variable_default_value.cpp
  virtual_table/ob_all_virtual_tenant_scheduler_running_job.cpp
  virtual_table/ob_all_virtual_compatibility_control.cpp
  virtual_table/ob_all_virtual_vector_index_info.cpp
  virtual_table/ob_all_virtual_session_ps_info.cpp
  virtual_table/ob_all_virtual_sql_stat.cpp
  virtual_table/ob_all_virtual_tmp_file.cpp
  virtual_table/ob_all_virtual_plugin_info.cpp
  virtual_table/ob_all_virtual_change_stream_refresh_stat.cpp
  virtual_table/ob_tenant_show_catalog_databases.cpp
  virtual_table/ob_all_virtual_tenant_vector_mem_info.cpp
  virtual_table/ob_all_virtual_ccl_status.cpp
)

ob_server_add_target(ob_server)


add_library(oceanbase_static
  STATIC
  EXCLUDE_FROM_ALL
  ${CMAKE_CURRENT_BINARY_DIR}/ob_version.cpp)

target_link_libraries(oceanbase_static
  PUBLIC ob_base "${ob_objects}" ob_sql_static ob_storage_static ob_share_static oblib ob_plugin)

if (OB_GPERF_MODE)
  target_link_libraries(oceanbase_static
  PRIVATE libprofiler.a)
endif()

if (OB_SO_CACHE)
  add_library(oceanbase SHARED IMPORTED GLOBAL)
  set_target_properties(oceanbase PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/liboceanbase.so")
  target_link_libraries(oceanbase
    INTERFACE ob_base
    ${STDC_SHARED_LIB})
else()
  add_library(oceanbase
    SHARED
    EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_BINARY_DIR}/ob_version.cpp)
  target_link_libraries(oceanbase
    PUBLIC ob_base
    INTERFACE
    ${STDC_SHARED_LIB}
    PRIVATE
    $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,-z,notext>
    $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--whole-archive>
    "${ob_objects}" "${oblib_object_libraries}" "${oblib_extra_objects}"
    ob_sql_static
    ob_storage_static
    ob_share_static
    ob_plugin
    $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--no-whole-archive>
    $<$<PLATFORM_ID:Linux>:-static-libgcc>
    $<$<PLATFORM_ID:Linux>:-static-libstdc++>)
endif()

if(STDC_SHARED_LIB)
  get_filename_component(STDC_SHARED_LIB_DIR ${STDC_SHARED_LIB} DIRECTORY)
  target_link_options(oceanbase INTERFACE "-Wl,-rpath=${STDC_SHARED_LIB_DIR}")
endif()

if (NOT OB_USE_ASAN)
  # On macOS, malloc_hook conflicts with linked LLVM
  # LLVM's global constructors run before main() and use hooked malloc,
  # but free with system free, causing crashes
  if (NOT APPLE AND NOT WIN32 AND NOT ANDROID)
    set(link_malloc_hook malloc_hook)
  else()
    message(STATUS "Disabling malloc_hook on macOS/Windows/Android")
    set(link_malloc_hook "")
  endif()
endif()

if(WIN32)
  add_library(ob_main OBJECT main.cpp ob_command_line_parser.cpp ob_win_service.cpp)
else()
  add_library(ob_main OBJECT main.cpp ob_command_line_parser.cpp)
endif()
config_ccls_flag(ob_main)
target_link_libraries(ob_main PUBLIC ob_base)
add_executable(seekdb)
if(WIN32)
  # Allow duplicate symbols (weak/selectany ODR) - linker picks first definition
  target_link_options(seekdb PRIVATE /FORCE:MULTIPLE)
  # Link compiler-rt builtins for __udivti3 (128-bit int div)
  get_filename_component(CLANG_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
  get_filename_component(LLVM_ROOT ${CLANG_BIN_DIR} DIRECTORY)
  file(GLOB CLANG_RT_BUILTINS "${LLVM_ROOT}/lib/clang/*/lib/windows/clang_rt.builtins-x86_64.lib")
  list(GET CLANG_RT_BUILTINS 0 CLANG_RT_BUILTINS_LIB)
  if(CLANG_RT_BUILTINS_LIB)
    target_link_libraries(seekdb PRIVATE ${CLANG_RT_BUILTINS_LIB})
    message(STATUS "Linking compiler-rt builtins: ${CLANG_RT_BUILTINS_LIB}")
  else()
    message(WARNING "clang_rt.builtins-x86_64.lib not found, __udivti3 may be undefined")
  endif()
  # oblib_add_extra_objects creates separate .lib files not merged into oblib.lib on Windows
  target_link_libraries(seekdb PRIVATE
    ${CMAKE_BINARY_DIR}/deps/oblib/src/lib/compress/zstd_1_3_8/zstd_1_3_8_objs.lib)
  # Itanium ABI _Unwind_* stubs (Windows uses SEH; PL unwinding non-functional)
  target_sources(seekdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/win32_unwind_stubs.c)
endif()
target_link_libraries(seekdb
  PRIVATE
  $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<NOT:$<PLATFORM_ID:Windows>>>:-Wl,--start-group>
  ob_main
  $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<NOT:$<PLATFORM_ID:Windows>>>:ob_malloc>
  oceanbase_static
  ob_sql_static
  ob_storage_static
  ob_share_static
  $<$<PLATFORM_ID:Windows>:ob_malloc>
  $<$<PLATFORM_ID:Windows>:synchronization>
  $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<NOT:$<PLATFORM_ID:Windows>>>:-Wl,--end-group>
  $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<NOT:$<PLATFORM_ID:Windows>>>:-Wl,--whole-archive>
  ob_plugin_export_static
  $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<NOT:$<PLATFORM_ID:Windows>>>:-Wl,--no-whole-archive>
  $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<NOT:$<PLATFORM_ID:Windows>>>:-Wl,--export-dynamic-symbol=obp_*>
  $<$<PLATFORM_ID:Linux>:-static-libgcc>
  $<$<PLATFORM_ID:Linux>:-static-libstdc++>
  ${link_malloc_hook}
)

if(WIN32)
  execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_REVISION
    ERROR_QUIET)
  execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_BRANCH
    ERROR_QUIET)
else()
  execute_process(
    COMMAND sh -c "cd ${CMAKE_SOURCE_DIR} && git rev-parse HEAD"
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_REVISION)
  execute_process(
    COMMAND sh -c "cd ${CMAKE_SOURCE_DIR} && git rev-parse --abbrev-ref HEAD"
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_BRANCH)
endif()
if (NOT CMAKE_BUILD_TYPE)
  set(BUILD_FLAGS "default" CACHE INTERNAL "build flags")
else()
  set(BUILD_FLAGS "${CMAKE_BUILD_TYPE}" CACHE INTERNAL "build flags")
endif()

# Link straight to `seekdb` (no post-link rename). The old design linked an
# intermediate observer binary then `mv`-ed it to `seekdb`, deleting the link
# output every build and forcing a full relink on every incremental make.

# ── Bundle third-party runtime DLLs next to seekdb.exe in the build tree ──
# Without this, running build/.../seekdb.exe directly requires the user's
# PATH to include ${OB_VCPKG_DIR}/bin etc. After this POST_BUILD step the
# build tree is self-contained for local smoke tests (--version, unit
# launches, debugger runs). Install/package bundling is still handled
# separately by cmake/Pack.cmake at cpack time.
#
# Attached to the `seekdb` target (not `observer`) so that:
#   * `cmake --build . --target seekdb` alone produces a runnable exe
#   * `observer` (which DEPENDS seekdb) still triggers bundling transitively
#   * bundling runs only once per build, not once per downstream target
if(WIN32)
  add_custom_command(TARGET seekdb POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DEXE=${CMAKE_BINARY_DIR}/src/observer/seekdb.exe"
      "-DOUT_DIR=${CMAKE_BINARY_DIR}/src/observer"
      "-DSEARCH_DIRS=${OB_VCPKG_DIR}/bin;${OB_VSAG_DIR}/bin"
      -P "${CMAKE_SOURCE_DIR}/cmake/BundleRuntimeDllsWindows.cmake"
    COMMENT "Bundling runtime DLLs next to seekdb.exe"
    VERBATIM)
endif()

if(WIN32)
  add_custom_target(observer ALL
    DEPENDS seekdb
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/src/observer/seekdb.exe
        ${CMAKE_BINARY_DIR}/src/observer/observer.exe
    COMMENT "Copying seekdb to observer"
  )
else()
  add_custom_target(observer ALL
    DEPENDS seekdb
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/src/observer/seekdb
        ${CMAKE_BINARY_DIR}/src/observer/observer
    COMMENT "Creating symbol link observer to seekdb"
  )
endif()

configure_file(../share/ob_version.cpp.in ob_version.cpp)
