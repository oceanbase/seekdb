# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
# Deliberately outside CTest/default builds until the real startup link closes.
# Undefined symbols are errors; there are no permissive stubs or fake services.
add_executable(seekdb_wasm_engine_link_probe EXCLUDE_FROM_ALL
  "${SEEKDB_ROOT}/unittest/wasm/engine_link_probe.cpp")
target_link_libraries(seekdb_wasm_engine_link_probe PRIVATE
  seekdb_wasm_engine_options
  seekdb_wasm_observer seekdb_wasm_sql seekdb_wasm_storage seekdb_wasm_share
  seekdb_wasm_logservice seekdb_wasm_rootserver seekdb_wasm_pl
  seekdb_wasm_query seekdb_wasm_data_plane seekdb_wasm_parser
  seekdb_wasm_oblib_common seekdb_wasm_oblib_lib seekdb_wasm_rpc
  seekdb_wasm_compression seekdb_wasm_restore seekdb_wasm_memory
  seekdb_wasm_sqlite seekdb_wasm_crypto seekdb_wasm_startup_sources)
target_link_options(seekdb_wasm_engine_link_probe PRIVATE --no-entry
  -Oz -pthread -msimd128 -sUSE_ZLIB=1 -sINITIAL_MEMORY=134217728
  -sSTACK_SIZE=1048576 -sASSERTIONS=2 -Wl,--error-limit=0)
