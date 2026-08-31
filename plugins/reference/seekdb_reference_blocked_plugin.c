/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "seekdb/plugin/seekdb_plugin_abi.h"

#ifndef SEEKDB_BLOCKED_PLUGIN_ID
#define SEEKDB_BLOCKED_PLUGIN_ID "org.seekdb.reference.blocked"
#endif

#ifndef SEEKDB_BLOCKED_PLUGIN_BUILD_ID
#define SEEKDB_BLOCKED_PLUGIN_BUILD_ID "reference-blocked-abi-v1"
#endif

#ifndef SEEKDB_BLOCKED_PLUGIN_FAIL_START
#define SEEKDB_BLOCKED_PLUGIN_FAIL_START 1
#endif

struct seekdb_plugin_instance_handle {
  uint32_t stop_calls;
};

static struct seekdb_plugin_instance_handle blocked_instance;

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL blocked_init(
    const seekdb_plugin_host_api_v1_t *host_api,
    seekdb_plugin_instance_handle_t **out_instance)
{
  if (NULL == host_api || NULL == out_instance ||
      host_api->struct_size < sizeof(seekdb_plugin_host_api_v1_t) ||
      host_api->abi_major != SEEKDB_PLUGIN_ABI_MAJOR) {
    return SEEKDB_PLUGIN_STATUS_UNSUPPORTED_ABI;
  }
  blocked_instance.stop_calls = 0;
  *out_instance = &blocked_instance;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL blocked_start(
    seekdb_plugin_instance_handle_t *instance)
{
  (void)instance;
  return SEEKDB_BLOCKED_PLUGIN_FAIL_START ? SEEKDB_PLUGIN_STATUS_INTERNAL
                                          : SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL blocked_stop(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance != &blocked_instance) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  ++blocked_instance.stop_calls;
  return 1 == blocked_instance.stop_calls ? SEEKDB_PLUGIN_STATUS_INTERNAL
                                          : SEEKDB_PLUGIN_STATUS_OK;
}

static void SEEKDB_PLUGIN_CALL blocked_deinit(
    seekdb_plugin_instance_handle_t *instance)
{
  (void)instance;
}

static const seekdb_plugin_manifest_v1_t blocked_manifest = {
    sizeof(seekdb_plugin_manifest_v1_t),
    SEEKDB_PLUGIN_ABI_MAJOR,
    SEEKDB_PLUGIN_ABI_MINOR,
    SEEKDB_BLOCKED_PLUGIN_ID,
    "seekdb",
    {1, 0, 0},
    SEEKDB_BLOCKED_PLUGIN_BUILD_ID,
    0,
    0,
    SEEKDB_PLUGIN_CAPABILITY_NONE,
    NULL,
    0,
    NULL,
    0,
    blocked_init,
    blocked_start,
    blocked_stop,
    blocked_deinit,
    {0, 0, 0, 0, 0, 0, 0, 0}};

SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *SEEKDB_PLUGIN_CALL
seekdb_plugin_entry_v1(void)
{
  return &blocked_manifest;
}
