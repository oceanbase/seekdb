// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "seekdb/plugin/seekdb_plugin_abi.h"
#include "sql/optimizer/ob_optimizer.h"

// Demonstrates access to a real inline server type, not a planner hook. No
// copy of an optimizer implementation or core archive is linked into this DSO.
extern "C" SEEKDB_PLUGIN_EXPORT uint64_t seekdb_server_dev_numbering_probe()
{
  oceanbase::sql::NumberingCtx numbering;
  return numbering.num_ + numbering.branch_id_ + numbering.op_id_;
}
static int instance;
static seekdb_plugin_status_t init(const seekdb_plugin_host_api_v1_t *host,
    seekdb_plugin_instance_handle_t **out)
{
  if (!host || !out || seekdb_server_dev_numbering_probe() != 0)
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  *out = reinterpret_cast<seekdb_plugin_instance_handle_t *>(&instance);
  return SEEKDB_PLUGIN_STATUS_OK;
}
static seekdb_plugin_status_t lifecycle(seekdb_plugin_instance_handle_t *value)
{
  return value == reinterpret_cast<seekdb_plugin_instance_handle_t *>(&instance)
    ? SEEKDB_PLUGIN_STATUS_OK : SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
}
static void deinit(seekdb_plugin_instance_handle_t *) {}

extern "C" SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *SEEKDB_PLUGIN_CALL
seekdb_plugin_entry_v1(void)
{
  // The build helper wraps this ordinary manifest with the linked-host
  // contract. Authors do not embed or copy an executable build ID manually.
  static const seekdb_plugin_manifest_v1_t manifest = {
    sizeof(manifest), SEEKDB_PLUGIN_ABI_MAJOR, SEEKDB_PLUGIN_ABI_MINOR,
    "org.seekdb.server_dev_reference", "Server-dev reference", {1, 0, 0},
    "server-dev-reference-v1", 1, 0, 0, nullptr, 0, nullptr, 0,
    init, lifecycle, lifecycle, deinit, {0}
  };
  return &manifest;
}
