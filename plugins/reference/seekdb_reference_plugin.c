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

#include <string.h>

typedef struct seekdb_reference_echo_service_v1 {
  uint32_t struct_size;
  seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *echo)(
      const uint8_t *input,
      uint64_t input_size,
      uint8_t *output,
      uint64_t *in_out_output_size);
} seekdb_reference_echo_service_v1_t;

struct seekdb_plugin_instance_handle {
  const seekdb_plugin_host_api_v1_t *host_api;
  uint8_t started;
};

static struct seekdb_plugin_instance_handle reference_instance;

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL reference_echo(
    const uint8_t *input,
    uint64_t input_size,
    uint8_t *output,
    uint64_t *in_out_output_size)
{
  seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
  if (NULL == in_out_output_size || (0 != input_size && NULL == input)) {
    status = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  } else if (*in_out_output_size < input_size || (0 != input_size && NULL == output)) {
    *in_out_output_size = input_size;
    status = SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  } else {
    if (0 != input_size) {
      memcpy(output, input, (size_t)input_size);
    }
    *in_out_output_size = input_size;
  }
  return status;
}

static const seekdb_reference_echo_service_v1_t echo_service = {
    sizeof(seekdb_reference_echo_service_v1_t),
    reference_echo};

static const seekdb_plugin_service_provide_descriptor_t dynamic_echo_service = {
    sizeof(seekdb_plugin_service_provide_descriptor_t),
    "org.seekdb.reference.dynamic-echo",
    {1, 0, 0},
    &echo_service,
    SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
    {0, 0, 0, 0}
};

#define REFERENCE_ECHO_IMPLEMENTATION                                      \
  {                                                                        \
    sizeof(seekdb_plugin_implementation_ref_v1_t),                         \
    "org.seekdb.reference.echo",                                          \
    {                                                                      \
      sizeof(seekdb_plugin_version_range_t),                               \
      {1, 0, 0},                                                           \
      {2, 0, 0},                                                           \
      {0, 0}                                                               \
    },                                                                     \
    SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,                                  \
    {0, 0, 0, 0}                                                           \
  }

static const seekdb_plugin_type_descriptor_v1_t extension_types[] = {
    {
      sizeof(seekdb_plugin_type_descriptor_v1_t),
      "org.seekdb.reference.type.echo",
      "reference_echo_type",
      "org.seekdb.reference.format.echo",
      1,
      0,
      SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      REFERENCE_ECHO_IMPLEMENTATION,
      {0, 0, 0, 0}
    }
};

typedef struct reference_padded_function_descriptor {
  seekdb_plugin_function_descriptor_v1_t descriptor;
  uint64_t trailing_v1_compatible_words[2];
} reference_padded_function_descriptor_t;

typedef struct reference_function_descriptor_block {
  reference_padded_function_descriptor_t padded;
  seekdb_plugin_function_descriptor_v1_t compact;
} reference_function_descriptor_block_t;

/*
 * Exercise the snapshot's forward-compatible byte-stride contract with two
 * elements: the walker must skip the first element's trailing words before it
 * can normalize the compact second descriptor.
 */
static const reference_function_descriptor_block_t extension_functions = {
  {
    {
      offsetof(reference_function_descriptor_block_t, compact),
      "org.seekdb.reference.function.echo",
      "reference_echo",
      1,
      1,
      "org.seekdb.reference.type.echo",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING |
          SEEKDB_PLUGIN_EXTENSION_FLAG_PARALLEL_SAFE,
      REFERENCE_ECHO_IMPLEMENTATION,
      {0, 0, 0, 0}
    },
    {0, 0}
  },
  {
    sizeof(seekdb_plugin_function_descriptor_v1_t),
    "org.seekdb.reference.function.echo-pair",
    "reference_echo_pair",
    2,
    2,
    "org.seekdb.reference.type.echo",
    SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
        SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
        SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING |
        SEEKDB_PLUGIN_EXTENSION_FLAG_PARALLEL_SAFE,
    REFERENCE_ECHO_IMPLEMENTATION,
    {0, 0, 0, 0}
  }
};

static const seekdb_plugin_cast_descriptor_v1_t extension_casts[] = {
    {
      sizeof(seekdb_plugin_cast_descriptor_v1_t),
      "org.seekdb.reference.cast.varchar-to-echo",
      "core.varchar",
      "org.seekdb.reference.type.echo",
      SEEKDB_PLUGIN_CAST_EXPLICIT,
      10,
      SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      REFERENCE_ECHO_IMPLEMENTATION,
      {0, 0, 0, 0}
    }
};

static const seekdb_plugin_index_access_method_descriptor_v1_t
    extension_index_access_methods[] = {
    {
      sizeof(seekdb_plugin_index_access_method_descriptor_v1_t),
      "org.seekdb.reference.index.echo",
      "reference_echo_index",
      SEEKDB_PLUGIN_EXTENSION_FLAG_PARALLEL_SAFE,
      REFERENCE_ECHO_IMPLEMENTATION,
      {0, 0, 0, 0}
    }
};

static const seekdb_plugin_optimizer_hook_descriptor_v1_t
    extension_optimizer_hooks[] = {
    {
      sizeof(seekdb_plugin_optimizer_hook_descriptor_v1_t),
      "org.seekdb.reference.optimizer.echo",
      "optimizer.post_rewrite",
      100,
      0,
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC,
      REFERENCE_ECHO_IMPLEMENTATION,
      {0, 0, 0, 0}
    }
};

static const seekdb_plugin_das_hook_descriptor_v1_t extension_das_hooks[] = {
    {
      sizeof(seekdb_plugin_das_hook_descriptor_v1_t),
      "org.seekdb.reference.das.echo",
      "das.pre_execute",
      100,
      0,
      SEEKDB_PLUGIN_EXTENSION_FLAG_PARALLEL_SAFE,
      REFERENCE_ECHO_IMPLEMENTATION,
      {0, 0, 0, 0}
    }
};

static const seekdb_plugin_catalog_object_descriptor_v1_t
    extension_catalog_objects[] = {
    {
      sizeof(seekdb_plugin_catalog_object_descriptor_v1_t),
      "org.seekdb.reference.catalog.echo",
      "extension.fixture",
      "sys",
      "reference_echo_catalog",
      "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT |
          SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG,
      {0, 0, 0, 0}
    }
};

static const seekdb_plugin_extension_snapshot_v1_t extension_snapshot = {
    sizeof(seekdb_plugin_extension_snapshot_v1_t),
    extension_types,
    1,
    sizeof(extension_types),
    (const seekdb_plugin_function_descriptor_v1_t *)&extension_functions,
    2,
    sizeof(extension_functions),
    extension_casts,
    1,
    sizeof(extension_casts),
    extension_index_access_methods,
    1,
    sizeof(extension_index_access_methods),
    extension_optimizer_hooks,
    1,
    sizeof(extension_optimizer_hooks),
    extension_das_hooks,
    1,
    sizeof(extension_das_hooks),
    extension_catalog_objects,
    1,
    sizeof(extension_catalog_objects),
    {0, 0, 0, 0, 0, 0, 0, 0}
};

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL reference_describe_extensions(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_extension_snapshot_v1_t **out_snapshot)
{
  seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
  if (NULL != out_snapshot) {
    *out_snapshot = NULL;
  }
  if (instance != &reference_instance || !reference_instance.started ||
      NULL == out_snapshot) {
    status = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  } else {
    *out_snapshot = &extension_snapshot;
  }
  return status;
}

static const seekdb_plugin_extension_catalog_service_v1_t
    extension_catalog_service = {
    sizeof(seekdb_plugin_extension_catalog_service_v1_t),
    reference_describe_extensions,
    {0, 0, 0, 0, 0, 0, 0, 0}
};

#undef REFERENCE_ECHO_IMPLEMENTATION

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL reference_init(
    const seekdb_plugin_host_api_v1_t *host_api,
    seekdb_plugin_instance_handle_t **out_instance)
{
  seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
  seekdb_plugin_registration_txn_t *registration = NULL;
  if (NULL == host_api || NULL == out_instance
      || host_api->struct_size < sizeof(seekdb_plugin_host_api_v1_t)
      || SEEKDB_PLUGIN_ABI_MAJOR != host_api->abi_major
      || NULL == host_api->begin_registration
      || NULL == host_api->register_service
      || NULL == host_api->commit_registration
      || NULL == host_api->abort_registration) {
    status = SEEKDB_PLUGIN_STATUS_UNSUPPORTED_ABI;
  } else {
    status = host_api->begin_registration(host_api->host_handle, &registration);
    if (SEEKDB_PLUGIN_STATUS_OK == status) {
      status = host_api->register_service(
          host_api->host_handle, registration, &dynamic_echo_service);
    }
    if (SEEKDB_PLUGIN_STATUS_OK == status) {
      status = host_api->commit_registration(host_api->host_handle, registration);
      if (SEEKDB_PLUGIN_STATUS_OK == status) registration = NULL;
    }
    if (SEEKDB_PLUGIN_STATUS_OK != status && NULL != registration) {
      host_api->abort_registration(host_api->host_handle, registration);
    } else if (SEEKDB_PLUGIN_STATUS_OK == status) {
      reference_instance.host_api = host_api;
      reference_instance.started = 0;
      *out_instance = &reference_instance;
      if (NULL != host_api->log) {
        host_api->log(host_api->host_handle, SEEKDB_PLUGIN_LOG_INFO,
                      "org.seekdb.reference", "initialized");
      }
    }
  }
  return status;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL reference_start(
    seekdb_plugin_instance_handle_t *instance)
{
  seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
  if (instance != &reference_instance || reference_instance.started) {
    status = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  } else {
    reference_instance.started = 1;
  }
  return status;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL reference_stop(
    seekdb_plugin_instance_handle_t *instance)
{
  seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
  if (instance != &reference_instance || !reference_instance.started) {
    status = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  } else {
    reference_instance.started = 0;
  }
  return status;
}

static void SEEKDB_PLUGIN_CALL reference_deinit(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance == &reference_instance) {
    reference_instance.host_api = NULL;
    reference_instance.started = 0;
  }
}

static const seekdb_plugin_service_provide_descriptor_t provided_services[] = {
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.reference.echo",
      {1, 0, 0},
      &echo_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.reference.extensions",
      {SEEKDB_PLUGIN_EXTENSION_SPI_MAJOR, SEEKDB_PLUGIN_EXTENSION_SPI_MINOR, 0},
      &extension_catalog_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE |
          SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG,
      {0, 0, 0, 0}
    }
};

static const seekdb_plugin_manifest_v1_t reference_manifest = {
    sizeof(seekdb_plugin_manifest_v1_t),
    SEEKDB_PLUGIN_ABI_MAJOR,
    SEEKDB_PLUGIN_ABI_MINOR,
    "org.seekdb.reference",
    "seekdb",
    {1, 0, 0},
    "reference-abi-v1",
    1,
    0,
    SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE |
        SEEKDB_PLUGIN_CAPABILITY_PERSISTENT_DATA,
    provided_services,
    2,
    NULL,
    0,
    reference_init,
    reference_start,
    reference_stop,
    reference_deinit,
    {0, 0, 0, 0, 0, 0, 0, 0}
};

SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *SEEKDB_PLUGIN_CALL
seekdb_plugin_entry_v1(void)
{
  return &reference_manifest;
}
