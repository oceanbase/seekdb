# This component is configured only by the product root through build.ps1.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR "Use the repository build.ps1 phase0 entry, not a standalone project")
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Phase 0 requires the product Windows x64 clang-cl configuration")
endif()
if(NOT TARGET oblib_base_without_pass)
  message(FATAL_ERROR "The product oblib configuration must be available before Phase 0")
endif()

# Resolve exactly the SQLite used by oblib; no alternate vendor or system fallback.
foreach(vendor_file include/sqlite3.h lib/sqlite3.lib bin/sqlite3.dll)
  if(NOT EXISTS "${OB_SQLITE_DIR}/${vendor_file}")
    message(FATAL_ERROR "Product SQLite input missing: ${OB_SQLITE_DIR}/${vendor_file}")
  endif()
endforeach()

# CMake owns manifest merging/embedding. Do not add manual MANIFESTINPUT options.
add_executable(sqlite_path_probe sqlite_path_probe.cpp phase0.manifest)
add_executable(path_fixture_test path_fixture_test.cpp)
add_executable(path_context_probe path_context_probe.cpp phase0.manifest)
foreach(probe_target sqlite_path_probe path_fixture_test path_context_probe)
  # Inherit actual product compile options, definitions, includes and vendor libs,
  # including /EHsc and the STL compatibility macro. Env.cmake owns standard/CRT.
  target_link_libraries(${probe_target} PRIVATE oblib_base_without_pass)
  set_target_properties(${probe_target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endforeach()
add_custom_target(windows_path_phase0 DEPENDS sqlite_path_probe path_fixture_test)
add_custom_target(windows_path_context_phase0 DEPENDS path_context_probe path_fixture_test)
add_executable(nio_path_probe nio_path_probe.cpp phase0.manifest
  "${CMAKE_SOURCE_DIR}/src/oblib/rpc/obmysql/ob_nio_abi_check.cpp")
target_link_libraries(nio_path_probe PRIVATE oblib_base_without_pass sql_nio)
set_target_properties(nio_path_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_executable(nio_tls_probe nio_tls_probe.cpp phase0.manifest
  "${CMAKE_SOURCE_DIR}/src/oblib/rpc/obmysql/ob_nio_abi_check.cpp")
target_link_libraries(nio_tls_probe PRIVATE oblib_base_without_pass sql_nio)
set_target_properties(nio_tls_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_target(windows_nio_path_phase0 DEPENDS nio_path_probe nio_tls_probe path_fixture_test)
# The existing fixture compiles, links and exercises STL and exception handling.
# build.ps1 runs it before reporting a successful Phase 0 build.
add_custom_command(TARGET sqlite_path_probe POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${OB_SQLITE_DIR}/bin/sqlite3.dll" "$<TARGET_FILE_DIR:sqlite_path_probe>"
  VERBATIM)

option(OB_BUILD_WINDOWS_NIO_ABI_MIX "Validate pinned old/new NIO object combinations" OFF)
if(OB_BUILD_WINDOWS_NIO_ABI_MIX)
  include("${CMAKE_CURRENT_LIST_DIR}/nio_abi_mix.cmake")
endif()

add_executable(sqlite_temp_probe sqlite_temp_probe.cpp phase0.manifest)
target_link_libraries(sqlite_temp_probe PRIVATE oblib_base_without_pass)
set_target_properties(sqlite_temp_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET sqlite_temp_probe POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${OB_SQLITE_DIR}/bin/sqlite3.dll" "$<TARGET_FILE_DIR:sqlite_temp_probe>" VERBATIM)
add_custom_target(windows_sqlite_temp_phase0 DEPENDS sqlite_temp_probe path_fixture_test)

# Exercise production normalization and directory operations with an allocator
# implementing the real ObIAllocator interface. No copy of the path algorithm.
add_executable(windows_file_path_test windows_file_path_test.cpp phase0.manifest
  "${CMAKE_SOURCE_DIR}/src/oblib/lib/file/windows_file_path.cpp"
  "${CMAKE_SOURCE_DIR}/src/oblib/lib/ob_abort.cpp"
  "${CMAKE_SOURCE_DIR}/src/oblib/lib/utility/ob_backtrace.cpp")
target_link_libraries(windows_file_path_test PRIVATE oblib_base_without_pass)
set_target_properties(windows_file_path_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Compile the production caller too; the full product link/startup is separate.
add_library(windows_file_utils_compile OBJECT
  "${CMAKE_SOURCE_DIR}/src/oblib/lib/file/file_directory_utils.cpp")
target_link_libraries(windows_file_utils_compile PRIVATE oblib_base_without_pass)
add_custom_target(windows_native_path DEPENDS windows_file_path_test windows_file_utils_compile path_fixture_test)

# Compile the actual product entry/parser/service objects, then exercise the
# production startup context and preflight without initializing a database.
add_executable(windows_startup_test windows_startup_test.cpp
  "${CMAKE_SOURCE_DIR}/src/observer/seekdb.manifest"
  "${CMAKE_SOURCE_DIR}/src/observer/windows_startup_context.cpp"
  "${CMAKE_SOURCE_DIR}/src/oblib/lib/file/windows_file_path.cpp"
  "${CMAKE_SOURCE_DIR}/src/oblib/lib/ob_abort.cpp"
  "${CMAKE_SOURCE_DIR}/src/oblib/lib/utility/ob_backtrace.cpp")
target_include_directories(windows_startup_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(windows_startup_test PRIVATE oblib_base_without_pass)
set_target_properties(windows_startup_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_library(windows_utility_compile OBJECT "${CMAKE_SOURCE_DIR}/src/oblib/lib/utility/utility.cpp")
target_link_libraries(windows_utility_compile PRIVATE oblib_base_without_pass)
add_custom_target(windows_native_startup DEPENDS windows_startup_test ob_main windows_utility_compile path_fixture_test)
option(OB_BUILD_WINDOWS_NATIVE_PRODUCT "Link seekdb for native CLI path validation" OFF)
if(OB_BUILD_WINDOWS_NATIVE_PRODUCT)
  add_dependencies(windows_native_startup seekdb)
endif()


# Compile the production log owner and its consumers while integrating paths.
add_library(windows_logging_compile OBJECT
  "${CMAKE_SOURCE_DIR}/src/oblib/lib/oblog/ob_async_log_struct.cpp"
  "${CMAKE_SOURCE_DIR}/src/oblib/lib/oblog/ob_log.cpp"
  "${CMAKE_SOURCE_DIR}/src/oblib/lib/oblog/ob_log_compressor.cpp")
target_link_libraries(windows_logging_compile PRIVATE oblib_base_without_pass)
add_dependencies(windows_native_startup windows_logging_compile)

# Compile the real server caller with the product base configuration.
add_library(windows_server_compile OBJECT
  "${CMAKE_SOURCE_DIR}/src/logservice/palf/palf_env_impl.cpp"
  "${CMAKE_SOURCE_DIR}/src/logservice/palf/log_reader.cpp"
  "${CMAKE_SOURCE_DIR}/src/logservice/palf/log_block_mgr.cpp"
  "${CMAKE_SOURCE_DIR}/src/logservice/palf/log_storage.cpp"
  "${CMAKE_SOURCE_DIR}/src/logservice/palf/palf_handle_impl.cpp"
  "${CMAKE_SOURCE_DIR}/src/logservice/palf/log_block_pool_interface.cpp"
  "${CMAKE_SOURCE_DIR}/src/logservice/ob_server_log_block_mgr.cpp"
  "${CMAKE_SOURCE_DIR}/src/logservice/palf/log_io_utils.cpp"
  "${CMAKE_SOURCE_DIR}/src/share/ob_local_device.cpp"
  "${CMAKE_SOURCE_DIR}/src/share/ob_io_device_helper.cpp"
  "${CMAKE_SOURCE_DIR}/src/observer/omt/ob_server_runtime.cpp"
  "${CMAKE_SOURCE_DIR}/src/observer/omt/ob_server_runtime_controller.cpp"
  "${CMAKE_SOURCE_DIR}/src/storage/ob_file_system_router.cpp"
  ${PROJECT_SOURCE_DIR}/src/share/ob_telemetry.cpp
  ${PROJECT_SOURCE_DIR}/src/observer/ob_service.cpp
  "${CMAKE_SOURCE_DIR}/src/observer/ob_server.cpp"
  "${CMAKE_SOURCE_DIR}/src/share/storage/ob_sqlite_connection.cpp"
  "${CMAKE_SOURCE_DIR}/src/share/storage/ob_sqlite_connection_pool.cpp"
  "${CMAKE_SOURCE_DIR}/src/observer/ob_srv_network_frame.cpp"
  "${CMAKE_SOURCE_DIR}/src/oblib/rpc/obmysql/ob_sql_nio_server.cpp")
target_link_libraries(windows_server_compile PRIVATE ob_base)
add_executable(windows_log_candidate_test windows_log_candidate_test.cpp)
target_link_libraries(windows_log_candidate_test PRIVATE oblib_base_without_pass)
set_target_properties(windows_log_candidate_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET windows_log_candidate_test POST_BUILD
  COMMAND "$<TARGET_FILE:windows_log_candidate_test>" VERBATIM)
add_dependencies(windows_native_startup windows_server_compile windows_log_candidate_test)

# This test links the production allocator and log implementation, not stubs.
option(OB_BUILD_WINDOWS_LOG_LIFECYCLE "Build real log file lifecycle test" OFF)
if(OB_BUILD_WINDOWS_LOG_LIFECYCLE)
  add_executable(windows_log_file_test windows_log_file_test.cpp phase0.manifest)
  target_link_libraries(windows_log_file_test PRIVATE oblib sql_nio synchronization
    "${CMAKE_BINARY_DIR}/src/oblib/lib/compress/zstd_1_3_8/zstd_1_3_8_objs.lib")
  target_link_options(windows_log_file_test PRIVATE /FORCE:MULTIPLE)
  set_target_properties(windows_log_file_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  add_dependencies(windows_native_startup windows_log_file_test)
endif()

option(OB_BUILD_WINDOWS_SQLITE_POOL "Build production SQLite pool path test" OFF)
if(OB_BUILD_WINDOWS_SQLITE_POOL)
  add_executable(windows_sqlite_pool_test windows_sqlite_pool_test.cpp phase0.manifest
    "${CMAKE_SOURCE_DIR}/src/share/storage/ob_sqlite_connection.cpp"
    "${CMAKE_SOURCE_DIR}/src/share/storage/ob_sqlite_connection_pool.cpp")
  target_link_libraries(windows_sqlite_pool_test PRIVATE ob_base oblib sql_nio synchronization
    "${CMAKE_BINARY_DIR}/src/oblib/lib/compress/zstd_1_3_8/zstd_1_3_8_objs.lib")
  target_link_options(windows_sqlite_pool_test PRIVATE /FORCE:MULTIPLE)
  set_target_properties(windows_sqlite_pool_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  add_custom_command(TARGET windows_sqlite_pool_test POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${OB_SQLITE_DIR}/bin/sqlite3.dll" "$<TARGET_FILE_DIR:windows_sqlite_pool_test>"
    VERBATIM)
  add_dependencies(windows_native_startup windows_sqlite_pool_test)
endif()

option(OB_BUILD_WINDOWS_DATA_VERSION "Build production data version path test" OFF)
if(OB_BUILD_WINDOWS_DATA_VERSION)
  add_executable(windows_data_version_test windows_data_version_test.cpp phase0.manifest)
  target_link_libraries(windows_data_version_test PRIVATE ob_base oblib sql_nio synchronization
    "${CMAKE_BINARY_DIR}/src/oblib/lib/compress/zstd_1_3_8/zstd_1_3_8_objs.lib")
  target_link_options(windows_data_version_test PRIVATE /FORCE:MULTIPLE)
  set_target_properties(windows_data_version_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  add_dependencies(windows_native_startup windows_data_version_test)
endif()

option(OB_BUILD_WINDOWS_ROUTER "Build production router path test" OFF)
if(OB_BUILD_WINDOWS_ROUTER)
  add_executable(windows_router_test windows_router_test.cpp "${CMAKE_SOURCE_DIR}/src/storage/ob_file_system_router.cpp" phase0.manifest)
  target_link_libraries(windows_router_test PRIVATE ob_base oblib sql_nio synchronization
    "${CMAKE_BINARY_DIR}/src/oblib/lib/compress/zstd_1_3_8/zstd_1_3_8_objs.lib")
  target_link_options(windows_router_test PRIVATE /FORCE:MULTIPLE)
  set_target_properties(windows_router_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  add_dependencies(windows_native_startup windows_router_test)
endif()

option(OB_BUILD_WINDOWS_BLOCK_FILE "Build production block file path test" OFF)
if(OB_BUILD_WINDOWS_BLOCK_FILE)
  # The real block pool is integrated with the log service. Use the product
  # library graph instead of maintaining a partial list of production objects.
  add_executable(windows_block_file_test windows_block_file_test.cpp phase0.manifest)
  add_executable(windows_instance_files_test windows_instance_files_test.cpp phase0.manifest)
  foreach(file_test windows_block_file_test windows_instance_files_test)
    target_link_libraries(${file_test} PRIVATE
      oceanbase_static ob_sql_static ob_storage_static ob_share_static
      sql_nio ob_malloc synchronization
      "${CMAKE_BINARY_DIR}/src/oblib/lib/compress/zstd_1_3_8/zstd_1_3_8_objs.lib")
    # Reuse the runtime selected by the product's compiler configuration. Its
    # variable is directory-scoped and is not inherited by this sibling target.
    get_directory_property(block_test_builtins
      DIRECTORY "${CMAKE_SOURCE_DIR}/src/observer" DEFINITION CLANG_RT_BUILTINS_LIB)
    if(block_test_builtins)
      target_link_libraries(${file_test} PRIVATE "${block_test_builtins}")
    endif()
    target_compile_options(${file_test} PRIVATE /FIshare/ob_errno.h)
    target_link_options(${file_test} PRIVATE /FORCE:MULTIPLE /errorlimit:0)
    set_target_properties(${file_test} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  endforeach()
  add_dependencies(windows_native_startup windows_block_file_test)
endif()

option(OB_BUILD_WINDOWS_TELEMETRY "Build production telemetry path test" OFF)
if(OB_BUILD_WINDOWS_TELEMETRY)
  add_executable(windows_telemetry_test windows_telemetry_test.cpp phase0.manifest
    "${CMAKE_SOURCE_DIR}/src/share/config/ob_server_config.cpp"
    "${CMAKE_SOURCE_DIR}/src/share/config/ob_common_config.cpp"
    "${CMAKE_SOURCE_DIR}/src/share/parameter/ob_parameter_attr.cpp"
    "${CMAKE_SOURCE_DIR}/src/share/ob_task_define.cpp"
    "${CMAKE_SOURCE_DIR}/src/share/ob_version_parser.cpp"
    "${CMAKE_SOURCE_DIR}/src/share/ob_errno.cpp"
    "${CMAKE_SOURCE_DIR}/src/share/config/ob_config.cpp"
    "${CMAKE_SOURCE_DIR}/src/share/config/ob_system_config.cpp"
    "${CMAKE_SOURCE_DIR}/src/share/config/ob_config_helper.cpp"
    "${CMAKE_SOURCE_DIR}/src/share/ob_encryption_util.cpp")
  target_link_libraries(windows_telemetry_test PRIVATE ob_base oblib sql_nio synchronization
    "${CMAKE_BINARY_DIR}/src/oblib/lib/compress/zstd_1_3_8/zstd_1_3_8_objs.lib")
  target_compile_options(windows_telemetry_test PRIVATE /FIshare/ob_errno.h)
  target_link_options(windows_telemetry_test PRIVATE /FORCE:MULTIPLE)
  set_target_properties(windows_telemetry_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  add_dependencies(windows_native_startup windows_telemetry_test)
endif()

option(OB_BUILD_WINDOWS_SQL_FACTORY "Verify SQL proxy factory override and fallback" OFF)
if(OB_BUILD_WINDOWS_SQL_FACTORY)
  foreach(mode fallback override)
    add_executable(windows_sql_factory_${mode}_test windows_sql_factory_test.cpp)
    target_link_libraries(windows_sql_factory_${mode}_test PRIVATE ob_base oblib sql_nio synchronization
      "${CMAKE_BINARY_DIR}/src/oblib/lib/compress/zstd_1_3_8/zstd_1_3_8_objs.lib")
    target_link_options(windows_sql_factory_${mode}_test PRIVATE /FORCE:MULTIPLE)
    set_target_properties(windows_sql_factory_${mode}_test PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    if(mode STREQUAL "override")
      target_compile_definitions(windows_sql_factory_${mode}_test PRIVATE TEST_SQL_FACTORY_OVERRIDE)
    endif()
    add_custom_command(TARGET windows_sql_factory_${mode}_test POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:seekdb>;$ENV{PATH}"
        "$<TARGET_FILE:windows_sql_factory_${mode}_test>" VERBATIM)
    add_dependencies(windows_native_startup windows_sql_factory_${mode}_test)
  endforeach()
endif()

option(OB_BUILD_WINDOWS_SLOG_READER "Build production slog reader long path test" OFF)
if(OB_BUILD_WINDOWS_SLOG_READER)
  add_executable(windows_slog_reader_test windows_slog_reader_test.cpp phase0.manifest)
  target_link_libraries(windows_slog_reader_test PRIVATE
    oceanbase_static ob_sql_static ob_storage_static ob_share_static
    sql_nio ob_malloc synchronization
    "${CMAKE_BINARY_DIR}/src/oblib/lib/compress/zstd_1_3_8/zstd_1_3_8_objs.lib")
  get_directory_property(slog_reader_builtins
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/observer" DEFINITION CLANG_RT_BUILTINS_LIB)
  if(slog_reader_builtins)
    target_link_libraries(windows_slog_reader_test PRIVATE "${slog_reader_builtins}")
  endif()
  target_compile_options(windows_slog_reader_test PRIVATE /FIshare/ob_errno.h)
  target_link_options(windows_slog_reader_test PRIVATE /FORCE:MULTIPLE)
  set_target_properties(windows_slog_reader_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  add_dependencies(windows_native_startup windows_slog_reader_test)
endif()
