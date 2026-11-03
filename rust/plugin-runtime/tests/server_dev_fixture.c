/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "seekdb/plugin/server_dev.h"
#include "server_dev_contract.h"
#include <stdlib.h>
static int instance;
static seekdb_plugin_status_t init(const seekdb_plugin_host_api_v1_t *host,
    seekdb_plugin_instance_handle_t **out) {
#if VARIANT != 0
  abort(); /* A rejected contract must never reach lifecycle init. */
#endif
  if (!host || !out) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  *out = (seekdb_plugin_instance_handle_t *)&instance;
  return SEEKDB_PLUGIN_STATUS_OK;
}
static seekdb_plugin_status_t lifecycle(seekdb_plugin_instance_handle_t *value) {
  return value == (seekdb_plugin_instance_handle_t *)&instance ? SEEKDB_PLUGIN_STATUS_OK : SEEKDB_PLUGIN_STATUS_INTERNAL;
}
static void deinit(seekdb_plugin_instance_handle_t *value) {
  if (lifecycle(value) != SEEKDB_PLUGIN_STATUS_OK) abort();
}
SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *SEEKDB_PLUGIN_CALL seekdb_plugin_entry_v1(void) {
  static seekdb_plugin_server_dev_manifest_v1_t manifest = {
    {sizeof(manifest), SEEKDB_PLUGIN_ABI_MAJOR, SEEKDB_PLUGIN_ABI_MINOR,
     "org.seekdb.sql_extension", "test", {1, 0, 0}, "sql-extension-catalog-v1",
     1, 1, SEEKDB_PLUGIN_CAPABILITY_SERVER_DEV, NULL, 0, NULL, 0,
     init, lifecycle, lifecycle, deinit, {0}},
    SEEKDB_PLUGIN_SERVER_DEV_BRIDGE_VERSION, SEEKDB_SERVER_DEV_HOST_BUILD_ID_SIZE,
    SEEKDB_SERVER_DEV_HOST_BUILD_ID_BYTES, {0}
  };
#if VARIANT == 1
  manifest.host_build_id[0] ^= 1;
#elif VARIANT == 2
  manifest.bridge_version++;
#elif VARIANT == 3
  manifest.v1.struct_size = sizeof(manifest.v1);
#elif VARIANT == 4
  manifest.reserved[0] = 1;
#elif VARIANT == 5
  manifest.host_build_id[63] = 1;
#elif VARIANT == 6
  manifest.host_build_id_size = 0;
#endif
  return &manifest.v1;
}
