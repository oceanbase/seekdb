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

#include <stdio.h>

typedef struct reference_registration_service_v1 {
  uint32_t struct_size;
} reference_registration_service_v1_t;

struct seekdb_plugin_instance_handle {
  uint8_t started;
};

static struct seekdb_plugin_instance_handle registration_instance;
static const reference_registration_service_v1_t registration_service = {
    sizeof(reference_registration_service_v1_t)};

static const seekdb_plugin_service_provide_descriptor_t shared_service = {
    sizeof(seekdb_plugin_service_provide_descriptor_t),
    "org.seekdb.reference.registration-conflict.shared",
    {1, 0, 0},
    &registration_service,
    SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
    {0, 0, 0, 0}};

static const seekdb_plugin_service_provide_descriptor_t after_abort_service = {
    sizeof(seekdb_plugin_service_provide_descriptor_t),
    "org.seekdb.reference.registration-conflict.after-abort",
    {1, 0, 0},
    &registration_service,
    SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
    {0, 0, 0, 0}};

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL registration_conflict_init(
    const seekdb_plugin_host_api_v1_t *host_api,
    seekdb_plugin_instance_handle_t **out_instance)
{
  seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
  seekdb_plugin_status_t observed = SEEKDB_PLUGIN_STATUS_OK;
  seekdb_plugin_registration_txn_t *open_transactions[SEEKDB_PLUGIN_MAX_SERVICES] = {0};
  seekdb_plugin_registration_txn_t *overflow_transaction = NULL;
  seekdb_plugin_registration_txn_t *pending_first = NULL;
  seekdb_plugin_registration_txn_t *pending_second = NULL;
  seekdb_plugin_registration_txn_t *first = NULL;
  seekdb_plugin_registration_txn_t *second = NULL;
  seekdb_plugin_registration_txn_t *after_abort = NULL;
  uint32_t open_count = 0;
  uint32_t pending_count = 0;
  char pending_service_id[96];
  seekdb_plugin_service_provide_descriptor_t pending_service = {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      NULL,
      {1, 0, 0},
      &registration_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
      {0, 0, 0, 0}};

  if (NULL == host_api || NULL == out_instance ||
      host_api->struct_size < sizeof(seekdb_plugin_host_api_v1_t) ||
      SEEKDB_PLUGIN_ABI_MAJOR != host_api->abi_major ||
      NULL == host_api->begin_registration ||
      NULL == host_api->register_service ||
      NULL == host_api->commit_registration ||
      NULL == host_api->abort_registration) {
    status = SEEKDB_PLUGIN_STATUS_UNSUPPORTED_ABI;
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    for (open_count = 0;
         open_count < SEEKDB_PLUGIN_MAX_SERVICES &&
             SEEKDB_PLUGIN_STATUS_OK == status;
         ++open_count) {
      status = host_api->begin_registration(
          host_api->host_handle, &open_transactions[open_count]);
    }
    if (SEEKDB_PLUGIN_STATUS_OK == status) {
      observed = host_api->begin_registration(
          host_api->host_handle, &overflow_transaction);
      if (SEEKDB_PLUGIN_STATUS_OK == observed) {
        host_api->abort_registration(
            host_api->host_handle, overflow_transaction);
        overflow_transaction = NULL;
        status = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
      } else if (SEEKDB_PLUGIN_STATUS_INVALID_MANIFEST != observed) {
        status = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
      }
    }
    while (open_count > 0) {
      --open_count;
      host_api->abort_registration(
          host_api->host_handle, open_transactions[open_count]);
      open_transactions[open_count] = NULL;
    }
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    status = host_api->begin_registration(
        host_api->host_handle, &pending_first);
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    status = host_api->begin_registration(
        host_api->host_handle, &pending_second);
  }
  for (pending_count = 0;
       pending_count < SEEKDB_PLUGIN_MAX_SERVICES &&
           SEEKDB_PLUGIN_STATUS_OK == status;
       ++pending_count) {
    const int length = snprintf(
        pending_service_id, sizeof(pending_service_id),
        "org.seekdb.reference.registration-conflict.pending.%u",
        pending_count);
    if (length <= 0 || (size_t)length >= sizeof(pending_service_id)) {
      status = SEEKDB_PLUGIN_STATUS_INTERNAL;
    } else {
      pending_service.service_id = pending_service_id;
      status = host_api->register_service(
          host_api->host_handle,
          0 == (pending_count & 1u) ? pending_first : pending_second,
          &pending_service);
    }
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    pending_service.service_id =
        "org.seekdb.reference.registration-conflict.pending.overflow";
    observed = host_api->register_service(
        host_api->host_handle, pending_first, &pending_service);
    if (SEEKDB_PLUGIN_STATUS_OK == observed) {
      status = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
    } else if (SEEKDB_PLUGIN_STATUS_INVALID_MANIFEST != observed) {
      status = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
    }
  }
  if (NULL != pending_second) {
    host_api->abort_registration(host_api->host_handle, pending_second);
    pending_second = NULL;
  }
  if (NULL != pending_first) {
    host_api->abort_registration(host_api->host_handle, pending_first);
    pending_first = NULL;
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    status = host_api->begin_registration(host_api->host_handle, &first);
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    status = host_api->begin_registration(host_api->host_handle, &second);
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    status = host_api->register_service(
        host_api->host_handle, first, &shared_service);
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    status = host_api->register_service(
        host_api->host_handle, second, &shared_service);
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    status = host_api->commit_registration(host_api->host_handle, first);
    if (SEEKDB_PLUGIN_STATUS_OK == status) {
      first = NULL;
    }
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    observed = host_api->commit_registration(host_api->host_handle, second);
    if (SEEKDB_PLUGIN_STATUS_OK == observed) {
      /* A successful commit consumes the transaction even though it is a bug. */
      second = NULL;
      status = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
    } else if (SEEKDB_PLUGIN_STATUS_ALREADY_EXISTS != observed) {
      status = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
    }
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    /* A rejected commit leaves the transaction live and abortable. */
    host_api->abort_registration(host_api->host_handle, second);
    second = NULL;
    status = host_api->begin_registration(
        host_api->host_handle, &after_abort);
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    status = host_api->register_service(
        host_api->host_handle, after_abort, &after_abort_service);
  }
  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    status = host_api->commit_registration(
        host_api->host_handle, after_abort);
    if (SEEKDB_PLUGIN_STATUS_OK == status) {
      after_abort = NULL;
    }
  }

  if (NULL != after_abort) {
    host_api->abort_registration(host_api->host_handle, after_abort);
  }
  if (NULL != second) {
    host_api->abort_registration(host_api->host_handle, second);
  }
  if (NULL != first) {
    host_api->abort_registration(host_api->host_handle, first);
  }

  if (SEEKDB_PLUGIN_STATUS_OK == status) {
    registration_instance.started = 0;
    *out_instance = &registration_instance;
  }
  return status;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL registration_conflict_start(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance != &registration_instance || registration_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  registration_instance.started = 1;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL registration_conflict_stop(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance != &registration_instance || !registration_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  registration_instance.started = 0;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static void SEEKDB_PLUGIN_CALL registration_conflict_deinit(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance == &registration_instance) {
    registration_instance.started = 0;
  }
}

static const seekdb_plugin_manifest_v1_t registration_conflict_manifest = {
    sizeof(seekdb_plugin_manifest_v1_t),
    SEEKDB_PLUGIN_ABI_MAJOR,
    SEEKDB_PLUGIN_ABI_MINOR,
    "org.seekdb.reference.registration-conflict",
    "seekdb",
    {1, 0, 0},
    "reference-registration-conflict-abi-v1",
    0,
    0,
    SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
    NULL,
    0,
    NULL,
    0,
    registration_conflict_init,
    registration_conflict_start,
    registration_conflict_stop,
    registration_conflict_deinit,
    {0, 0, 0, 0, 0, 0, 0, 0}};

SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *SEEKDB_PLUGIN_CALL
seekdb_plugin_entry_v1(void)
{
  return &registration_conflict_manifest;
}
