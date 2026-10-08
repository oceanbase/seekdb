# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
include_guard(GLOBAL)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include("${CMAKE_CURRENT_LIST_DIR}/ServerDev.cmake")

# The root source-boundary target is declared after plugin subdirectories.
# Wire it only after root configuration, just like the managed C++ plugins.
function(_seekdb_wire_rust_plugin_boundaries)
  if(TARGET plugin_boundary_check)
    get_property(_targets GLOBAL PROPERTY SEEKDB_RUST_PLUGIN_TARGETS)
    foreach(_target IN LISTS _targets)
      add_dependencies(${_target} plugin_boundary_check)
    endforeach()
  endif()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _seekdb_wire_rust_plugin_boundaries)

# Rust plugins are independent cdylibs, never host staticlibs. Server-dev uses
# the same SDK and version-bound C bridges, not a Rust copy of the host runtime.
# Cargo path dependencies and final binary exports/imports are audited. This
# does not sandbox arbitrary registry crates or their native build scripts.
function(seekdb_add_rust_plugin target)
  cmake_parse_arguments(RPLUGIN "NO_INSTALL;STANDALONE" "LIBRARY_NAME;MANIFEST;SERVER_DEV_HOST" "" ${ARGN})
  file(REAL_PATH "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." _repo)
  file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" _callsite)
  string(FIND "${_callsite}" "${_repo}/plugins/" _callsite_prefix)
  if(NOT _callsite_prefix EQUAL 0)
    # Explicit standalone public profile: an external project may use this
    # helper without copying host sources or weakening Cargo/binary audits.
    # Do not allow core source subdirectories to opt themselves into this path.
    file(REAL_PATH "${CMAKE_SOURCE_DIR}" _standalone_root)
    string(FIND "${_callsite}" "${_repo}/" _inside_repo)
    if(NOT RPLUGIN_STANDALONE OR NOT _callsite STREQUAL _standalone_root OR
       _callsite STREQUAL _repo OR _inside_repo EQUAL 0)
      message(FATAL_ERROR "Rust plugin must be inside plugins/ or an explicit external STANDALONE project root")
    endif()
  endif()
  if(_callsite_prefix EQUAL 0 AND COMMAND _seekdb_require_plugin_callsite)
    _seekdb_require_plugin_callsite("${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  if(RPLUGIN_UNPARSED_ARGUMENTS OR NOT RPLUGIN_LIBRARY_NAME MATCHES "^[a-zA-Z_][a-zA-Z0-9_]*$")
    message(FATAL_ERROR "seekdb_add_rust_plugin requires a valid LIBRARY_NAME")
  endif()
  file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" _root)
  if(NOT EXISTS "${_root}/Cargo.toml" OR NOT EXISTS "${RPLUGIN_MANIFEST}")
    message(FATAL_ERROR "Rust plugin requires Cargo.toml and a package MANIFEST")
  endif()
  file(REAL_PATH "${RPLUGIN_MANIFEST}" _manifest)
  string(FIND "${_manifest}" "${_root}/" _manifest_prefix)
  if(NOT _manifest_prefix EQUAL 0)
    message(FATAL_ERROR "Rust plugin manifest escapes its plugin tree")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_manifest}" "${_repo}/cmake/plugin_profile.py")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${_repo}/cmake/plugin_profile.py"
    --manifest "${_manifest}" --source "${_repo}"
    RESULT_VARIABLE _profile_result OUTPUT_VARIABLE _profile_json ERROR_VARIABLE _profile_error)
  if(NOT _profile_result EQUAL 0)
    message(FATAL_ERROR "Rust plugin profile: ${_profile_error}")
  endif()
  string(JSON _profile GET "${_profile_json}" profile)
  string(JSON _headers LENGTH "${_profile_json}" headers)
  if(_headers GREATER 0)
    message(FATAL_ERROR "Rust plugins use version-bound C bridges; server_headers require a separately managed C++ adapter")
  endif()
  if(RPLUGIN_SERVER_DEV_HOST AND NOT _profile STREQUAL "server-dev")
    message(FATAL_ERROR "SERVER_DEV_HOST requires api_profile=server-dev")
  endif()
  if(NOT CARGO)
    find_program(CARGO cargo REQUIRED)
  endif()
  set(_sdk "${_repo}/rust/extension-sdk")
  set(_target_dir "${CMAKE_CURRENT_BINARY_DIR}/cargo-target")
  if(WIN32)
    set(_file "${RPLUGIN_LIBRARY_NAME}.dll")
  elseif(APPLE)
    set(_file "lib${RPLUGIN_LIBRARY_NAME}.dylib")
  else()
    set(_file "lib${RPLUGIN_LIBRARY_NAME}.so")
  endif()
  set(_binary "${_target_dir}/release/${_file}")
  set(_link_flags)
  set(_audit_flags)
  set(_cargo_env)
  set(_contract_dependency)
  if(UNIX AND NOT APPLE)
    list(APPEND _link_flags "-C" "link-arg=-Wl,-z,defs")
  endif()
  if(_profile STREQUAL "server-dev")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
      message(FATAL_ERROR "Rust server-dev currently requires Linux")
    endif()
    if(RPLUGIN_SERVER_DEV_HOST)
      if(NOT IS_ABSOLUTE "${RPLUGIN_SERVER_DEV_HOST}" OR
         NOT EXISTS "${RPLUGIN_SERVER_DEV_HOST}" OR IS_DIRECTORY "${RPLUGIN_SERVER_DEV_HOST}")
        message(FATAL_ERROR "SERVER_DEV_HOST must name an existing absolute linked host executable")
      endif()
      file(REAL_PATH "${RPLUGIN_SERVER_DEV_HOST}" _host)
      set(_host_dependency "${_host}")
    elseif(RPLUGIN_STANDALONE)
      message(FATAL_ERROR "standalone server-dev requires SERVER_DEV_HOST")
    else()
      set(_host "$<TARGET_FILE:seekdb>")
      set(_host_dependency seekdb)
    endif()
    _seekdb_server_dev_contract_tool(_tool _tool_dependency)
    set(_contract "${CMAKE_CURRENT_BINARY_DIR}/server-dev-${target}/contract.rs")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/server-dev-${target}")
    add_custom_target("${target}_server_dev_contract"
      COMMAND "${CMAKE_COMMAND}" "-DTOOL=${_tool}" "-DHOST=${_host}" "-DOUTPUT=${_contract}" -DFORMAT=rust
        -P "${_repo}/cmake/GenerateServerDevContract.cmake"
      DEPENDS ${_host_dependency} "${_host}" ${_tool_dependency} "${_tool}"
        "${_repo}/cmake/GenerateServerDevContract.cmake"
      BYPRODUCTS "${_contract}"
      VERBATIM)
    set(_contract_dependency "${target}_server_dev_contract")
    set(_cargo_env "SEEKDB_SERVER_DEV_CONTRACT=${_contract}")
    string(JSON _exports LENGTH "${_profile_json}" exports)
    if(_exports GREATER 0)
      math(EXPR _last "${_exports} - 1")
      foreach(_index RANGE 0 ${_last})
        string(JSON _symbol GET "${_profile_json}" exports ${_index})
        list(APPEND _audit_flags --allow-export "${_symbol}")
      endforeach()
    endif()
  endif()
  # Always run the policy and binary gates, including after an earlier audit
  # failed with an already-created binary. Cargo owns incremental dependency
  # tracking (including transitive path crates/build.rs), not a partial glob.
  add_custom_target(${target}
    COMMAND "${Python3_EXECUTABLE}" "${_repo}/cmake/rust_plugin_boundary_check.py"
      --cargo "${CARGO}" --manifest "${_root}/Cargo.toml" --plugin-root "${_root}" --sdk-root "${_sdk}"
    COMMAND "${CMAKE_COMMAND}" -E env ${_cargo_env}
      "${CARGO}" rustc --offline --release --manifest-path "${_root}/Cargo.toml"
      --target-dir "${_target_dir}" -- ${_link_flags}
    COMMAND "${Python3_EXECUTABLE}" "${_repo}/cmake/plugin_binary_check.py"
      --binary "${_binary}" --nm "${CMAKE_NM}" ${_audit_flags}
    DEPENDS ${_contract_dependency}
    BYPRODUCTS "${_binary}"
    WORKING_DIRECTORY "${_repo}/rust"
    COMMENT "Building and auditing Rust plugin ${target}"
    VERBATIM)
  set_property(TARGET ${target} PROPERTY SEEKDB_RUST_PLUGIN_BINARY "${_binary}")
  set_property(GLOBAL APPEND PROPERTY SEEKDB_RUST_PLUGIN_TARGETS ${target})
  # cargo-seekdb uses the same declared paths as the audited build, not guessed
  # Cargo output names or a second manifest. This is a trusted build recipe,
  # not a signature or permission grant for deployment into a running server.
  set(_package_recipe "${CMAKE_BINARY_DIR}/seekdb-plugin-packages/${target}.cmake")
  file(GENERATE OUTPUT "${_package_recipe}" CONTENT
"if(NOT IS_ABSOLUTE \"\${SEEKDB_PLUGIN_PACKAGE_DIR}\" OR NOT IS_DIRECTORY \"\${SEEKDB_PLUGIN_PACKAGE_DIR}\")
  message(FATAL_ERROR \"package output must be an existing absolute staging directory\")
endif()
file(INSTALL DESTINATION \"\${SEEKDB_PLUGIN_PACKAGE_DIR}\" TYPE FILE FILES [==[${_binary}]==])
file(INSTALL DESTINATION \"\${SEEKDB_PLUGIN_PACKAGE_DIR}\" TYPE FILE RENAME plugin.toml FILES [==[${_manifest}]==])
")
  if(NOT RPLUGIN_NO_INSTALL)
    include(GNUInstallDirs)
    install(FILES "${_binary}" "${_manifest}"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/seekdb/plugins/${target}" COMPONENT plugins)
  endif()
endfunction()
