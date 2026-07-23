ob_define(CPACK_PACKAGING_INSTALL_PREFIX /)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OceanBase is a distributed relational database")
set(CPACK_PACKAGE_VENDOR "OceanBase Inc.")
set(CPACK_PACKAGE_DESCRIPTION "OceanBase is a distributed relational database")
set(CPACK_COMPONENTS_ALL server)

set(CPACK_PACKAGE_NAME "seekdb")
set(CPACK_PACKAGE_VERSION "${OceanBase_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${OceanBase_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${OceanBase_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${OceanBase_VERSION_PATCH}")

## TIPS
#
# - PATH is relative to the **ROOT directory** of project other than the cmake directory.

set(BITCODE_TO_ELF_LIST "")

# Process system variable init JSON (shared across platforms)
set(INSTALL_EXTRA_FILES "")
file(READ "${CMAKE_SOURCE_DIR}/src/share/system_variable/ob_system_variable_init.json" SYS_VAR_INIT_JSON)
string(REGEX REPLACE "\"ref_url\"[^\"]*\"[^\"]*\"" "\"ref_url\": \"\"" SYS_VAR_INIT_JSON "${SYS_VAR_INIT_JSON}")
file(WRITE "${CMAKE_BINARY_DIR}/src/share/ob_system_variable_init.json" "${SYS_VAR_INIT_JSON}")

if(WIN32)
  ##############################################################################
  # Windows install layout:
  #   bin/     - seekdb.exe, observer.exe, ob_admin.exe, runtime DLLs
  #   etc/     - seekdb.cnf, JSON configs
  #   share/   - admin SQL, timezone, srs, help
  ##############################################################################

  # ── VC++ runtime redistributable (MSVCP140.dll, VCRUNTIME140.dll, etc.) ──
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT server)
  include(InstallRequiredSystemLibraries)

  # Binaries -> bin/
  install(PROGRAMS
    ${CMAKE_BINARY_DIR}/src/observer/seekdb.exe
    DESTINATION bin
    COMPONENT server)

  # ── Bundle third-party runtime DLLs (vcpkg, OpenSSL, etc.) ───────────────
  # Uses file(GET_RUNTIME_DEPENDENCIES) at install time to recursively resolve
  # all DLL dependencies of the built executables — similar to how MySQL bundles
  # its runtime libraries into the MSI package.
  # OB_VCPKG_DIR / OB_VSAG_DIR are normalized to forward slashes in Env.cmake;
  # the TO_CMAKE_PATH calls below are defensive (idempotent) in case a future
  # caller injects backslashes — configure_file() would otherwise bake them
  # verbatim into _bundle_dlls.cmake and trigger CMake 3.20+'s "Invalid
  # character escape" error (\w, \d, \x, ...).
  file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/src/observer/seekdb.exe" _SEEKDB_EXE)
  file(TO_CMAKE_PATH "${OB_VCPKG_DIR}/bin" _VCPKG_BIN_DIR)
  file(TO_CMAKE_PATH "${OB_VSAG_DIR}/bin" _VSAG_BIN_DIR)

  file(WRITE "${CMAKE_BINARY_DIR}/_bundle_dlls.cmake.in" [=[
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES
    "@_SEEKDB_EXE@"
  RESOLVED_DEPENDENCIES_VAR _resolved
  UNRESOLVED_DEPENDENCIES_VAR _unresolved
  CONFLICTING_DEPENDENCIES_PREFIX _conflicts
  DIRECTORIES
    "@_VCPKG_BIN_DIR@"
    "@_VSAG_BIN_DIR@"
  PRE_EXCLUDE_REGEXES
    "^api-ms-"
    "^ext-ms-"
)

set(_search_dirs "@_VCPKG_BIN_DIR@;@_VSAG_BIN_DIR@")
set(_bundled 0)

# Install resolved dependencies that live in vcpkg or vsag directories.
# System-only DLLs (KERNEL32, ADVAPI32, ...) are absent from these dirs
# and therefore skipped — they ship with every Windows installation.
foreach(_file ${_resolved})
  get_filename_component(_name "${_file}" NAME)
  set(_found FALSE)
  foreach(_dir ${_search_dirs})
    if(EXISTS "${_dir}/${_name}")
      message(STATUS "  ${_name}")
      file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
        TYPE SHARED_LIBRARY FILES "${_dir}/${_name}")
      math(EXPR _bundled "${_bundled} + 1")
      set(_found TRUE)
      break()
    endif()
  endforeach()
endforeach()

# Conflicting dependencies (same DLL in multiple dirs AND System32).
foreach(_name ${_conflicts_FILENAMES})
  foreach(_dir ${_search_dirs})
    if(EXISTS "${_dir}/${_name}")
      message(STATUS "  ${_name} (conflict resolved -> ${_dir})")
      file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
        TYPE SHARED_LIBRARY FILES "${_dir}/${_name}")
      math(EXPR _bundled "${_bundled} + 1")
      break()
    endif()
  endforeach()
endforeach()

message(STATUS "Bundled ${_bundled} runtime DLLs into bin/")
]=])

  configure_file(
    "${CMAKE_BINARY_DIR}/_bundle_dlls.cmake.in"
    "${CMAKE_BINARY_DIR}/_bundle_dlls.cmake"
    @ONLY)

  install(SCRIPT "${CMAKE_BINARY_DIR}/_bundle_dlls.cmake"
    COMPONENT server)

  # Configuration -> etc/
  install(FILES
    tools/systemd/profile/seekdb_win.cnf
    DESTINATION etc
    RENAME seekdb.cnf
    COMPONENT server)

  install(FILES
    src/share/parameter/default_parameter.json
    src/share/system_variable/default_system_variable.json
    ${CMAKE_BINARY_DIR}/src/share/ob_system_variable_init.json
    ${INSTALL_EXTRA_FILES}
    DESTINATION etc
    COMPONENT server)

  # Admin SQL -> share/admin/
  message(STATUS "system package release directory: " ${SYS_PACK_RELEASE_DIR})
  install(
    DIRECTORY ${SYS_PACK_RELEASE_DIR}/
    DESTINATION share/admin
    COMPONENT server)

  # Timezone -> share/timezone/
  install(FILES
    tools/timezone_V1.log
    tools/timezone.data
    tools/timezone_name.data
    tools/timezone_trans.data
    tools/timezone_trans_type.data
    DESTINATION share/timezone
    COMPONENT server)

  # SRS -> share/srs/
  install(FILES
    tools/spatial_reference_systems.data
    tools/default_srs_data_mysql.sql
    DESTINATION share/srs
    COMPONENT server)

  # Management script -> bin/
  if(EXISTS "${CMAKE_SOURCE_DIR}/tools/windows/seekdb_manage.ps1")
    install(PROGRAMS
      tools/windows/seekdb_manage.ps1
      DESTINATION bin
      COMPONENT server)
  endif()

  # seekdb Configurator -> bin/
  # Built by 'dotnet publish' (self-contained single-file) before cpack runs.
  # build.ps1 places the output under tools/windows/seekdbConfigurator/publish/.
  set(_CONFIGURATOR_EXE
    "${CMAKE_SOURCE_DIR}/tools/windows/seekdbConfigurator/publish/seekdbConfigurator.exe")
  if(EXISTS "${_CONFIGURATOR_EXE}")
    install(PROGRAMS "${_CONFIGURATOR_EXE}"
      DESTINATION bin
      COMPONENT server)
  else()
    message(WARNING
      "seekdbConfigurator.exe not found at ${_CONFIGURATOR_EXE}. "
      "The MSI will not include the post-install Configurator wizard. "
      "Run 'dotnet publish' on seekdbConfigurator.csproj first, or use "
      "'build.ps1 package' which does this automatically.")
  endif()

  # TODO: Utils (ob_admin/ob_error) — uncomment when Windows tool builds are ready
  # if(OB_BUILD_OBADMIN)
  #   list(APPEND CPACK_COMPONENTS_ALL utils)
  #   install(PROGRAMS
  #     ${CMAKE_BINARY_DIR}/tools/ob_admin/ob_admin.exe
  #     ${CMAKE_BINARY_DIR}/tools/ob_error/src/ob_error.exe
  #     DESTINATION bin
  #     COMPONENT utils)
  # endif()

else()
  ##############################################################################
  # Linux/macOS install layout (original):
  #   usr/bin/                      - seekdb, obshell
  #   usr/lib/systemd/system/       - seekdb.service
  #   usr/libexec/seekdb/           - python scripts
  #   etc/seekdb/                   - configs
  #   usr/share/seekdb/             - admin, timezone, srs, help
  ##############################################################################

  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/telemetry.sh.template
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/telemetry.sh
  @ONLY)

  set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
      "/usr" "/usr/lib" "/usr/lib/systemd" "/usr/lib/systemd/system" "/usr/libexec" "/etc"
  )

  # Install binaries to /usr/bin
  install(PROGRAMS
    ${CMAKE_BINARY_DIR}/src/observer/seekdb
    deps/3rd/home/admin/oceanbase/bin/obshell
    DESTINATION usr/bin
    COMPONENT server)

  # Install systemd service to /usr/lib/systemd/system
  install(FILES
    tools/systemd/profile/seekdb.service
    DESTINATION usr/lib/systemd/system
    COMPONENT server)

  # Install python scripts to /usr/libexec/oceanbase
  install(PROGRAMS
    tools/import_time_zone_info.py
    tools/import_srs_data.py
    DESTINATION usr/libexec/seekdb
    COMPONENT server)

  install(PROGRAMS
    tools/systemd/profile/seekdb_systemd_start
    tools/systemd/profile/seekdb_systemd_stop
    tools/systemd/profile/telemetry.sh
    DESTINATION usr/libexec/seekdb/scripts
    COMPONENT server)

  # Install configuration files to /etc/seekdb
  install(FILES
    src/share/parameter/default_parameter.json
    src/share/system_variable/default_system_variable.json
    ${CMAKE_BINARY_DIR}/src/share/ob_system_variable_init.json
    ${INSTALL_EXTRA_FILES}
    tools/systemd/profile/seekdb.cnf
    tools/systemd/profile/oceanbase-pre.json
    tools/systemd/profile/telemetry-pre.json
    DESTINATION etc/seekdb
    COMPONENT server)

  # Install admin SQL files to /usr/share/seekdb/admin
  message(STATUS "system package release directory: " ${SYS_PACK_RELEASE_DIR})
  install(
    DIRECTORY ${SYS_PACK_RELEASE_DIR}/
    DESTINATION usr/share/seekdb/admin
    COMPONENT server)

  # Install timezone files to /usr/share/seekdb/timezone
  install(FILES
    tools/timezone_V1.log
    tools/timezone.data
    tools/timezone_name.data
    tools/timezone_trans.data
    tools/timezone_trans_type.data
    DESTINATION usr/share/seekdb/timezone
    COMPONENT server)

  # Install SRS files to /usr/share/seekdb/srs
  install(FILES
    tools/spatial_reference_systems.data
    tools/default_srs_data_mysql.sql
    DESTINATION usr/share/seekdb/srs
    COMPONENT server)

endif()

if(NOT APPLE AND NOT WIN32)
  ## oceanbase-libs (Linux only; Windows does not ship libaio.so)
  list(APPEND CPACK_COMPONENTS_ALL libs)
  install(PROGRAMS
    deps/3rd/usr/local/oceanbase/deps/devel/lib/libaio.so.1
    deps/3rd/usr/local/oceanbase/deps/devel/lib/libaio.so.1.0.1
    deps/3rd/usr/local/oceanbase/deps/devel/lib/libaio.so
    DESTINATION usr/libexec/seekdb/lib
    COMPONENT libs
  )
endif()
