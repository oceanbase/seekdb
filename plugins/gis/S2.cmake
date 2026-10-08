# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
#
# Plugin-private copies of the SAME S2 / Abseil / OpenSSL archives used by
# SeekDB's existing spatial index. No server archive, GEOS or PROJ is linked.
# Keep the Abseil graph explicit: importing the host's grpc::/absl:: targets can
# silently choose a different Abseil ABI, and globbing all archives conceals
# new dependencies from the plugin boundary validator.
if(TARGET gis_s2)
  return()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/BoostGeometry.cmake")
get_target_property(_gis_s2_include gis_boost_geometry INTERFACE_INCLUDE_DIRECTORIES)
get_filename_component(_gis_s2_deps "${_gis_s2_include}" DIRECTORY)

function(_gis_s2_archive target relative_path)
  set(_archive "${_gis_s2_deps}/${relative_path}")
  if(NOT EXISTS "${_archive}")
    message(FATAL_ERROR "GIS S2 requires SeekDB's prepared archive: ${_archive}")
  endif()
  add_library(${target} STATIC IMPORTED GLOBAL)
  set_target_properties(${target} PROPERTIES
    IMPORTED_LOCATION "${_archive}"
    INTERFACE_INCLUDE_DIRECTORIES "${_gis_s2_include}")
  if(ARGN)
    set_property(TARGET ${target} PROPERTY INTERFACE_LINK_LIBRARIES "${ARGN}")
  endif()
  # The standalone integration-test project has no server plugin helper.
  if(COMMAND seekdb_mark_plugin_private_library)
    seekdb_mark_plugin_private_library(${target})
  endif()
endfunction()

# Dependency-first ordering mirrors the prepared Abseil target definitions.
_gis_s2_archive(gis_absl_log_severity lib64/libabsl_log_severity.a)
_gis_s2_archive(gis_absl_raw_logging_internal lib64/libabsl_raw_logging_internal.a
  gis_absl_log_severity)
_gis_s2_archive(gis_absl_spinlock_wait lib64/libabsl_spinlock_wait.a)
_gis_s2_archive(gis_absl_base lib64/libabsl_base.a
  gis_absl_log_severity gis_absl_raw_logging_internal gis_absl_spinlock_wait pthread rt)
_gis_s2_archive(gis_absl_city lib64/libabsl_city.a
  gis_absl_base)
_gis_s2_archive(gis_absl_throw_delegate lib64/libabsl_throw_delegate.a
  gis_absl_raw_logging_internal)
_gis_s2_archive(gis_absl_int128 lib64/libabsl_int128.a)
_gis_s2_archive(gis_absl_strings_internal lib64/libabsl_strings_internal.a
  gis_absl_base gis_absl_raw_logging_internal)
_gis_s2_archive(gis_absl_strings lib64/libabsl_strings.a
  gis_absl_strings_internal gis_absl_base gis_absl_int128 gis_absl_raw_logging_internal gis_absl_throw_delegate)
_gis_s2_archive(gis_absl_bad_optional_access lib64/libabsl_bad_optional_access.a
  gis_absl_raw_logging_internal)
_gis_s2_archive(gis_absl_bad_variant_access lib64/libabsl_bad_variant_access.a
  gis_absl_raw_logging_internal)
_gis_s2_archive(gis_absl_low_level_hash lib64/libabsl_low_level_hash.a
  gis_absl_base gis_absl_int128)
_gis_s2_archive(gis_absl_hash lib64/libabsl_hash.a
  gis_absl_city gis_absl_base gis_absl_throw_delegate gis_absl_int128 gis_absl_strings gis_absl_bad_optional_access gis_absl_bad_variant_access gis_absl_low_level_hash)
_gis_s2_archive(gis_absl_exponential_biased lib64/libabsl_exponential_biased.a)
_gis_s2_archive(gis_absl_malloc_internal lib64/libabsl_malloc_internal.a
  gis_absl_base gis_absl_raw_logging_internal pthread)
_gis_s2_archive(gis_absl_graphcycles_internal lib64/libabsl_graphcycles_internal.a
  gis_absl_base gis_absl_malloc_internal gis_absl_raw_logging_internal)
_gis_s2_archive(gis_absl_civil_time lib64/libabsl_civil_time.a)
_gis_s2_archive(gis_absl_time_zone lib64/libabsl_time_zone.a)
_gis_s2_archive(gis_absl_time lib64/libabsl_time.a
  gis_absl_base gis_absl_civil_time gis_absl_int128 gis_absl_raw_logging_internal gis_absl_strings gis_absl_time_zone)
_gis_s2_archive(gis_absl_debugging_internal lib64/libabsl_debugging_internal.a
  gis_absl_raw_logging_internal)
_gis_s2_archive(gis_absl_stacktrace lib64/libabsl_stacktrace.a
  gis_absl_debugging_internal)
_gis_s2_archive(gis_absl_demangle_internal lib64/libabsl_demangle_internal.a
  gis_absl_base)
_gis_s2_archive(gis_absl_symbolize lib64/libabsl_symbolize.a
  gis_absl_debugging_internal gis_absl_demangle_internal gis_absl_base gis_absl_malloc_internal gis_absl_raw_logging_internal gis_absl_strings)
_gis_s2_archive(gis_absl_synchronization lib64/libabsl_synchronization.a
  gis_absl_graphcycles_internal gis_absl_raw_logging_internal gis_absl_time gis_absl_base gis_absl_malloc_internal gis_absl_stacktrace gis_absl_symbolize pthread)
_gis_s2_archive(gis_absl_hashtablez_sampler lib64/libabsl_hashtablez_sampler.a
  gis_absl_base gis_absl_exponential_biased gis_absl_synchronization)
_gis_s2_archive(gis_absl_raw_hash_set lib64/libabsl_raw_hash_set.a
  gis_absl_base gis_absl_bad_optional_access gis_absl_hashtablez_sampler)
_gis_s2_archive(gis_absl_str_format_internal lib64/libabsl_str_format_internal.a
  gis_absl_strings gis_absl_int128 gis_absl_throw_delegate)
# S2 exact predicates use OpenSSL BIGNUM.
_gis_s2_archive(gis_s2_crypto lib/libcrypto.a dl pthread)
_gis_s2_archive(gis_s2 lib64/libs2.a
  gis_absl_hash gis_absl_raw_hash_set gis_absl_str_format_internal
  gis_absl_synchronization gis_absl_throw_delegate gis_absl_spinlock_wait
  gis_s2_crypto m)

