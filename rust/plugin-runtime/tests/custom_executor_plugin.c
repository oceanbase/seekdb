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

#include "seekdb/plugin/server_dev.h"
#include "seekdb/plugin/server_dev_executor.h"
#include "server_dev_contract.h"
#include <stdlib.h>
#include <stdatomic.h>
static int instance;
static _Atomic int allocated, closed;
static seekdb_plugin_status_t init(const seekdb_plugin_host_api_v1_t *host, seekdb_plugin_instance_handle_t **out) {
  if (!host || !out) abort(); *out = (seekdb_plugin_instance_handle_t *)&instance; return 0;
}
static seekdb_plugin_status_t lifecycle(seekdb_plugin_instance_handle_t *p) { if (p != (void *)&instance) abort(); return 0; }
static void deinit(seekdb_plugin_instance_handle_t *p) { if (lifecycle(p) || allocated != closed) abort(); }
static seekdb_plugin_status_t open_cursor(seekdb_plugin_instance_handle_t *p, const uint8_t *plan, uint32_t size, void **out) {
  (void)plan; if (lifecycle(p) || size) abort();
  if (VARIANT == 8) { *out = NULL; return 0; }
  *out = malloc(1); if (!*out) abort(); ++allocated;
  return VARIANT == 9 ? SEEKDB_PLUGIN_STATUS_INTERNAL : 0;
}
static seekdb_plugin_status_t next_cursor(seekdb_plugin_instance_handle_t *p, void *cursor, const seekdb_plugin_custom_context_v1_t *c) {
  if (lifecycle(p) || !cursor) abort();
  if (VARIANT < 21 && c->struct_size != sizeof(*c) && c->struct_size != sizeof(seekdb_plugin_custom_context_v2_t)) abort();
  if (VARIANT == 1) return 0;
  int32_t error = 0;
  seekdb_plugin_custom_row_v1_t row = {sizeof(row), 0, NULL, {0}};
  if (VARIANT >= 21) {
    const int action = VARIANT >= 34 ? VARIANT - 13 : VARIANT;
    if (c->struct_size != (VARIANT >= 34 ? sizeof(seekdb_plugin_custom_context_v4_t) : sizeof(seekdb_plugin_custom_context_v3_t)))
      return SEEKDB_PLUGIN_STATUS_UNSUPPORTED_ABI;
    const seekdb_plugin_custom_context_v3_t *control = (const seekdb_plugin_custom_context_v3_t *)c;
    if (action == 27) {
      if (c->next_input(c->host_context, 0, &row, &error) || c->emit(c->host_context, row.values, row.column_count, &error)) abort();
    }
    if (VARIANT >= 34) {
      const seekdb_plugin_custom_context_v4_t *bound = (const seekdb_plugin_custom_context_v4_t *)c;
      bound->bind_rescan_input(c->host_context, action == 22 ? c->input_count : 0, action == 23 ? NULL : &error);
    } else {
      control->rescan_input(c->host_context, action == 22 ? c->input_count : 0, action == 23 ? NULL : &error);
    }
    if (action != 21) return SEEKDB_PLUGIN_STATUS_END_OF_STREAM; // Deliberately ignore every control failure.
    if (c->next_input(c->host_context, 0, &row, &error)) abort();
    return c->emit(c->host_context, row.values, row.column_count, &error);
  }
  if (VARIANT == 4 || VARIANT == 17) {
    c->next_input(c->host_context, VARIANT == 4 ? c->input_count : 0, &row, VARIANT == 17 ? NULL : &error);
    return SEEKDB_PLUGIN_STATUS_END_OF_STREAM;
  }
  if (VARIANT == 7) { c->check_interrupt(c->host_context, &error); return SEEKDB_PLUGIN_STATUS_END_OF_STREAM; }
  int status = c->next_input(c->host_context, 0, &row, &error);
  if (VARIANT == 5 || VARIANT == 20) return SEEKDB_PLUGIN_STATUS_END_OF_STREAM; // Deliberately swallow a child/schema error.
  if (status) return status;
  if (VARIANT == 19) {
    seekdb_plugin_execution_value_v1_t altered[2] = {row.values[0], row.values[1]};
    altered[0].type_id = "org.example.number";
    c->emit(c->host_context, altered, 2, &error);
    return 0; // Deliberately swallow the host's output-schema rejection.
  }
  status = c->emit(c->host_context, row.values, VARIANT == 16 ? 1 : row.column_count, &error);
  if (VARIANT == 2) { c->emit(c->host_context, row.values, row.column_count, &error); return 0; }
  if (VARIANT == 3) return SEEKDB_PLUGIN_STATUS_END_OF_STREAM;
  if (VARIANT == 6) return 0;
  return status;
}
static seekdb_plugin_status_t rescan(seekdb_plugin_instance_handle_t *p, void *cursor) {
  if (lifecycle(p) || !cursor) abort(); return VARIANT == 10 ? SEEKDB_PLUGIN_STATUS_END_OF_STREAM : 0;
}
static seekdb_plugin_status_t close_cursor(seekdb_plugin_instance_handle_t *p, void *cursor) {
  if (lifecycle(p) || !cursor) abort(); free(cursor); ++closed; return VARIANT == 11 ? SEEKDB_PLUGIN_STATUS_INTERNAL : 0;
}
static const seekdb_plugin_custom_executor_v1_t service = {
  sizeof(service), 1, VARIANT == 13 ? 9 : VARIANT >= 34 ? 2 : VARIANT >= 21 ? 1 : 0, 0, open_cursor, next_cursor, rescan, close_cursor, {0}
};
static const seekdb_plugin_service_provide_descriptor_t provides = {
  sizeof(provides), "test.custom.executor", {1, 0, 0}, &service,
  VARIANT == 18 ? 0 : SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0}
};
SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *seekdb_plugin_entry_v1(void) {
  static const seekdb_plugin_server_dev_manifest_v1_t manifest = {
    {sizeof(manifest), 1, 0, "org.seekdb.sql_extension", "test", {1, 0, 0}, "sql-extension-catalog-v1",
     1, 1, (VARIANT == 12 ? 0 : SEEKDB_PLUGIN_CAPABILITY_SERVER_DEV) |
           (VARIANT == 18 ? SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE : 0),
     &provides, 1, NULL, 0, init, lifecycle, lifecycle, deinit, {0}},
    SEEKDB_PLUGIN_SERVER_DEV_BRIDGE_VERSION, SEEKDB_SERVER_DEV_HOST_BUILD_ID_SIZE,
    SEEKDB_SERVER_DEV_HOST_BUILD_ID_BYTES, {0}
  };
  return &manifest.v1;
}
