ob_define(CPACK_PACKAGING_INSTALL_PREFIX /)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OceanBase is a distributed relational database")
set(CPACK_PACKAGE_VENDOR "OceanBase Inc.")
set(CPACK_PACKAGE_DESCRIPTION "OceanBase is a distributed relational database")
set(CPACK_COMPONENTS_ALL server sql-parser)

set(CPACK_PACKAGE_NAME "oceanbase-seekdb")
set(CPACK_PACKAGE_VERSION "${OceanBase_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${OceanBase_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${OceanBase_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${OceanBase_VERSION_PATCH}")

## TIPS
#
# - PATH is relative to the **ROOT directory** of project other than the cmake directory.

set(BITCODE_TO_ELF_LIST "")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/telemetry.sh.template
${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/telemetry.sh
@ONLY)

set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/usr" "/usr/lib" "/usr/lib/systemd" "/usr/lib/systemd/system" "/usr/libexec" "/etc"
)

## server - Install to proper directory structure

# Install binaries to /usr/bin
install(PROGRAMS
  ${CMAKE_BINARY_DIR}/src/observer/observer
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
  tools/systemd/profile/oceanbase.service
  DESTINATION usr/lib/systemd/system
  COMPONENT server)

# Install python scripts to /usr/libexec/oceanbase
install(PROGRAMS
  tools/import_time_zone_info.py
  tools/import_srs_data.py
  DESTINATION usr/libexec/oceanbase
  COMPONENT server)

install(PROGRAMS
  tools/systemd/profile/oceanbase_systemd_start
  tools/systemd/profile/telemetry.sh
  DESTINATION usr/libexec/oceanbase/scripts
  COMPONENT server)

# Install configuration files to /etc/oceanbase
set(INSTALL_EXTRA_FILES "")

file(READ "${CMAKE_SOURCE_DIR}/src/share/system_variable/ob_system_variable_init.json" SYS_VAR_INIT_JSON)
string(REGEX REPLACE "\"ref_url\"[^\"]*\"[^\"]*\"" "\"ref_url\": \"\"" SYS_VAR_INIT_JSON "${SYS_VAR_INIT_JSON}")
file(WRITE "${CMAKE_BINARY_DIR}/src/share/ob_system_variable_init.json" "${SYS_VAR_INIT_JSON}")

install(FILES
  src/share/parameter/default_parameter.json
  src/share/system_variable/default_system_variable.json
  tools/upgrade/oceanbase_upgrade_dep.yml
  tools/upgrade/deps_compat.yml
  ${CMAKE_BINARY_DIR}/src/share/ob_system_variable_init.json
  ${INSTALL_EXTRA_FILES}
  tools/systemd/profile/oceanbase.cnf
  tools/systemd/profile/oceanbase-pre.json
  tools/systemd/profile/telemetry-pre.json
  DESTINATION etc/oceanbase
  COMPONENT server)

# Install admin SQL files to /usr/share/oceanbase/admin
message(STATUS "system package release directory: " ${SYS_PACK_RELEASE_DIR})
install(
  DIRECTORY ${SYS_PACK_RELEASE_DIR}/
  DESTINATION usr/share/oceanbase/admin
  COMPONENT server)

# Install help files to /usr/share/oceanbase/help
install(FILES
  src/sql/fill_help_tables-ob.sql
  DESTINATION usr/share/oceanbase/help
  COMPONENT server)

# Install timezone files to /usr/share/oceanbase/timezone
install(FILES
  tools/timezone_V1.log
  tools/timezone.data
  tools/timezone_name.data
  tools/timezone_trans.data
  tools/timezone_trans_type.data
  DESTINATION usr/share/oceanbase/timezone
  COMPONENT server)

# Install SRS files to /usr/share/oceanbase/srs
install(FILES
  tools/spatial_reference_systems.data
  tools/default_srs_data_mysql.sql
  DESTINATION usr/share/oceanbase/srs
  COMPONENT server)

# Install upgrade scripts to /usr/share/oceanbase/upgrade
install(FILES
  tools/upgrade/upgrade_pre.py
  tools/upgrade/upgrade_post.py
  tools/upgrade/upgrade_checker.py
  tools/upgrade/upgrade_health_checker.py
  DESTINATION usr/share/oceanbase/upgrade
  COMPONENT server)

# Install ocp configuration to /usr/share/oceanbase/software_package
install(DIRECTORY
  DESTINATION usr/share/oceanbase/software_package
  COMPONENT server)

## oceanbase-sql-parser
if (OB_BUILD_LIBOB_SQL_PROXY_PARSER)

  if (ENABLE_THIN_LTO)
    message(STATUS "add libob_sql_proxy_parser_static_to_elf")
    add_custom_command(
      OUTPUT libob_sql_proxy_parser_static_to_elf
      COMMAND ${CMAKE_SOURCE_DIR}/cmake/script/bitcode_to_elfobj --ld=${OB_LD_BIN} --input=${CMAKE_BINARY_DIR}/src/sql/parser/libob_sql_proxy_parser_static.a --output=${CMAKE_BINARY_DIR}/src/sql/parser/libob_sql_proxy_parser_static.a
      DEPENDS ob_sql_proxy_parser_static
      COMMAND_EXPAND_LISTS)
    list(APPEND BITCODE_TO_ELF_LIST libob_sql_proxy_parser_static_to_elf)
  endif()

  install(PROGRAMS
    ${CMAKE_BINARY_DIR}/src/sql/parser/libob_sql_proxy_parser_static.a
    DESTINATION usr/lib64
    COMPONENT sql-parser
    )
endif()

install(FILES
  src/objit/include/objit/common/ob_item_type.h
  deps/oblib/src/common/sql_mode/ob_sql_mode.h
  src/sql/parser/ob_sql_parser.h
  src/sql/parser/parse_malloc.h
  src/sql/parser/parser_proxy_func.h
  src/sql/parser/parse_node.h
  DESTINATION usr/include
  COMPONENT sql-parser)

if(OB_BUILD_OBADMIN)
  ## oceanbase-utils
  list(APPEND CPACK_COMPONENTS_ALL utils)
  install(PROGRAMS
    ${CMAKE_BINARY_DIR}/tools/ob_admin/ob_admin
    ${CMAKE_BINARY_DIR}/tools/ob_error/src/ob_error
    ${DEVTOOLS_DIR}/bin/obstack
    DESTINATION usr/bin
    COMPONENT utils
  )
endif()
