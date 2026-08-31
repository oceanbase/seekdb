macro(ob_define VAR DEFAULT)
  if (NOT DEFINED ${VAR})
    set(${VAR} ${DEFAULT})
  endif()
endmacro()

function(ob_replace_in_file INFILE OUTFILE MATCH-STRING REPLACE-STRING)
  file(READ ${INFILE} CONTENT)
  string(REPLACE ${MATCH-STRING} ${REPLACE-STRING} NEW-CONTENT ${CONTENT})
  file(WRITE ${OUTFILE} ${NEW-CONTENT})
endfunction()

function(ob_set_subtarget target group)

  list(LENGTH ${target}_cache_objects_ CCLS_TARGET_CURRENT_LENGTH)

  # 需要参与编译的源文件列表
  set(ARGN_NEED_LIST "")

  FOREACH(item ${ARGN})
    list(APPEND ARGN_NEED_LIST ${item})
  ENDFOREACH(item)

  list(APPEND "${target}_cache_objects_" ${ARGN_NEED_LIST})
  set("${target}_cache_objects_" ${${target}_cache_objects_} PARENT_SCOPE)

  if (OB_CMAKE_RULES_CHECK)
    FOREACH(item ${ARGN_NEED_LIST})
      # [E1001] Header files are not allowed in CMakeLists.txt
      string(REGEX MATCHALL "^.*\.h$" MATCH_OUTPUT ${item})
      if(MATCH_OUTPUT)
        message(FATAL_ERROR "\n${E1001}\n不允许把头文件${item}写到CMakeLists.txt文件中\n")
      endif()
    ENDFOREACH(item)
  endif()

  # diable global unity build
  if (NOT OB_ENABLE_UNITY)
    return()
  endif()

  # ALONE group will not join unity build
  if(group STREQUAL "ALONE")
    return()
  endif()

  if (NOT OB_BUILD_CCLS)
    set(i 0)
    set(group_id 0)
  else()
    # ccls构建，将更改分组方法，是以target为单位，而不是以group为单元
    set(i ${CCLS_TARGET_CURRENT_LENGTH})
    math(EXPR group_id "(${i} / ${OB_MAX_UNITY_BATCH_SIZE})")
  endif()

  set(ob_sub_objects "")
  FOREACH(item ${ARGN_NEED_LIST})
    math(EXPR i "(${i} + 1) % ${OB_MAX_UNITY_BATCH_SIZE}")
    list(APPEND ob_sub_objects ${item})
    if (${i} EQUAL 0)
      if (NOT OB_BUILD_CCLS)
        set_source_files_properties(${ob_sub_objects} PROPERTIES UNITY_GROUP "${target}_${group}/${group_id}")
      else()
        set_source_files_properties(${ob_sub_objects} PROPERTIES UNITY_GROUP "${target}/${group_id}")
      endif()
      math(EXPR group_id "${group_id} + 1")
      set(ob_sub_objects "")
    endif()
  ENDFOREACH(item)

  if (${i} GREATER 0)
    if (NOT OB_BUILD_CCLS)
      set_source_files_properties(${ob_sub_objects} PROPERTIES UNITY_GROUP "${target}_${group}/${group_id}")
    else()
      set_source_files_properties(${ob_sub_objects} PROPERTIES UNITY_GROUP "${target}/${group_id}")
    endif()
  endif()

endfunction()

# Apply the exact Unity groups frozen by the Bazel production inventory.  This
# avoids a second source list and prevents CMake's grouping from drifting away
# from the action boundaries already validated by Bazel.
set(SEEKDB_CORE_GIS_SQL_REPLACEMENTS
  ob_expr_priv_st_transform.cpp
  ob_expr_st_transform.cpp
  ob_expr_st_bestsrid.cpp
  ob_expr_st_buffer.cpp
  ob_expr_priv_st_clipbybox2d.cpp
  ob_expr_st_union.cpp
  ob_expr_st_difference.cpp
  ob_expr_st_symdifference.cpp
  ob_expr_priv_st_asmvtgeom.cpp
  ob_expr_priv_st_makevalid.cpp
  ob_expr_priv_st_point.cpp
  ob_expr_spatial_cellid.cpp
  ob_expr_spatial_mbr.cpp
  ob_expr_priv_st_geohash.cpp
  ob_expr_spatial_collection.cpp
  ob_geo_expr_utils.cpp)

function(seekdb_filter_core_gis_sql_sources output_var)
  set(filtered_sources)
  foreach(source IN LISTS ARGN)
    get_filename_component(source_name "${source}" NAME)
    list(FIND SEEKDB_CORE_GIS_SQL_REPLACEMENTS "${source_name}" replacement_index)
    if (replacement_index EQUAL -1 OR SEEKDB_ENABLE_CORE_GIS)
      list(APPEND filtered_sources "${source}")
    endif()
  endforeach()
  set("${output_var}" "${filtered_sources}" PARENT_SCOPE)
endfunction()

function(seekdb_apply_unity_inventory target prefix)
  set(all_sources "${${target}_cache_objects_}")
  foreach(group IN LISTS ${prefix}_GROUPS)
    # The Bazel inventory intentionally records the complete production
    # surface, including legacy GIS groups.  CMake's lightweight core profile
    # must prune those groups before they become Unity translation units; the
    # GIS plugin owns their implementation when core GIS is disabled.
    if (NOT SEEKDB_ENABLE_CORE_GIS AND target STREQUAL "ob_share" AND
        group MATCHES "^ob_share_geo(_|$)")
      continue()
    endif()
    set(group_sources "${${prefix}_GROUP_${group}}")
    if (NOT SEEKDB_ENABLE_CORE_GIS AND target STREQUAL "ob_sql")
      seekdb_filter_core_gis_sql_sources(group_sources ${group_sources})
    endif()
    if(NOT group_sources)
      message(FATAL_ERROR "Empty Unity group ${prefix}:${group}")
    endif()
    list(APPEND all_sources ${group_sources})
    set_source_files_properties(${group_sources}
      PROPERTIES UNITY_GROUP "${target}_${group}")
  endforeach()
  set("${target}_cache_objects_" "${all_sources}" PARENT_SCOPE)
endfunction()

function(seekdb_apply_standalone_inventory target variable)
  set(standalone_sources "${${variable}}")
  if (NOT SEEKDB_ENABLE_CORE_GIS AND target STREQUAL "ob_share")
    list(FILTER standalone_sources EXCLUDE REGEX "/src/share/geo/")
  elseif (NOT SEEKDB_ENABLE_CORE_GIS AND target STREQUAL "ob_sql")
    seekdb_filter_core_gis_sql_sources(standalone_sources ${standalone_sources})
  endif()
  if(standalone_sources)
    set(all_sources "${${target}_cache_objects_}")
    list(APPEND all_sources ${standalone_sources})
    set_source_files_properties(${standalone_sources}
      PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    set("${target}_cache_objects_" "${all_sources}" PARENT_SCOPE)
  endif()
endfunction()

function (check_need_build_unity_target target need_build)
  list(LENGTH ${target}_cache_objects_ TARGET_LENGTH)
  if (TARGET_LENGTH EQUAL 0)
    set(${need_build} FALSE PARENT_SCOPE)
  else()
    set(${need_build} TRUE PARENT_SCOPE)
  endif()
endfunction()


set(unity_after [[
#ifdef USING_LOG_PREFIX
#undef USING_LOG_PREFIX
#endif
]])

function(config_target_unity target)
  if (OB_ENABLE_UNITY)
    set_target_properties(${target} PROPERTIES UNITY_BUILD ON)
    set_target_properties(${target} PROPERTIES UNITY_BUILD_CODE_AFTER_INCLUDE "${unity_after}")
    set_target_properties(${target} PROPERTIES UNITY_BUILD_MODE GROUP)
  endif()
endfunction()

function(config_ccls_flag target)
  if (OB_BUILD_CCLS)
    target_compile_definitions(${target} PRIVATE CCLS_LASY_OFF)
  endif()
endfunction()

function(config_remove_coverage_flag target)
  # 针对于特定的目标，由于某种写法会命中clang的DAG解析的bug，将少量文件不参与coverage编译
  if (WITH_COVERAGE)
    get_target_property(EXTLIB_COMPILE_FLAGS ${target} COMPILE_OPTIONS)
    list(REMOVE_ITEM EXTLIB_COMPILE_FLAGS ${CMAKE_COVERAGE_COMPILE_OPTIONS})
    set_target_properties(${target} PROPERTIES COMPILE_OPTIONS "${EXTLIB_COMPILE_FLAGS}")
  endif()
endfunction()

function(ob_add_object_target target)
  add_library(${target} OBJECT "${${target}_cache_objects_}")
  config_target_unity(${target})
  config_ccls_flag(${target})
endfunction()

function(ob_lib_add_target target)
  message(STATUS "ob_lib_add_target ${target}")
  if (${ARGC} EQUAL 1)
    set(base "oblib_base")
  else()
    set(base "oblib_base_without_pass")
  endif()
  ob_add_object_target(${target})
  target_link_libraries(${target} PUBLIC ${base})
  list(APPEND oblib_object_libraries ${target})
  set(oblib_object_libraries "${oblib_object_libraries}" CACHE INTERNAL "observer library list")
  config_ccls_flag(${target})
endfunction()

function(ob_add_new_object_target target target_objects_list)
  message(STATUS "ob_add_new_object_target ${target}")
  add_library(${target} OBJECT EXCLUDE_FROM_ALL "${${target_objects_list}_cache_objects_}")
  config_target_unity(${target})
  config_ccls_flag(${target})
endfunction()

function(ob_insert_nonlse_to_package_version INPUT_PACKAGE_VERSION OUTPUT_PACKAGE_VERSION)
  # 在传入的版本号中插入nonlse版本号
  # input: 2024041400001.el7
  # output: 2024041400001.nonlse.el7
  set(${OUTPUT_PACKAGE_VERSION} "${INPUT_PACKAGE_VERSION}" PARENT_SCOPE)
  string(FIND "${INPUT_PACKAGE_VERSION}" "." DOT_INDEX REVERSE)
  # 只有包含.的才处理
  if(DOT_INDEX GREATER -1)
    # 计算插入点位置
    math(EXPR INSERT_INDEX "${DOT_INDEX} + 1")
    string(SUBSTRING "${INPUT_PACKAGE_VERSION}" 0 "${INSERT_INDEX}" FILE_NAME_PREFIX)
    string(SUBSTRING "${INPUT_PACKAGE_VERSION}" "${INSERT_INDEX}" "-1" FILE_NAME_SUFFIX)
    # 拼接最后的带有nonlse版本号
    set(${OUTPUT_PACKAGE_VERSION} "${FILE_NAME_PREFIX}nonlse.${FILE_NAME_SUFFIX}" PARENT_SCOPE)
  else()
    set(${OUTPUT_PACKAGE_VERSION} "${INPUT_PACKAGE_VERSION}.nonlse" PARENT_SCOPE)
  endif()
endfunction()
