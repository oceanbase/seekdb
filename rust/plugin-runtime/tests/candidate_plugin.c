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

/* Real Rust callback; this file supplies only the loader manifest/registration. */
#include "seekdb/plugin/server_dev.h"
#include "seekdb/plugin/server_dev_planner.h"
#include "seekdb/plugin/extension_spi.h"
#include "server_dev_contract.h"
#include <stdlib.h>
extern seekdb_plugin_status_t candidate_around(seekdb_plugin_instance_handle_t *, const seekdb_plugin_candidate_context_v1_t *);
extern seekdb_plugin_status_t candidate_replace(seekdb_plugin_instance_handle_t *, const seekdb_plugin_candidate_context_v1_t *);
extern seekdb_plugin_status_t candidate_register(const seekdb_plugin_host_api_v1_t *);
extern seekdb_plugin_status_t candidate_register_relation(const seekdb_plugin_host_api_v1_t *);
extern seekdb_plugin_status_t candidate_contribute(seekdb_plugin_instance_handle_t *, const seekdb_plugin_candidate_context_v1_t *);
extern seekdb_plugin_status_t candidate_construct(seekdb_plugin_instance_handle_t *, const seekdb_plugin_candidate_context_v1_t *);
static uint32_t variant = VARIANT;
#if VARIANT >= 13
#if VARIANT == 14
#define MODE SEEKDB_PLUGIN_CANDIDATE_REPLACE
#else
#define MODE SEEKDB_PLUGIN_CANDIDATE_AROUND
#endif
#define CALLBACK candidate_contribute
#elif VARIANT >= 9
#define MODE SEEKDB_PLUGIN_CANDIDATE_AROUND
#define CALLBACK candidate_construct
#elif VARIANT == 1 || VARIANT == 5 || VARIANT == 6 || VARIANT == 7
#define MODE SEEKDB_PLUGIN_CANDIDATE_AROUND
#define CALLBACK candidate_around
#else
#define MODE SEEKDB_PLUGIN_CANDIDATE_REPLACE
#define CALLBACK candidate_replace
#endif
static const seekdb_plugin_candidate_service_v1_t service = {
  sizeof(service), 1,
#if VARIANT >= 9 && VARIANT != 15
  1,
#else
  0,
#endif
  MODE, CALLBACK, {0}
};
static const seekdb_plugin_service_provide_descriptor_t provides = {
  sizeof(provides), "org.seekdb.candidate.policy", {1, 0, 0}, &service, 0, {0}
};
static seekdb_plugin_status_t init(const seekdb_plugin_host_api_v1_t *host, seekdb_plugin_instance_handle_t **out) {
  if (!host || !out) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  seekdb_plugin_status_t status = VARIANT >= 13 ? candidate_register_relation(host) : candidate_register(host);
  *out = (seekdb_plugin_instance_handle_t *)&variant;
  return status;
}
static seekdb_plugin_status_t lifecycle(seekdb_plugin_instance_handle_t *value) {
  return value == (seekdb_plugin_instance_handle_t *)&variant ? SEEKDB_PLUGIN_STATUS_OK : SEEKDB_PLUGIN_STATUS_INTERNAL;
}
static void deinit(seekdb_plugin_instance_handle_t *value) { if (lifecycle(value)) abort(); }
SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *SEEKDB_PLUGIN_CALL seekdb_plugin_entry_v1(void) {
  static const seekdb_plugin_server_dev_manifest_v1_t manifest = {
    {sizeof(manifest), SEEKDB_PLUGIN_ABI_MAJOR, SEEKDB_PLUGIN_ABI_MINOR,
     "org.seekdb.sql_extension", "test", {1, 0, 0}, "sql-extension-catalog-v1",
     1, 1,
#if VARIANT == 8 || VARIANT == 16
     0,
#else
     SEEKDB_PLUGIN_CAPABILITY_SERVER_DEV,
#endif
     &provides, 1, NULL, 0, init, lifecycle, lifecycle, deinit, {0}},
    SEEKDB_PLUGIN_SERVER_DEV_BRIDGE_VERSION, SEEKDB_SERVER_DEV_HOST_BUILD_ID_SIZE,
    SEEKDB_SERVER_DEV_HOST_BUILD_ID_BYTES, {0}
  };
  return &manifest.v1;
}
