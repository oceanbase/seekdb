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
    0,
    0,
    SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
    provided_services,
    1,
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
