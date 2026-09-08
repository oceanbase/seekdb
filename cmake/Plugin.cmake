# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

include_guard(GLOBAL)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if (NOT TARGET seekdb_plugin_sdk)
  add_library(seekdb_plugin_sdk INTERFACE)
  add_library(seekdb::plugin_sdk ALIAS seekdb_plugin_sdk)
  target_include_directories(seekdb_plugin_sdk INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
  target_compile_features(seekdb_plugin_sdk INTERFACE c_std_99 cxx_std_11)
  set_target_properties(seekdb_plugin_sdk PROPERTIES EXPORT_NAME plugin_sdk)

  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)
  set(_seekdb_plugin_sdk_cmake_dir
    "${CMAKE_INSTALL_LIBDIR}/cmake/SeekDBPluginSDK")
  install(FILES
    "${PROJECT_SOURCE_DIR}/include/seekdb/plugin/seekdb_plugin_abi.h"
    "${PROJECT_SOURCE_DIR}/include/seekdb/plugin/extension_spi.h"
    "${PROJECT_SOURCE_DIR}/include/seekdb/plugin/execution_spi.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/seekdb/plugin"
    COMPONENT plugin-sdk)
  install(TARGETS seekdb_plugin_sdk
    EXPORT SeekDBPluginSDKTargets
    COMPONENT plugin-sdk)
  install(EXPORT SeekDBPluginSDKTargets
    FILE SeekDBPluginSDKTargets.cmake
    NAMESPACE seekdb::
    DESTINATION "${_seekdb_plugin_sdk_cmake_dir}"
    COMPONENT plugin-sdk)

  configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/PluginSDKConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SeekDBPluginSDKConfig.cmake"
    INSTALL_DESTINATION "${_seekdb_plugin_sdk_cmake_dir}")
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SeekDBPluginSDKConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    # R0 validates one SDK/server release as an exact combination.  Relaxing
    # this requires the R2 compatibility matrix and is not a CMake-only choice.
    COMPATIBILITY ExactVersion)
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SeekDBPluginSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SeekDBPluginSDKConfigVersion.cmake"
    DESTINATION "${_seekdb_plugin_sdk_cmake_dir}"
    COMPONENT plugin-sdk)
endif()

# Resolve aliases before consulting properties.  The explicit global registry
# below is intentionally separate from the target marker: plugin CMake code
# cannot opt itself in merely by copying a property name.
function(_seekdb_resolve_plugin_target target output)
  if (NOT TARGET "${target}")
    message(FATAL_ERROR "unknown plugin-private target: ${target}")
  endif()
  get_target_property(_aliased_target "${target}" ALIASED_TARGET)
  if (_aliased_target)
    set(${output} "${_aliased_target}" PARENT_SCOPE)
  else()
    set(${output} "${target}" PARENT_SCOPE)
  endif()
endfunction()

function(_seekdb_reject_core_identity value context)
  string(TOLOWER "${value}" _identity_lower)
  get_filename_component(_identity_name "${_identity_lower}" NAME)
  if (_identity_lower MATCHES "^(oceanbase|seekdb|ob)" OR
      _identity_name MATCHES "^(lib)?(oceanbase|seekdb)([_-][a-z0-9_.-]*)?\\.(a|lib)$" OR
      _identity_name MATCHES "^(lib)?ob(lib)?([_-][a-z0-9_.-]*)?\\.(a|lib)$")
    message(FATAL_ERROR "${context} refers to seekdb core code: ${value}")
  endif()
endfunction()

function(_seekdb_require_plugin_callsite callsite)
  # In the seekdb monorepo only code below plugins/ may grant plugin-private
  # trust.  This prevents a benignly named core target from marking itself.
  # Standalone projects consuming a copied helper do not have this in-tree
  # plugins/ directory and are governed by their own project root instead.
  if (IS_DIRECTORY "${PROJECT_SOURCE_DIR}/plugins")
    file(REAL_PATH "${PROJECT_SOURCE_DIR}/plugins" _plugin_tree)
    file(REAL_PATH "${callsite}" _callsite_real)
    string(FIND "${_callsite_real}" "${_plugin_tree}/" _callsite_prefix)
    if (NOT _callsite_real STREQUAL "${_plugin_tree}" AND
        NOT _callsite_prefix EQUAL 0)
      message(FATAL_ERROR
        "plugin trust helper may only be called below ${_plugin_tree}; "
        "actual callsite is ${_callsite_real}")
    endif()
  endif()
endfunction()

function(_seekdb_validate_plugin_compile_surface target plugin_root imported)
  foreach(_include_property INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(_include_dirs "${target}" "${_include_property}")
    if (_include_dirs AND NOT _include_dirs MATCHES "-NOTFOUND$")
      foreach(_include_dir IN LISTS _include_dirs)
        if (_include_dir MATCHES "\\$<")
          message(FATAL_ERROR
            "plugin target ${target} has unverifiable ${_include_property}: ${_include_dir}")
        endif()
        get_filename_component(_include_absolute "${_include_dir}" ABSOLUTE
          BASE_DIR "${plugin_root}")
        if (NOT IS_DIRECTORY "${_include_absolute}")
          message(FATAL_ERROR
            "plugin target ${target} has missing ${_include_property}: ${_include_dir}")
        endif()
        file(REAL_PATH "${_include_absolute}" _include_real)
        string(FIND "${_include_real}" "${plugin_root}/" _plugin_include_prefix)
        if (_include_real STREQUAL "${plugin_root}" OR
            _plugin_include_prefix EQUAL 0)
          continue()
        endif()

        # Only explicitly registered imported vendor targets may contribute an
        # external include tree.  Even then, never admit another seekdb source
        # tree; the bundled deps/3rd prefix is the sole in-tree vendor area.
        file(REAL_PATH "${PROJECT_SOURCE_DIR}" _project_source_real)
        file(REAL_PATH "${PROJECT_SOURCE_DIR}/deps/3rd" _third_party_real)
        string(FIND "${_include_real}" "${_project_source_real}/" _project_prefix)
        string(FIND "${_include_real}" "${_third_party_real}/" _third_party_prefix)
        if (NOT imported OR
            (_project_prefix EQUAL 0 AND NOT _third_party_prefix EQUAL 0))
          message(FATAL_ERROR
            "plugin target ${target} ${_include_property} escapes its trusted "
            "plugin/vendor tree: ${_include_dir}")
        endif()
      endforeach()
    endif()
  endforeach()

  # These properties can inject a core header, linker search path, or hidden
  # compilation unit without appearing in SOURCES/LINK_LIBRARIES.
  foreach(_forbidden_property
      COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS
      LINK_DIRECTORIES INTERFACE_LINK_DIRECTORIES
      PRECOMPILE_HEADERS INTERFACE_PRECOMPILE_HEADERS
      COMPILE_FLAGS LINK_FLAGS)
    get_target_property(_forbidden_value "${target}" "${_forbidden_property}")
    if (_forbidden_value AND NOT _forbidden_value MATCHES "-NOTFOUND$")
      message(FATAL_ERROR
        "plugin target ${target} has unreviewable ${_forbidden_property}: "
        "${_forbidden_value}")
    endif()
  endforeach()

  # Definitions are data, never compiler switches or paths.  Macro-driven
  # #include is independently rejected by plugin_boundary_check.py.
  foreach(_definition_property COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
    get_target_property(_definitions "${target}" "${_definition_property}")
    if (_definitions AND NOT _definitions MATCHES "-NOTFOUND$")
      foreach(_definition IN LISTS _definitions)
        if (_definition MATCHES "\\$<" OR
            NOT _definition MATCHES
              "^[A-Za-z_][A-Za-z0-9_]*(=(\"[A-Za-z0-9_.:+-]*\"|[A-Za-z0-9_.:+-]+))?$")
          message(FATAL_ERROR
            "plugin target ${target} has unsafe ${_definition_property}: ${_definition}")
        endif()
      endforeach()
    endif()
  endforeach()
endfunction()

function(_seekdb_validate_plugin_private_shape target plugin_root)
  _seekdb_resolve_plugin_target("${target}" _private_target)
  _seekdb_reject_core_identity("${target}" "plugin-private target")
  _seekdb_reject_core_identity("${_private_target}" "plugin-private target")

  get_target_property(_private_type "${_private_target}" TYPE)
  if (NOT _private_type STREQUAL "STATIC_LIBRARY" AND
      NOT _private_type STREQUAL "OBJECT_LIBRARY" AND
      NOT _private_type STREQUAL "INTERFACE_LIBRARY")
    message(FATAL_ERROR
      "plugin-private target ${target} must be STATIC, OBJECT, or INTERFACE; "
      "actual type is ${_private_type}")
  endif()

  get_target_property(_private_imported "${_private_target}" IMPORTED)
  _seekdb_validate_plugin_compile_surface(
    "${_private_target}" "${plugin_root}" "${_private_imported}")
  if (_private_imported AND _private_type STREQUAL "OBJECT_LIBRARY")
    message(FATAL_ERROR
      "imported OBJECT target ${target} is not an auditable plugin-private library; "
      "import a static archive instead")
  elseif (_private_imported AND _private_type STREQUAL "STATIC_LIBRARY")
    set(_import_locations "")
    get_target_property(_import_configs "${_private_target}" IMPORTED_CONFIGURATIONS)
    set(_location_suffixes "" DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    foreach(_config IN LISTS _import_configs)
      string(TOUPPER "${_config}" _config_upper)
      list(APPEND _location_suffixes "${_config_upper}")
    endforeach()
    list(REMOVE_DUPLICATES _location_suffixes)
    foreach(_suffix IN LISTS _location_suffixes)
      if (_suffix STREQUAL "")
        set(_location_properties IMPORTED_LOCATION IMPORTED_IMPLIB)
      else()
        set(_location_properties
          "IMPORTED_LOCATION_${_suffix}" "IMPORTED_IMPLIB_${_suffix}")
      endif()
      foreach(_property IN LISTS _location_properties)
        get_target_property(_location "${_private_target}" "${_property}")
        if (_location AND NOT _location MATCHES "-NOTFOUND$")
          if (_location MATCHES "\\$<")
            message(FATAL_ERROR
              "plugin-private imported target ${target} has an unverifiable "
              "generator-expression location: ${_location}")
          endif()
          if (NOT IS_ABSOLUTE "${_location}")
            message(FATAL_ERROR
              "plugin-private imported archive for ${target} must use an "
              "absolute path: ${_location}")
          endif()
          string(TOLOWER "${_location}" _location_lower)
          if (NOT _location_lower MATCHES "\\.(a|lib)$")
            message(FATAL_ERROR
              "plugin-private imported STATIC target ${target} does not name a "
              "static archive: ${_location}")
          endif()
          if (NOT EXISTS "${_location}")
            message(FATAL_ERROR
              "plugin-private imported archive for ${target} does not exist: ${_location}")
          endif()
          _seekdb_reject_core_identity(
            "${_location}" "plugin-private imported archive")
          file(REAL_PATH "${_location}" _location_real)
          _seekdb_reject_core_identity(
            "${_location_real}" "plugin-private imported archive target")
          list(APPEND _import_locations "${_location}")
        endif()
      endforeach()
    endforeach()
    if (NOT _import_locations)
      message(FATAL_ERROR
        "plugin-private imported STATIC target ${target} has no auditable archive location")
    endif()
  elseif (_private_imported AND _private_type STREQUAL "INTERFACE_LIBRARY")
    get_target_property(_interface_sources "${_private_target}" INTERFACE_SOURCES)
    if (_interface_sources AND NOT _interface_sources MATCHES "-NOTFOUND$")
      message(FATAL_ERROR
        "imported INTERFACE target ${target} may not inject plugin sources")
    endif()
  elseif (NOT _private_imported)
    get_target_property(_target_source_dir "${_private_target}" SOURCE_DIR)
    file(REAL_PATH "${_target_source_dir}" _target_source_dir_real)
    string(FIND "${_target_source_dir_real}" "${plugin_root}/" _target_dir_prefix)
    if (NOT _target_dir_prefix EQUAL 0 AND
        NOT _target_source_dir_real STREQUAL "${plugin_root}")
      message(FATAL_ERROR
        "plugin-private target ${target} was created outside its plugin tree: "
        "${_target_source_dir_real}")
    endif()
    # INTERFACE_SOURCES are compiled into consumers and are therefore just as
    # security-sensitive as the helper target's own SOURCES.
    foreach(_source_property SOURCES INTERFACE_SOURCES)
      get_target_property(_private_sources
        "${_private_target}" "${_source_property}")
      if (_private_sources AND NOT _private_sources MATCHES "-NOTFOUND$")
        foreach(_source IN LISTS _private_sources)
          if (_source MATCHES "\\$<")
            message(FATAL_ERROR
              "plugin-private target ${target} has generator-expression "
              "${_source_property} entry ${_source}")
          endif()
          get_filename_component(_source_absolute "${_source}" ABSOLUTE
            BASE_DIR "${plugin_root}")
          if (NOT EXISTS "${_source_absolute}")
            message(FATAL_ERROR
              "plugin-private target ${target} has generated or missing "
              "${_source_property} entry ${_source}; only auditable "
              "source-tree files are allowed")
          endif()
          file(REAL_PATH "${_source_absolute}" _source_real)
          string(FIND "${_source_real}" "${plugin_root}/" _source_prefix)
          if (NOT _source_prefix EQUAL 0)
            message(FATAL_ERROR
              "plugin-private target ${target} ${_source_property} entry "
              "escapes plugin tree: ${_source}")
          endif()
        endforeach()
      endif()
    endforeach()
  endif()

  # Link options can smuggle an archive or linker script around
  # INTERFACE_LINK_LIBRARIES, so private helpers must express their complete
  # link graph as validated CMake targets.
  foreach(_option_property LINK_OPTIONS INTERFACE_LINK_OPTIONS)
    get_target_property(_private_options "${_private_target}" "${_option_property}")
    if (_private_options AND NOT _private_options MATCHES "-NOTFOUND$")
      message(FATAL_ERROR
        "plugin-private target ${target} has unreviewable ${_option_property}: "
        "${_private_options}")
    endif()
  endforeach()
endfunction()

function(_seekdb_validate_all_marked_plugin_targets)
  get_property(_explicit_targets GLOBAL
    PROPERTY SEEKDB_EXPLICIT_PLUGIN_PRIVATE_TARGETS)
  foreach(_private_target IN LISTS _explicit_targets)
    _seekdb_validate_marked_plugin_target("${_private_target}" "")
  endforeach()

  # Re-read managed plugin targets at the end of configure so a later
  # target_link_libraries/set_property call cannot add an unregistered edge
  # after seekdb_add_plugin performed its immediate validation.
  get_property(_managed_plugins GLOBAL PROPERTY SEEKDB_MANAGED_PLUGIN_TARGETS)
  foreach(_plugin_target IN LISTS _managed_plugins)
    get_target_property(_plugin_root "${_plugin_target}" SEEKDB_MANAGED_PLUGIN_ROOT)
    _seekdb_validate_plugin_compile_surface(
      "${_plugin_target}" "${_plugin_root}" FALSE)
    get_target_property(_plugin_sources "${_plugin_target}" SOURCES)
    foreach(_source IN LISTS _plugin_sources)
      if (_source MATCHES "\\$<")
        message(FATAL_ERROR
          "managed plugin ${_plugin_target} gained generator-expression source ${_source}")
      endif()
      get_filename_component(_source_absolute "${_source}" ABSOLUTE
        BASE_DIR "${_plugin_root}")
      if (NOT EXISTS "${_source_absolute}")
        message(FATAL_ERROR
          "managed plugin ${_plugin_target} gained missing/generated source ${_source}")
      endif()
      file(REAL_PATH "${_source_absolute}" _source_real)
      string(FIND "${_source_real}" "${_plugin_root}/" _source_prefix)
      if (NOT _source_prefix EQUAL 0)
        message(FATAL_ERROR
          "managed plugin ${_plugin_target} source escapes plugin tree: ${_source}")
      endif()
    endforeach()

    foreach(_link_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(_plugin_links "${_plugin_target}" "${_link_property}")
      if (_plugin_links AND NOT _plugin_links MATCHES "-NOTFOUND$")
        foreach(_dependency IN LISTS _plugin_links)
          if (_dependency STREQUAL "seekdb_plugin_sdk" OR
              _dependency STREQUAL "seekdb::plugin_sdk")
            continue()
          endif()
          if (_dependency MATCHES "\\$<" OR NOT TARGET "${_dependency}")
            message(FATAL_ERROR
              "managed plugin ${_plugin_target} gained an unverifiable late link: ${_dependency}")
          endif()
          _seekdb_validate_marked_plugin_target("${_dependency}" "")
        endforeach()
      endif()
    endforeach()

    get_target_property(_plugin_link_options "${_plugin_target}" LINK_OPTIONS)
    if (_plugin_link_options AND NOT _plugin_link_options MATCHES "-NOTFOUND$")
      foreach(_option IN LISTS _plugin_link_options)
        if (_option STREQUAL "-Wl,-z,defs" OR
            _option STREQUAL "-static-libstdc++" OR
            _option STREQUAL "-static-libgcc")
          continue()
        endif()
        # A plugin may provide a local export map to hide implementation
        # symbols (for example C++ standard-library template instantiations).
        # The map is auditable only when it is a real file below that plugin's
        # source tree; arbitrary linker scripts remain forbidden.
        if (_option MATCHES "^-Wl,--version-script=(.+)$")
          set(_version_script "${CMAKE_MATCH_1}")
          if (EXISTS "${_version_script}")
            file(REAL_PATH "${_version_script}" _version_script_real)
            string(FIND "${_version_script_real}" "${_plugin_root}/" _version_script_prefix)
            if (_version_script_prefix EQUAL 0)
              continue()
            endif()
          endif()
        endif()
        message(FATAL_ERROR
          "managed plugin ${_plugin_target} gained an unreviewable link option: ${_option}")
      endforeach()
    endif()

    if (TARGET plugin_boundary_check)
      add_dependencies("${_plugin_target}" plugin_boundary_check)
    endif()
  endforeach()
endfunction()

function(_seekdb_validate_marked_plugin_target target visited)
  _seekdb_resolve_plugin_target("${target}" _private_target)
  get_property(_explicit_targets GLOBAL
    PROPERTY SEEKDB_EXPLICIT_PLUGIN_PRIVATE_TARGETS)
  list(FIND _explicit_targets "${_private_target}" _explicit_index)
  if (_explicit_index EQUAL -1)
    message(FATAL_ERROR
      "plugin dependency ${target} was not explicitly registered with "
      "seekdb_mark_plugin_private_library")
  endif()

  get_target_property(_plugin_root "${_private_target}"
    SEEKDB_PLUGIN_PRIVATE_ROOT)
  if (NOT _plugin_root OR _plugin_root MATCHES "-NOTFOUND$")
    message(FATAL_ERROR "plugin-private target ${target} has no trusted source root")
  endif()
  _seekdb_validate_plugin_private_shape("${_private_target}" "${_plugin_root}")

  set(_visited ${visited})
  list(FIND _visited "${_private_target}" _visited_index)
  if (NOT _visited_index EQUAL -1)
    return()
  endif()
  list(APPEND _visited "${_private_target}")

  foreach(_link_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(_private_links "${_private_target}" "${_link_property}")
    if (_private_links AND NOT _private_links MATCHES "-NOTFOUND$")
      foreach(_dependency IN LISTS _private_links)
        if (_dependency STREQUAL "seekdb_plugin_sdk" OR
            _dependency STREQUAL "seekdb::plugin_sdk")
          continue()
        endif()
        if (_dependency MATCHES "\\$<")
          message(FATAL_ERROR
            "plugin-private target ${target} has unverifiable transitive "
            "generator-expression dependency: ${_dependency}")
        endif()
        if (NOT TARGET "${_dependency}")
          message(FATAL_ERROR
            "plugin-private target ${target} has raw/unregistered transitive "
            "link item: ${_dependency}")
        endif()
        _seekdb_reject_core_identity(
          "${_dependency}" "plugin-private transitive dependency")
        _seekdb_validate_marked_plugin_target("${_dependency}" "${_visited}")
      endforeach()
    endif()
  endforeach()
endfunction()

# Explicitly opt in a plugin-owned or imported vendor helper.  Every target in
# its transitive link graph must be marked first; raw flags, generator
# expressions, shared libraries and unknown imported libraries fail closed.
function(seekdb_mark_plugin_private_library target)
  _seekdb_require_plugin_callsite("${CMAKE_CURRENT_SOURCE_DIR}")
  _seekdb_resolve_plugin_target("${target}" _private_target)
  file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" _plugin_private_root)
  _seekdb_validate_plugin_private_shape(
    "${_private_target}" "${_plugin_private_root}")

  # Dependencies must already have gone through this function.  Marking leaves
  # are therefore deterministic and a later add_plugin call revalidates the
  # entire graph in case a target was mutated after registration.
  foreach(_link_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(_private_links "${_private_target}" "${_link_property}")
    if (_private_links AND NOT _private_links MATCHES "-NOTFOUND$")
      foreach(_dependency IN LISTS _private_links)
        if (_dependency STREQUAL "seekdb_plugin_sdk" OR
            _dependency STREQUAL "seekdb::plugin_sdk")
          continue()
        endif()
        if (_dependency MATCHES "\\$<" OR NOT TARGET "${_dependency}")
          message(FATAL_ERROR
            "plugin-private target ${target} has an unverifiable dependency: ${_dependency}")
        endif()
        _seekdb_validate_marked_plugin_target("${_dependency}" "")
      endforeach()
    endif()
  endforeach()

  set_property(TARGET "${_private_target}"
    PROPERTY SEEKDB_PLUGIN_PRIVATE_LIBRARY TRUE)
  set_property(TARGET "${_private_target}"
    PROPERTY SEEKDB_PLUGIN_PRIVATE_ROOT "${_plugin_private_root}")
  get_property(_explicit_targets GLOBAL
    PROPERTY SEEKDB_EXPLICIT_PLUGIN_PRIVATE_TARGETS)
  list(APPEND _explicit_targets "${_private_target}")
  list(REMOVE_DUPLICATES _explicit_targets)
  set_property(GLOBAL PROPERTY SEEKDB_EXPLICIT_PLUGIN_PRIVATE_TARGETS
    "${_explicit_targets}")
endfunction()

# Add a native seekdb plugin without exposing or linking any core-private
# target.  Plugins communicate with seekdb exclusively through the C ABI host
# table in include/seekdb/plugin/seekdb_plugin_abi.h.
#
# seekdb_add_plugin(<target>
#   SOURCES <source>...
#   [OUTPUT_NAME <file-stem>]
#   MANIFEST <plugin.toml>
#   [PRIVATE_LIBRARIES <private-vendor-lib>...]
#   [NO_INSTALL])
function(seekdb_add_plugin target)
  _seekdb_require_plugin_callsite("${CMAKE_CURRENT_SOURCE_DIR}")
  set(_options NO_INSTALL)
  set(_one_value OUTPUT_NAME MANIFEST)
  set(_multi_value SOURCES PRIVATE_LIBRARIES)
  cmake_parse_arguments(PLUGIN
    "${_options}" "${_one_value}" "${_multi_value}" ${ARGN})

  if (PLUGIN_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "seekdb_add_plugin(${target}): unknown arguments: ${PLUGIN_UNPARSED_ARGUMENTS}")
  endif()
  if (NOT PLUGIN_SOURCES)
    message(FATAL_ERROR "seekdb_add_plugin(${target}) requires SOURCES")
  endif()
  if (PLUGIN_MANIFEST)
    get_filename_component(_plugin_manifest_absolute "${PLUGIN_MANIFEST}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  if (NOT PLUGIN_MANIFEST OR NOT EXISTS "${_plugin_manifest_absolute}")
    message(FATAL_ERROR
      "seekdb_add_plugin(${target}) requires an existing MANIFEST")
  endif()
  if (TARGET ${target})
    message(FATAL_ERROR "seekdb_add_plugin(${target}): target already exists")
  endif()

  file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" _plugin_source_root)
  file(REAL_PATH "${_plugin_manifest_absolute}" _plugin_manifest_real)
  string(FIND "${_plugin_manifest_real}" "${_plugin_source_root}/"
    _manifest_prefix)
  if (NOT _manifest_prefix EQUAL 0)
    message(FATAL_ERROR
      "seekdb plugin ${target} manifest escapes its plugin tree: ${PLUGIN_MANIFEST}")
  endif()
  foreach(_source IN LISTS PLUGIN_SOURCES)
    if (_source MATCHES "\\$<")
      message(FATAL_ERROR
        "seekdb plugin ${target} may not use generator-expression SOURCES: ${_source}")
    endif()
    get_filename_component(_source_absolute "${_source}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if (NOT EXISTS "${_source_absolute}")
      message(FATAL_ERROR
        "seekdb plugin ${target} source does not exist: ${_source}")
    endif()
    file(REAL_PATH "${_source_absolute}" _source_real)
    string(FIND "${_source_real}" "${_plugin_source_root}/" _source_prefix)
    if (NOT _source_prefix EQUAL 0)
      message(FATAL_ERROR
        "seekdb plugin ${target} source escapes its plugin tree: ${_source}")
    endif()
  endforeach()

  # A plugin that links these targets has bypassed the ABI and cannot be loaded
  # safely by a different seekdb build.  Vendor libraries remain allowed and
  # are private to the module, but imported status alone is not a trust signal.
  foreach(_library IN LISTS PLUGIN_PRIVATE_LIBRARIES)
    if (_library MATCHES "\\$<")
      message(FATAL_ERROR
        "seekdb plugin ${target} must not link core-private target ${_library}")
    endif()
    if (NOT TARGET ${_library})
      message(FATAL_ERROR
        "seekdb plugin ${target} PRIVATE_LIBRARIES accepts only validated "
        "CMake targets, not raw link items: ${_library}")
    endif()
    _seekdb_reject_core_identity(
      "${_library}" "seekdb plugin ${target} private dependency")
    _seekdb_validate_marked_plugin_target("${_library}" "")
  endforeach()

  add_library(${target} MODULE ${PLUGIN_SOURCES})
  target_link_libraries(${target} PRIVATE
    seekdb_plugin_sdk ${PLUGIN_PRIVATE_LIBRARIES})
  set_target_properties(${target} PROPERTIES
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    POSITION_INDEPENDENT_CODE YES
    SEEKDB_MANAGED_PLUGIN_ROOT "${_plugin_source_root}")
  get_property(_managed_plugins GLOBAL PROPERTY SEEKDB_MANAGED_PLUGIN_TARGETS)
  list(APPEND _managed_plugins "${target}")
  list(REMOVE_DUPLICATES _managed_plugins)
  set_property(GLOBAL PROPERTY SEEKDB_MANAGED_PLUGIN_TARGETS "${_managed_plugins}")
  if (PLUGIN_OUTPUT_NAME)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${PLUGIN_OUTPUT_NAME}")
  endif()

  # The ABI must be closed: an implementation cannot silently bind to seekdb
  # internals from the loading process.  macOS resolves host symbols differently
  # and Windows reports unresolved imports without this GNU linker flag.
  if (UNIX AND NOT APPLE)
    target_link_options(${target} PRIVATE "-Wl,-z,defs")
  endif()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/cmake/plugin_binary_check.py"
            --binary "$<TARGET_FILE:${target}>"
            --nm "${CMAKE_NM}"
    COMMENT "Auditing seekdb plugin binary ${target}"
    VERBATIM)

  if (NOT PLUGIN_NO_INSTALL)
    include(GNUInstallDirs)
    set(_plugin_install_dir
      "${CMAKE_INSTALL_LIBDIR}/seekdb/plugins/${target}")
    install(TARGETS ${target}
      LIBRARY DESTINATION "${_plugin_install_dir}" COMPONENT plugins
      RUNTIME DESTINATION "${_plugin_install_dir}"
      COMPONENT plugins)
    install(FILES "${_plugin_manifest_real}"
      DESTINATION "${_plugin_install_dir}"
      COMPONENT plugins)
  endif()
endfunction()

# Validate again after all subdirectories have had a chance to mutate targets.
# The project requires CMake 3.20, so deferred directory calls are available.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
  CALL _seekdb_validate_all_marked_plugin_targets)
