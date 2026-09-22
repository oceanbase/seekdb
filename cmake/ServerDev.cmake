# Version-bound in-tree C++ plugin build profile. No core link dependency is
# inherited: only the host compiler context, declared headers, and a wrapper.
include_guard(GLOBAL)
function(_seekdb_server_dev_contract_tool output dependency)
  file(REAL_PATH "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." _repo)
  if(SEEKDB_SERVER_DEV_CONTRACT_TOOL)
    if(NOT EXISTS "${SEEKDB_SERVER_DEV_CONTRACT_TOOL}")
      message(FATAL_ERROR "SEEKDB_SERVER_DEV_CONTRACT_TOOL does not exist")
    endif()
    set(_tool "${SEEKDB_SERVER_DEV_CONTRACT_TOOL}")
    set(_dependency "${_tool}")
  else()
    find_program(_cargo cargo REQUIRED)
    set(_tool "${CMAKE_BINARY_DIR}/server-dev-tools/debug/examples/server_dev_contract")
    if(NOT TARGET seekdb_server_dev_contract_tool)
      file(GLOB_RECURSE _tool_sources CONFIGURE_DEPENDS "${_repo}/rust/plugin-runtime/src/*.rs")
      file(GLOB _workspace_manifests CONFIGURE_DEPENDS
        "${_repo}/rust/*/Cargo.toml" "${_repo}/rust/Cargo.lock" "${_repo}/rust/rust-toolchain*")
      add_custom_command(OUTPUT "${_tool}"
        COMMAND "${_cargo}" build --offline --manifest-path "${_repo}/rust/Cargo.toml"
          --target-dir "${CMAKE_BINARY_DIR}/server-dev-tools"
          -p seekdb-plugin-runtime --example server_dev_contract
        DEPENDS ${_tool_sources} ${_workspace_manifests} "${_repo}/rust/Cargo.toml"
          "${_repo}/rust/plugin-runtime/examples/server_dev_contract.rs"
        VERBATIM)
      add_custom_target(seekdb_server_dev_contract_tool DEPENDS "${_tool}")
    endif()
    set(_dependency seekdb_server_dev_contract_tool)
  endif()
  set(${output} "${_tool}" PARENT_SCOPE)
  set(${dependency} "${_dependency}" PARENT_SCOPE)
endfunction()

function(_seekdb_configure_server_dev target profile_json)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT TARGET seekdb OR NOT TARGET ob_sql)
    message(FATAL_ERROR "server-dev currently requires the Linux seekdb and ob_sql build targets")
  endif()
  get_target_property(_sources "${target}" SOURCES)
  foreach(_source IN LISTS _sources)
    if(NOT _source MATCHES "\\.(cpp|cc|cxx|h|hpp)$")
      message(FATAL_ERROR "server-dev currently compiles C++ sources/adapters: ${_source}")
    endif()
  endforeach()
  get_target_property(_plugin_binary_dir "${target}" BINARY_DIR)
  set(_generated "${_plugin_binary_dir}/server-dev-${target}")
  file(MAKE_DIRECTORY "${_generated}")
  configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ServerDevEntry.cpp.in"
    "${_generated}/entry.cpp" COPYONLY)
  set(_exports "seekdb_plugin_entry_v1;")
  string(JSON _count LENGTH "${profile_json}" exports)
  if(_count GREATER 0)
    math(EXPR _last "${_count} - 1")
    foreach(_i RANGE 0 ${_last})
      string(JSON _symbol GET "${profile_json}" exports ${_i})
      string(APPEND _exports " ${_symbol};")
    endforeach()
  endif()
  file(WRITE "${_generated}/exports.map" "{ global: ${_exports} local: *; };\n")

  _seekdb_server_dev_contract_tool(_tool _tool_dependency)
  add_custom_command(OUTPUT "${_generated}/server_dev_contract.h"
    COMMAND "${CMAKE_COMMAND}" "-DTOOL=${_tool}" "-DHOST=$<TARGET_FILE:seekdb>"
      "-DOUTPUT=${_generated}/server_dev_contract.h"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateServerDevContract.cmake"
    DEPENDS seekdb ${_tool_dependency} "${_tool}" "$<TARGET_FILE:seekdb>"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateServerDevContract.cmake"
    VERBATIM)
  add_custom_target("${target}_server_dev_contract" DEPENDS "${_generated}/server_dev_contract.h")
  add_dependencies("${target}" "${target}_server_dev_contract")

  set(_includes "${_generated}")
  set(_definitions "seekdb_plugin_entry_v1=seekdb_plugin_server_dev_entry_impl")
  set(_options "")
  get_target_property(_host_kind ob_sql TYPE)
  set(_host_property_prefix "")
  if(_host_kind STREQUAL "INTERFACE_LIBRARY")
    set(_host_property_prefix "INTERFACE_")
  endif()
  foreach(_property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
    # Evaluate the host's transitive compile requirements at generation time;
    # get_target_property alone misses headers/flags supplied by ob_base. This
    # does not add LINK_LIBRARIES or copy any of the host's object files.
    set(_value "$<TARGET_PROPERTY:ob_sql,${_host_property_prefix}${_property}>")
      if(_property STREQUAL "INCLUDE_DIRECTORIES")
        list(APPEND _includes ${_value})
      elseif(_property STREQUAL "COMPILE_DEFINITIONS")
        list(APPEND _definitions ${_value})
      else()
        list(APPEND _options ${_value})
      endif()
  endforeach()
  target_sources("${target}" PRIVATE "${_generated}/entry.cpp")
  target_include_directories("${target}" PRIVATE ${_includes})
  target_compile_definitions("${target}" PRIVATE ${_definitions})
  target_compile_options("${target}" PRIVATE ${_options})
  target_compile_features("${target}" PRIVATE cxx_std_20)
  target_link_options("${target}" PRIVATE "-Wl,--version-script=${_generated}/exports.map")
  # Snapshot the tool-added exceptions; end-of-configure validation still
  # rejects extra sources/options/includes and every core static link edge.
  set_property(GLOBAL APPEND PROPERTY SEEKDB_SERVER_DEV_TARGETS "${target}")
  set_property(TARGET "${target}" PROPERTY SEEKDB_SERVER_DEV_INCLUDE_DIRS "${_includes}")
  set_property(TARGET "${target}" PROPERTY SEEKDB_SERVER_DEV_DEFINITIONS "${_definitions}")
  set_property(TARGET "${target}" PROPERTY SEEKDB_SERVER_DEV_OPTIONS "${_options}")
  set_property(TARGET "${target}" PROPERTY SEEKDB_SERVER_DEV_SOURCE "${_generated}/entry.cpp")
  set_property(TARGET "${target}" PROPERTY SEEKDB_SERVER_DEV_EXPORT_MAP "${_generated}/exports.map")
endfunction()
