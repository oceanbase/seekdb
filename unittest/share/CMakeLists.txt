storage_unittest(test_scanner)
#ob_unittest(test_log_file_store redolog/test_log_file_store.cpp)
storage_unittest(test_log_file_handler redolog/test_log_file_handler.cpp)
storage_unittest(test_obj_cast)
storage_unittest(test_datum_cmp)
#ob_unittest(test_national_encrypt_algorithm)
#ob_unittest(test_kv_storecache)
#ob_unittest(test_async_task_queue)
ob_unittest(test_dml_sql_splicer_batch)
#ob_unittest(test_ob_config)
#ob_unittest(test_global_stat_proxy)
#ob_unittest(##test_reentrant_thread)
#ob_unittest(test_tenant_mgr)
#ob_unittest(test_ob_column_stat)
#ob_unittest(test_ob_log)
#ob_unittest(test_truncated_string)
#ob_unittest(test_latch)
ob_unittest(test_debug_sync)
#ob_unittest(test_time_zone_info_manager)
#ob_unittest(test_rpc_struct)
storage_unittest(test_ob_function)
storage_unittest(test_ob_guard)
#ob_unittest(test_ob_occam_time_guard)
storage_unittest(test_ob_occam_timer)
storage_unittest(test_ob_occam_thread_pool)
ob_unittest(test_ob_future)
ob_unittest(test_ob_occam_time_guard)
ob_unittest(test_version_parser)
if (SEEKDB_ENABLE_EXPERIMENTAL_PLUGINS)
  ob_unittest(test_plugin_registry)
  ob_unittest(test_plugin_loader)
  ob_unittest(test_plugin_catalog)
  ob_unittest(test_gis_plugin)
  target_link_libraries(test_plugin_registry PRIVATE seekdb_plugin_sdk)
  target_link_libraries(test_plugin_catalog PRIVATE seekdb_plugin_sdk)
  add_dependencies(test_gis_plugin seekdb_gis_plugin)
  target_link_libraries(test_gis_plugin PRIVATE seekdb_plugin_sdk ${CMAKE_DL_LIBS})
  target_compile_definitions(test_gis_plugin PRIVATE
    SEEKDB_TEST_GIS_PLUGIN_DIR="$<TARGET_FILE_DIR:seekdb_gis_plugin>"
    SEEKDB_TEST_GIS_PLUGIN_FILE="$<TARGET_FILE_NAME:seekdb_gis_plugin>")

  # The GIS ABI test only exercises the public plugin SDK and the dynamic
  # loader.  Keep a standalone variant so it can run in core-only builds
  # without pulling the multi-gigabyte observer shared library into the link.
  add_executable(gis_plugin_abi_smoke test_gis_plugin.cpp)
  find_package(Threads REQUIRED)
  add_test(NAME gis_plugin_abi_smoke COMMAND gis_plugin_abi_smoke)
  set_tests_properties(gis_plugin_abi_smoke PROPERTIES TIMEOUT 300)
  add_dependencies(gis_plugin_abi_smoke seekdb_gis_plugin)
  target_link_libraries(gis_plugin_abi_smoke PRIVATE
    seekdb_plugin_sdk gtest gmock Threads::Threads ${CMAKE_DL_LIBS})
  target_link_directories(gis_plugin_abi_smoke PRIVATE
    ${DEP_DIR}/lib64 ${DEP_DIR}/lib)
  target_include_directories(gis_plugin_abi_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/unittest
    ${CMAKE_SOURCE_DIR}/deps/oblib/unittest ${DEP_DIR}/include)
  target_compile_definitions(gis_plugin_abi_smoke PRIVATE
    SEEKDB_TEST_GIS_PLUGIN_DIR="$<TARGET_FILE_DIR:seekdb_gis_plugin>"
    SEEKDB_TEST_GIS_PLUGIN_FILE="$<TARGET_FILE_NAME:seekdb_gis_plugin>")
  add_dependencies(test_plugin_loader
    seekdb_reference_plugin seekdb_reference_blocked_plugin
    seekdb_reference_invalid_extensions_plugin
    seekdb_reference_invalid_manifest_plugin
    seekdb_reference_stop_blocked_plugin
    seekdb_reference_registration_conflict_plugin)
  target_link_libraries(test_plugin_loader PRIVATE seekdb_plugin_sdk)
  target_compile_definitions(test_plugin_loader PRIVATE
    SEEKDB_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:seekdb_reference_plugin>"
    SEEKDB_TEST_PLUGIN_FILE="$<TARGET_FILE_NAME:seekdb_reference_plugin>"
    SEEKDB_TEST_BLOCKED_PLUGIN_FILE="$<TARGET_FILE_NAME:seekdb_reference_blocked_plugin>"
    SEEKDB_TEST_INVALID_EXTENSIONS_PLUGIN_FILE="$<TARGET_FILE_NAME:seekdb_reference_invalid_extensions_plugin>"
    SEEKDB_TEST_INVALID_MANIFEST_PLUGIN_FILE="$<TARGET_FILE_NAME:seekdb_reference_invalid_manifest_plugin>"
    SEEKDB_TEST_STOP_BLOCKED_PLUGIN_FILE="$<TARGET_FILE_NAME:seekdb_reference_stop_blocked_plugin>"
    SEEKDB_TEST_REGISTRATION_CONFLICT_PLUGIN_FILE="$<TARGET_FILE_NAME:seekdb_reference_registration_conflict_plugin>")
endif()
ob_unittest(test_standby_source_util)
ob_unittest(test_scn)
ob_unittest(test_log_allocator)
ob_unittest(test_geo_bin)
ob_unittest(test_s2adapter)
ob_unittest(test_geo_common)
ob_unittest(test_wkt_parser)
ob_unittest(test_geo_3d)
ob_unittest(test_geo_tree)
ob_unittest(test_geo_srs)
ob_unittest(test_geo_srs_parser)
ob_unittest(test_geo_func_difference)
ob_unittest(test_geo_func_union)
ob_unittest(test_geo_func_box)
ob_unittest(test_throttling_utils)
ob_unittest(test_array_meta)
ob_unittest(test_map_meta)

ob_unittest(test_json_base)
ob_unittest(test_json_bin)
ob_unittest(test_json_path)
ob_unittest(test_json_schema)
ob_unittest(test_json_tree)

ob_unittest(test_text_analyzer text_analysis/test_text_analyzer.cpp)

add_subdirectory(allocator)
add_subdirectory(cache)
add_subdirectory(deadlock)
add_subdirectory(schema)
add_subdirectory(vector_index)
add_subdirectory(compaction)
add_subdirectory(scheduler)
