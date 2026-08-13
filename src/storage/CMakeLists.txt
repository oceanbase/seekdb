ob_set_subtarget(ob_storage_tablet_autoincrement_state runtime
  ${SEEKDB_STORAGE_TABLET_AUTOINCREMENT_STATE})
ob_server_add_target(ob_storage_tablet_autoincrement_state)

seekdb_apply_unity_inventory(ob_storage SEEKDB_STORAGE_UNITY)
seekdb_apply_standalone_inventory(ob_storage SEEKDB_STORAGE_STANDALONE)
seekdb_apply_standalone_inventory(ob_storage_extra SEEKDB_STORAGE_EXTRA)

ob_add_new_object_target(ob_storage ob_storage)
target_link_libraries(ob_storage PUBLIC ob_base)

# Keep the legacy aggregate target name used by the final link.  The current
# production inventory has no Storage extra sources, so avoid creating an
# empty object library.
add_library(ob_storage_static STATIC EXCLUDE_FROM_ALL)
target_link_libraries(ob_storage_static PUBLIC ob_storage)

seekdb_apply_unity_inventory(ob_storage_simd SEEKDB_STORAGE_SIMD_UNITY)
ob_server_add_target(ob_storage_simd)
if(ARCHITECTURE STREQUAL "x86_64" OR ARCHITECTURE STREQUAL "amd64")
  target_compile_options(ob_storage_simd PRIVATE
    -mtune=core-avx2 -mavx2 -mfma -mbmi2 -mavx512vl -mavx512bw)
endif()
