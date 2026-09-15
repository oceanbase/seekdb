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

#include "seekdb/plugin/extension_spi.h"

#ifndef SEEKDB_INVALID_EXTENSIONS_PLUGIN_ID
#define SEEKDB_INVALID_EXTENSIONS_PLUGIN_ID \
  "org.seekdb.reference.invalid-extensions"
#endif

#ifndef SEEKDB_INVALID_EXTENSIONS_PLUGIN_BUILD_ID
#define SEEKDB_INVALID_EXTENSIONS_PLUGIN_BUILD_ID \
  "reference-invalid-extensions-abi-v1"
#endif

#ifndef SEEKDB_INVALID_EXTENSIONS_MANIFEST_CAPABILITIES
#define SEEKDB_INVALID_EXTENSIONS_MANIFEST_CAPABILITIES \
  SEEKDB_PLUGIN_CAPABILITY_NONE
#endif

typedef struct invalid_implementation_service_v1 {
  uint32_t struct_size;
} invalid_implementation_service_v1_t;

struct seekdb_plugin_instance_handle {
  uint8_t started;
};

static struct seekdb_plugin_instance_handle invalid_instance;
static const invalid_implementation_service_v1_t implementation_service = {
    sizeof(invalid_implementation_service_v1_t)};

#define INVALID_FIXTURE_IMPLEMENTATION                                    \
  {                                                                       \
    sizeof(seekdb_plugin_implementation_ref_v1_t),                        \
    "org.seekdb.reference.invalid-extensions.impl",                      \
    {                                                                     \
      sizeof(seekdb_plugin_version_range_t),                              \
      {1, 0, 0},                                                          \
      {2, 0, 0},                                                          \
      {0, 0}                                                              \
    },                                                                    \
    SEEKDB_PLUGIN_CAPABILITY_NONE,                                        \
    {0, 0, 0, 0}                                                          \
  }

static const seekdb_plugin_function_descriptor_v1_t invalid_functions[] = {
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.reference.invalid-extensions.function.first",
      "invalid_extension_first",
      0,
      0,
      "core.type.bytes",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC,
      INVALID_FIXTURE_IMPLEMENTATION,
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.reference.invalid-extensions.function.second",
      "invalid_extension_second",
      0,
      0,
      "core.type.bytes",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC,
      INVALID_FIXTURE_IMPLEMENTATION,
      {0, 0, 0, 0}
    }
};

static const seekdb_plugin_extension_snapshot_v1_t invalid_snapshot = {
    sizeof(seekdb_plugin_extension_snapshot_v1_t),
    NULL,
    0,
    0,
    invalid_functions,
    2,
    sizeof(invalid_functions) - 1,
    NULL,
    0,
    0,
    NULL,
    0,
    0,
    NULL,
    0,
    0,
    NULL,
    0,
    0,
    NULL,
    0,
    0,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL describe_invalid_extensions(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_extension_snapshot_v1_t **out_snapshot)
{
  if (NULL == out_snapshot || instance != &invalid_instance ||
      !invalid_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  *out_snapshot = &invalid_snapshot;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static const seekdb_plugin_extension_catalog_service_v1_t
    invalid_catalog_service = {
        sizeof(seekdb_plugin_extension_catalog_service_v1_t),
        describe_invalid_extensions,
        {0, 0, 0, 0, 0, 0, 0, 0}};

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL invalid_init(
    const seekdb_plugin_host_api_v1_t *host_api,
    seekdb_plugin_instance_handle_t **out_instance)
{
  if (NULL == host_api || NULL == out_instance ||
      host_api->struct_size < sizeof(seekdb_plugin_host_api_v1_t) ||
      host_api->abi_major != SEEKDB_PLUGIN_ABI_MAJOR) {
    return SEEKDB_PLUGIN_STATUS_UNSUPPORTED_ABI;
  }
  invalid_instance.started = 0;
  *out_instance = &invalid_instance;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL invalid_start(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance != &invalid_instance || invalid_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  invalid_instance.started = 1;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL invalid_stop(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance != &invalid_instance || !invalid_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  invalid_instance.started = 0;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static void SEEKDB_PLUGIN_CALL invalid_deinit(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance == &invalid_instance) {
    invalid_instance.started = 0;
  }
}

static const seekdb_plugin_service_provide_descriptor_t invalid_services[] = {
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.reference.invalid-extensions.impl",
      {1, 0, 0},
      &implementation_service,
      SEEKDB_PLUGIN_CAPABILITY_NONE,
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.reference.invalid-extensions.catalog",
      {SEEKDB_PLUGIN_EXTENSION_SPI_MAJOR, SEEKDB_PLUGIN_EXTENSION_SPI_MINOR, 0},
      &invalid_catalog_service,
      SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG,
      {0, 0, 0, 0}
    }
};

static const seekdb_plugin_manifest_v1_t invalid_manifest = {
    sizeof(seekdb_plugin_manifest_v1_t),
    SEEKDB_PLUGIN_ABI_MAJOR,
    SEEKDB_PLUGIN_ABI_MINOR,
    SEEKDB_INVALID_EXTENSIONS_PLUGIN_ID,
    "seekdb",
    {1, 0, 0},
    SEEKDB_INVALID_EXTENSIONS_PLUGIN_BUILD_ID,
    0,
    0,
    SEEKDB_INVALID_EXTENSIONS_MANIFEST_CAPABILITIES,
    invalid_services,
    2,
    NULL,
    0,
    invalid_init,
    invalid_start,
    invalid_stop,
    invalid_deinit,
    {0, 0, 0, 0, 0, 0, 0, 0}};

SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *SEEKDB_PLUGIN_CALL
seekdb_plugin_entry_v1(void)
{
  return &invalid_manifest;
}

#undef INVALID_FIXTURE_IMPLEMENTATION
