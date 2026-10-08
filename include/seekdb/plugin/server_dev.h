/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_PLUGIN_SERVER_DEV_H_
#define SEEKDB_PLUGIN_SERVER_DEV_H_
#include "seekdb/plugin/seekdb_plugin_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Set the manifest-only SERVER_DEV bit and v1.struct_size = sizeof(this).
 * Checked before init/start, but after OS loading/entry (constructors are not
 * sandboxed). Linked host identity is not file integrity or a signature.
 * Matching grants no raw server pointers, exports, or planner replacement;
 * those require separate host APIs and the matching SDK/server headers.
 * Initial implementation: Linux ELF64 little-endian hosts only. */
#define SEEKDB_PLUGIN_SERVER_DEV_BRIDGE_VERSION 1u
typedef struct seekdb_plugin_server_dev_manifest_v1 {
  seekdb_plugin_manifest_v1_t v1;
  uint32_t bridge_version;
  uint32_t host_build_id_size;
  uint8_t host_build_id[64];
  uint64_t reserved[4];
} seekdb_plugin_server_dev_manifest_v1_t;
#ifdef __cplusplus
}
#endif
#endif
