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
  #   share/   - admin SQL, timezone, srs, upgrade, help
  ##############################################################################

  # ── VC++ runtime redistributable (MSVCP140.dll, VCRUNTIME140.dll, etc.) ──
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT server)
  include(InstallRequiredSystemLibraries)

  # Binaries -> bin/
  install(PROGRAMS
    ${CMAKE_BINARY_DIR}/src/observer/seekdb.exe
    ${CMAKE_BINARY_DIR}/src/observer/observer.exe
    DESTINATION bin
    COMPONENT server)

  # ── Bundle third-party runtime DLLs (vcpkg, OpenSSL, etc.) ───────────────
  # Uses file(GET_RUNTIME_DEPENDENCIES) at install time to recursively resolve
  # all DLL dependencies of the built executables — similar to how MySQL bundles
  # its runtime libraries into the MSI package.
  set(_SEEKDB_EXE "${CMAKE_BINARY_DIR}/src/observer/seekdb.exe")
  set(_OBSERVER_EXE "${CMAKE_BINARY_DIR}/src/observer/observer.exe")
  set(_VCPKG_BIN_DIR "${OB_VCPKG_DIR}/bin")

  file(WRITE "${CMAKE_BINARY_DIR}/_bundle_dlls.cmake.in" [=[
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES
    "@_SEEKDB_EXE@"
    "@_OBSERVER_EXE@"
  RESOLVED_DEPENDENCIES_VAR _resolved
  UNRESOLVED_DEPENDENCIES_VAR _unresolved
  CONFLICTING_DEPENDENCIES_PREFIX _conflicts
  DIRECTORIES
    "@_VCPKG_BIN_DIR@"
  PRE_EXCLUDE_REGEXES
    "^api-ms-"
    "^ext-ms-"
)

set(_vcpkg_bin "@_VCPKG_BIN_DIR@")
set(_bundled 0)

# Install resolved dependencies that have a vcpkg copy.
# System-only DLLs (KERNEL32, ADVAPI32, ...) are absent from vcpkg and
# therefore skipped — they ship with every Windows installation.
foreach(_file ${_resolved})
  get_filename_component(_name "${_file}" NAME)
  if(EXISTS "${_vcpkg_bin}/${_name}")
    message(STATUS "  ${_name}")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
      TYPE SHARED_LIBRARY FILES "${_vcpkg_bin}/${_name}")
    math(EXPR _bundled "${_bundled} + 1")
  endif()
endforeach()

# Conflicting dependencies (same DLL in vcpkg AND System32, e.g. libssl).
foreach(_name ${_conflicts_FILENAMES})
  if(EXISTS "${_vcpkg_bin}/${_name}")
    message(STATUS "  ${_name} (conflict resolved -> vcpkg)")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
      TYPE SHARED_LIBRARY FILES "${_vcpkg_bin}/${_name}")
    math(EXPR _bundled "${_bundled} + 1")
  endif()
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
    tools/upgrade/oceanbase_upgrade_dep.yml
    tools/upgrade/deps_compat.yml
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

  # Help -> share/help/
  install(FILES
    src/sql/fill_help_tables-ob.sql
    DESTINATION share/help
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

  # Upgrade -> share/upgrade/
  install(FILES
    tools/upgrade/upgrade_pre.py
    tools/upgrade/upgrade_post.py
    tools/upgrade/upgrade_checker.py
    tools/upgrade/upgrade_health_checker.py
    DESTINATION share/upgrade
    COMPONENT server)

  # Management script -> bin/
  if(EXISTS "${CMAKE_SOURCE_DIR}/tools/windows/seekdb_manage.ps1")
    install(PROGRAMS
      tools/windows/seekdb_manage.ps1
      DESTINATION bin
      COMPONENT server)
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
  #   usr/share/seekdb/             - admin, timezone, srs, upgrade, help
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

  if (OB_BUILD_STANDALONE)
    install(PROGRAMS
    deps/3rd/home/admin/oceanbase/bin/obshell
    DESTINATION usr/bin
    COMPONENT server)
  endif()

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
    tools/upgrade/oceanbase_upgrade_dep.yml
    tools/upgrade/deps_compat.yml
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

  # Install help files to /usr/share/seekdb/help
  install(FILES
    src/sql/fill_help_tables-ob.sql
    DESTINATION usr/share/seekdb/help
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

  # Install upgrade scripts to /usr/share/seekdb/upgrade
  install(FILES
    tools/upgrade/upgrade_pre.py
    tools/upgrade/upgrade_post.py
    tools/upgrade/upgrade_checker.py
    tools/upgrade/upgrade_health_checker.py
    DESTINATION usr/share/seekdb/upgrade
    COMPONENT server)

  # Install ocp configuration to /usr/share/seekdb/software_package
  install(DIRECTORY
    DESTINATION usr/share/seekdb/software_package
    COMPONENT server)

  if(OB_BUILD_OBADMIN)
    if(NOT APPLE)
      list(APPEND CPACK_COMPONENTS_ALL utils)
      install(PROGRAMS
        ${CMAKE_BINARY_DIR}/tools/ob_admin/ob_admin
        ${CMAKE_BINARY_DIR}/tools/ob_error/src/ob_error
        ${DEVTOOLS_DIR}/bin/obstack
        DESTINATION usr/bin
        COMPONENT utils
      )
    else()
      list(APPEND CPACK_COMPONENTS_ALL utils)
      install(PROGRAMS
        ${CMAKE_BINARY_DIR}/tools/ob_admin/ob_admin
        ${CMAKE_BINARY_DIR}/tools/ob_error/src/ob_error
        DESTINATION usr/bin
        COMPONENT utils
      )
    endif()
  endif()

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
