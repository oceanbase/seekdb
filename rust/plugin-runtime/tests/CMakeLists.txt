# Standalone integration test: real C++ registry + production Rust host archive.
cmake_minimum_required(VERSION 3.20)
project(SeekDBPluginRuntimeTests LANGUAGES C CXX)
enable_testing()
get_filename_component(SEEKDB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(RUST_WORKSPACE_DIR "${SEEKDB_SOURCE_DIR}/rust")
set(SEEKDB_ENABLE_EXPERIMENTAL_PLUGINS ON)
include("${SEEKDB_SOURCE_DIR}/cmake/Rust.cmake")
add_executable(plugin_runtime_bridge
  host_bridge.cpp
  "${SEEKDB_SOURCE_DIR}/src/share/plugin/ob_plugin_registry.cpp")
target_compile_features(plugin_runtime_bridge PRIVATE cxx_std_17)
target_include_directories(plugin_runtime_bridge PRIVATE
  "${SEEKDB_SOURCE_DIR}/include"
  "${SEEKDB_SOURCE_DIR}/src"
  "${SEEKDB_SOURCE_DIR}/src/oblib")
target_link_libraries(plugin_runtime_bridge PRIVATE seekdb_rust_host sql_nio)
add_test(NAME plugin_runtime_bridge COMMAND plugin_runtime_bridge)
set_tests_properties(plugin_runtime_bridge PROPERTIES TIMEOUT 30)

add_executable(plugin_hook_modes hook_modes.cpp)
target_compile_features(plugin_hook_modes PRIVATE cxx_std_17)
target_link_libraries(plugin_hook_modes PRIVATE seekdb_rust_host)
add_test(NAME plugin_hook_modes COMMAND plugin_hook_modes)
set_tests_properties(plugin_hook_modes PROPERTIES TIMEOUT 30)

add_executable(plugin_query_transaction query_transaction.cpp)
target_compile_features(plugin_query_transaction PRIVATE cxx_std_17)
target_link_libraries(plugin_query_transaction PRIVATE seekdb_rust_host)
add_test(NAME plugin_query_transaction COMMAND plugin_query_transaction)
set_tests_properties(plugin_query_transaction PROPERTIES TIMEOUT 30)

add_executable(plugin_loader_registration
  loader_registration.cpp
  "${SEEKDB_SOURCE_DIR}/src/share/plugin/ob_plugin_registry.cpp"
  "${SEEKDB_SOURCE_DIR}/src/share/plugin/ob_plugin_loader.cpp")
target_compile_features(plugin_loader_registration PRIVATE cxx_std_17)
target_include_directories(plugin_loader_registration PRIVATE
  "${SEEKDB_SOURCE_DIR}/include" "${SEEKDB_SOURCE_DIR}/src" "${SEEKDB_SOURCE_DIR}/src/oblib")
target_link_libraries(plugin_loader_registration PRIVATE seekdb_rust_host)
foreach(mode direct snapshot)
  add_library(sql_registration_${mode} MODULE
    "${SEEKDB_SOURCE_DIR}/plugins/sql_extension/seekdb_sql_extension_plugin.c")
  target_include_directories(sql_registration_${mode} PRIVATE "${SEEKDB_SOURCE_DIR}/include")
  set_target_properties(sql_registration_${mode} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
  if(mode STREQUAL "snapshot")
    target_compile_definitions(sql_registration_${mode} PRIVATE SEEKDB_SQL_SNAPSHOT_REGISTRATION=1)
  endif()
  add_dependencies(plugin_loader_registration sql_registration_${mode})
  add_test(NAME plugin_loader_registration_${mode}
    COMMAND plugin_loader_registration "$<TARGET_FILE_DIR:sql_registration_${mode}>"
      "$<TARGET_FILE_NAME:sql_registration_${mode}>")
  set_tests_properties(plugin_loader_registration_${mode} PROPERTIES TIMEOUT 30)
endforeach()

add_test(NAME plugin_host_memory
  COMMAND plugin_loader_registration "$<TARGET_FILE_DIR:sql_registration_direct>"
    "$<TARGET_FILE_NAME:sql_registration_direct>" memory)
set_tests_properties(plugin_host_memory PROPERTIES TIMEOUT 30)

add_library(registration_conflict MODULE
  "${SEEKDB_SOURCE_DIR}/plugins/reference/seekdb_reference_registration_conflict_plugin.c")
target_include_directories(registration_conflict PRIVATE "${SEEKDB_SOURCE_DIR}/include")
set_target_properties(registration_conflict PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
add_dependencies(plugin_loader_registration registration_conflict)
add_test(NAME plugin_loader_registration_services
  COMMAND plugin_loader_registration "$<TARGET_FILE_DIR:registration_conflict>"
    "$<TARGET_FILE_NAME:registration_conflict>" services)
set_tests_properties(plugin_loader_registration_services PROPERTIES TIMEOUT 30)

add_executable(plugin_registration_api registration_api.cpp
  "${SEEKDB_SOURCE_DIR}/src/share/plugin/ob_plugin_registry.cpp")
target_compile_features(plugin_registration_api PRIVATE cxx_std_17)
target_include_directories(plugin_registration_api PRIVATE
  "${SEEKDB_SOURCE_DIR}/include" "${SEEKDB_SOURCE_DIR}/src" "${SEEKDB_SOURCE_DIR}/src/oblib")
target_link_libraries(plugin_registration_api PRIVATE seekdb_rust_host)
add_test(NAME plugin_registration_api COMMAND plugin_registration_api)
set_tests_properties(plugin_registration_api PROPERTIES TIMEOUT 30)

add_executable(plugin_registry_resolution registry_resolution.cpp
  "${SEEKDB_SOURCE_DIR}/src/share/plugin/ob_plugin_registry.cpp")
target_compile_features(plugin_registry_resolution PRIVATE cxx_std_17)
target_include_directories(plugin_registry_resolution PRIVATE
  "${SEEKDB_SOURCE_DIR}/include" "${SEEKDB_SOURCE_DIR}/src" "${SEEKDB_SOURCE_DIR}/src/oblib")
target_link_libraries(plugin_registry_resolution PRIVATE seekdb_rust_host)
add_test(NAME plugin_registry_resolution COMMAND plugin_registry_resolution)
set_tests_properties(plugin_registry_resolution PROPERTIES TIMEOUT 30)

add_executable(plugin_native_loader native_loader.cpp)
target_compile_features(plugin_native_loader PRIVATE cxx_std_17)
target_link_libraries(plugin_native_loader PRIVATE seekdb_rust_host)
foreach(mode entry missing)
  add_library(native_fixture_${mode} MODULE native_fixture.c)
  set_target_properties(native_fixture_${mode} PROPERTIES C_VISIBILITY_PRESET hidden)
  if(mode STREQUAL "missing")
    target_compile_definitions(native_fixture_${mode} PRIVATE OMIT_ENTRY=1)
  endif()
  add_dependencies(plugin_native_loader native_fixture_${mode})
endforeach()
add_test(NAME plugin_native_loader COMMAND plugin_native_loader
  "$<TARGET_FILE:native_fixture_entry>" "$<TARGET_FILE:native_fixture_missing>")
set_tests_properties(plugin_native_loader PROPERTIES TIMEOUT 30)

# Compile the unmodified C/C++ GIS implementation, not a Rust replacement.
add_library(gis_registration MODULE
  "${SEEKDB_SOURCE_DIR}/plugins/gis/seekdb_gis_plugin.c"
  "${SEEKDB_SOURCE_DIR}/plugins/gis/geometry_engine.cpp")
target_compile_features(gis_registration PRIVATE cxx_std_17)
target_include_directories(gis_registration PRIVATE "${SEEKDB_SOURCE_DIR}/include")
set_target_properties(gis_registration PROPERTIES
  C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
add_dependencies(plugin_loader_registration gis_registration)
add_test(NAME plugin_loader_registration_gis COMMAND plugin_loader_registration
  "$<TARGET_FILE_DIR:gis_registration>" "$<TARGET_FILE_NAME:gis_registration>" gis)
set_tests_properties(plugin_loader_registration_gis PROPERTIES TIMEOUT 30)

add_executable(plugin_type_identity type_identity.cpp)
target_compile_features(plugin_type_identity PRIVATE cxx_std_17)
target_include_directories(plugin_type_identity PRIVATE
  "${SEEKDB_SOURCE_DIR}/include" "${SEEKDB_SOURCE_DIR}/src" "${SEEKDB_SOURCE_DIR}/src/oblib")
add_test(NAME plugin_type_identity COMMAND plugin_type_identity)
set_tests_properties(plugin_type_identity PROPERTIES TIMEOUT 30)

include("${SEEKDB_SOURCE_DIR}/cmake/RustPlugin.cmake")
add_subdirectory("${SEEKDB_SOURCE_DIR}/plugins/rust_text" "rust_text")
get_target_property(_rust_text_binary seekdb_rust_text_plugin SEEKDB_RUST_PLUGIN_BINARY)
get_filename_component(_rust_text_dir "${_rust_text_binary}" DIRECTORY)
get_filename_component(_rust_text_file "${_rust_text_binary}" NAME)
add_dependencies(plugin_loader_registration seekdb_rust_text_plugin)
add_test(NAME plugin_loader_registration_rust COMMAND plugin_loader_registration
  "${_rust_text_dir}" "${_rust_text_file}" rust)
set_tests_properties(plugin_loader_registration_rust PROPERTIES TIMEOUT 30)
add_test(NAME plugin_rust_host_memory COMMAND plugin_loader_registration
  "${_rust_text_dir}" "${_rust_text_file}" rust-memory)
set_tests_properties(plugin_rust_host_memory PROPERTIES TIMEOUT 30)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME plugin_server_dev_admission
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/server_dev_admission.py"
      --loader "$<TARGET_FILE:plugin_loader_registration>" --cc "${CMAKE_C_COMPILER}" --cargo "${CARGO}")
  set_tests_properties(plugin_server_dev_admission PROPERTIES TIMEOUT 240 LABELS "plugin_build_boundary")
  add_test(NAME plugin_server_dev_profile
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/server_dev_profile.py"
      --loader "$<TARGET_FILE:plugin_loader_registration>" --cargo "${CARGO}")
  set_tests_properties(plugin_server_dev_profile PROPERTIES TIMEOUT 240 LABELS "plugin_build_boundary")
  add_test(NAME plugin_candidate_rust
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/candidate_plugin.py"
      --loader "$<TARGET_FILE:plugin_loader_registration>" --cc "${CMAKE_C_COMPILER}")
  set_tests_properties(plugin_candidate_rust PROPERTIES TIMEOUT 240 LABELS "plugin_build_boundary")
  add_test(NAME plugin_custom_executor
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/custom_executor_plugin.py"
      --loader "$<TARGET_FILE:plugin_loader_registration>" --cc "${CMAKE_C_COMPILER}")
  set_tests_properties(plugin_custom_executor PROPERTIES TIMEOUT 180 LABELS "plugin_build_boundary")
  add_test(NAME rust_server_dev_package
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/rust_server_dev.py"
      --loader "$<TARGET_FILE:plugin_loader_registration>" --cargo "${CARGO}")
  set_tests_properties(rust_server_dev_package PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE LABELS "plugin_sdk;plugin_build_boundary")
endif()

add_test(NAME plugin_native_profiles
  COMMAND "${Python3_EXECUTABLE}" "${SEEKDB_SOURCE_DIR}/cmake/test_plugin_profile.py")
set_tests_properties(plugin_native_profiles PROPERTIES TIMEOUT 30 LABELS "plugin_build_boundary")

add_test(NAME rust_plugin_cargo_boundary
  COMMAND "${Python3_EXECUTABLE}" "${SEEKDB_SOURCE_DIR}/cmake/test_rust_plugin_boundary.py")
set_tests_properties(rust_plugin_cargo_boundary PROPERTIES TIMEOUT 30)

add_test(NAME rust_extension_sdk
  COMMAND "${CARGO}" test --manifest-path "${SEEKDB_SOURCE_DIR}/rust/extension-sdk/Cargo.toml" --offline)
set_tests_properties(rust_extension_sdk PROPERTIES
  WORKING_DIRECTORY "${RUST_WORKSPACE_DIR}" TIMEOUT 120 LABELS "plugin_sdk")

add_test(NAME cargo_seekdb
  COMMAND "${CARGO}" test --manifest-path "${SEEKDB_SOURCE_DIR}/rust/cargo-seekdb/Cargo.toml" --offline)
set_tests_properties(cargo_seekdb PROPERTIES
  WORKING_DIRECTORY "${RUST_WORKSPACE_DIR}" TIMEOUT 120 LABELS "plugin_sdk")
if(UNIX)
  add_test(NAME rust_plugin_scaffold
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/rust_scaffold.py"
      --cargo "${CARGO}" --loader "$<TARGET_FILE:plugin_loader_registration>")
  set_tests_properties(rust_plugin_scaffold PROPERTIES
    WORKING_DIRECTORY "${RUST_WORKSPACE_DIR}" TIMEOUT 240 RUN_SERIAL TRUE LABELS "plugin_sdk;plugin_build_boundary")
  get_target_property(_packaged_rust_binary seekdb_rust_text_plugin SEEKDB_RUST_PLUGIN_BINARY)
  add_test(NAME rust_plugin_package
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/rust_package.py"
      --cargo "${CARGO}" --build-dir "${CMAKE_BINARY_DIR}"
      --artifact "${_packaged_rust_binary}" --loader "$<TARGET_FILE:plugin_loader_registration>")
  set_tests_properties(rust_plugin_package PROPERTIES
    WORKING_DIRECTORY "${RUST_WORKSPACE_DIR}" TIMEOUT 180 RUN_SERIAL TRUE LABELS "plugin_sdk;plugin_build_boundary")
endif()

add_executable(plugin_object_catalog object_catalog.cpp)
target_compile_features(plugin_object_catalog PRIVATE cxx_std_17)
target_link_libraries(plugin_object_catalog PRIVATE seekdb_rust_host)
add_test(NAME plugin_object_catalog COMMAND plugin_object_catalog)
set_tests_properties(plugin_object_catalog PROPERTIES TIMEOUT 30)

add_executable(plugin_install_coordinator install_coordinator.cpp)
target_compile_features(plugin_install_coordinator PRIVATE cxx_std_17)
target_link_libraries(plugin_install_coordinator PRIVATE seekdb_rust_host)
add_test(NAME plugin_install_coordinator COMMAND plugin_install_coordinator)
set_tests_properties(plugin_install_coordinator PROPERTIES TIMEOUT 30)

add_test(NAME generated_schema_inventory
  COMMAND "${Python3_EXECUTABLE}" "${SEEKDB_SOURCE_DIR}/tools/cmake/test_generated_schema_inventory.py")
set_tests_properties(generated_schema_inventory PROPERTIES TIMEOUT 30)

add_test(NAME plugin_sdk_install
  COMMAND "${Python3_EXECUTABLE}" "${SEEKDB_SOURCE_DIR}/cmake/test_plugin_sdk_install.py"
    --cc "${CMAKE_C_COMPILER}" --cxx "${CMAKE_CXX_COMPILER}")
set_tests_properties(plugin_sdk_install PROPERTIES TIMEOUT 240 LABELS "plugin_build_boundary")

add_test(NAME sql_source_inventory
  COMMAND "${Python3_EXECUTABLE}" "${SEEKDB_SOURCE_DIR}/tools/cmake/test_sql_source_inventory.py")
set_tests_properties(sql_source_inventory PROPERTIES TIMEOUT 30)

add_test(NAME sql_plugin_profiles
  COMMAND "${Python3_EXECUTABLE}" "${SEEKDB_SOURCE_DIR}/tools/cmake/test_sql_plugin_profiles.py")
# Four independent configure-only projects, each with its own 45-second limit.
# This checks conditional ownership, not four fully compiled server variants.
set_tests_properties(sql_plugin_profiles PROPERTIES TIMEOUT 240)

add_test(NAME plugin_kernel_build_gate
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_kernel_build_gate.py")
set_tests_properties(plugin_kernel_build_gate PROPERTIES TIMEOUT 30)

# Checks acceptance-runner safeguards without a server/PyMySQL. This is NOT the
# opt-in query_catalog_server.py database regression and must not count as one.
add_test(NAME plugin_query_catalog_runner
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_query_catalog_server.py")
set_tests_properties(plugin_query_catalog_runner PROPERTIES TIMEOUT 30 LABELS "plugin_test_harness")
add_test(NAME plugin_native_extension_runner
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_native_extension_server.py")
set_tests_properties(plugin_native_extension_runner PROPERTIES TIMEOUT 30 LABELS "plugin_test_harness")
add_test(NAME plugin_gis_extension_runner
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_gis_extension_server.py")
set_tests_properties(plugin_gis_extension_runner PROPERTIES TIMEOUT 30 LABELS "plugin_test_harness")
add_test(NAME plugin_gis_declaration_inventory
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_gis_declarations.py")
set_tests_properties(plugin_gis_declaration_inventory PROPERTIES TIMEOUT 30 LABELS "plugin_test_harness")
set_tests_properties(generated_schema_inventory sql_source_inventory
  sql_plugin_profiles plugin_kernel_build_gate rust_plugin_cargo_boundary
  PROPERTIES LABELS "plugin_build_boundary")

add_executable(plugin_package_source package_source.cpp
  "${SEEKDB_SOURCE_DIR}/src/share/plugin/extension_package.cpp")
target_compile_features(plugin_package_source PRIVATE cxx_std_17)
target_include_directories(plugin_package_source PRIVATE
  "${SEEKDB_SOURCE_DIR}/src" "${SEEKDB_SOURCE_DIR}/src/oblib")
target_link_libraries(plugin_package_source PRIVATE seekdb_rust_host)
if (UNIX)
  add_test(NAME plugin_package_source
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/package_source.py"
      "$<TARGET_FILE:plugin_package_source>" "${SEEKDB_SOURCE_DIR}/plugins/sql_packages")
  set_tests_properties(plugin_package_source PROPERTIES TIMEOUT 30)
  add_test(NAME plugin_package_layout
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/package_layout.py"
      "$<TARGET_FILE:plugin_package_source>" "${SEEKDB_SOURCE_DIR}/plugins/sql_packages" "${CMAKE_COMMAND}")
  set_tests_properties(plugin_package_layout PROPERTIES TIMEOUT 60)
endif()
