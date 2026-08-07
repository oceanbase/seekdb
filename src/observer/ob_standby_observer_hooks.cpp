/*
 * Copyright (c) 2025 OceanBase.
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

#define USING_LOG_PREFIX SERVER
#include "observer/ob_standby_observer_hooks.h"
#include "lib/oblog/ob_log.h"
#include "share/ob_structured_event_logger.h"
#include "standby/ob_standby_observer_adapter.h"

namespace oceanbase
{
namespace observer
{
namespace
{

void report_physical_copy_task(
    const uint64_t tenant_id,
    const int64_t ls_id,
    const uint64_t tablet_id,
    const storage::ObITable::TableKey &table_key,
    const int64_t macro_block_count)
{
  SERVER_EVENT_ADD("standby_restore", "physical_copy_task",
      "tenant_id", tenant_id,
      "ls_id", ls_id,
      "tablet_id", tablet_id,
      "table_key", table_key,
      "macro_block_count", macro_block_count);
}

void report_set_first_result(
    const uint64_t tenant_id,
    const int32_t result,
    const bool allow_retry,
    const int32_t retry_count,
    const char *failed_task_id,
    const char *dag_type)
{
  SERVER_EVENT_ADD("standby_restore", "set_first_result",
      "tenant_id", tenant_id,
      "result", result,
      "allow_retry", allow_retry,
      "retry_count", retry_count,
      "failed_task_id", failed_task_id,
      "dag_type", dag_type);
}

void report_schema_change_need_merge_tablet_meta(
    const uint64_t tenant_id,
    const uint64_t tablet_id,
    const int64_t old_schema_version,
    const int64_t new_schema_version)
{
  SERVER_EVENT_ADD("standby_restore", "schema_change_need_merge_tablet_meta",
      "tenant_id", tenant_id,
      "tablet_id", tablet_id,
      "old_schema_version", old_schema_version,
      "new_schema_version", new_schema_version);
}

void report_update_major_tablet_table_store(
    const uint64_t tablet_id,
    const int64_t old_multi_version_start,
    const int64_t new_multi_version_start,
    const int64_t old_snapshot_version,
    const int64_t new_snapshot_version,
    const bool has_truncate_info)
{
  SERVER_EVENT_ADD("standby_restore", "update_major_tablet_table_store",
      "tablet_id", tablet_id,
      "old_multi_version_start", old_multi_version_start,
      "new_multi_version_start", new_multi_version_start,
      "old_snapshot_version", old_snapshot_version,
      "new_snapshot_version", new_snapshot_version,
      "has_truncate_info", has_truncate_info);
}

void report_tablet_copy_finish_task(
    const uint64_t tenant_id,
    const int64_t ls_id,
    const uint64_t tablet_id,
    const int32_t result_code,
    const bool dag_failed,
    const int64_t sstable_count,
    const char *extra_info)
{
  SERVER_EVENT_ADD("standby_restore", "tablet_copy_finish_task",
      "tenant_id", tenant_id,
      "ls_id", ls_id,
      "tablet_id", tablet_id,
      "ret", result_code,
      "result", dag_failed,
      "sstable_count", sstable_count,
      extra_info);
}

} // namespace

int register_standby_observer_hooks(
    ObStandbyObserverHook stop_server,
    ObStandbyObserverHook reset_max_id_cache)
{
  standby::ObStandbyObserverHooks hooks;
  hooks.stop_server_ = stop_server;
  hooks.report_physical_copy_task_ = report_physical_copy_task;
  hooks.report_set_first_result_ = report_set_first_result;
  hooks.report_schema_change_need_merge_tablet_meta_ = report_schema_change_need_merge_tablet_meta;
  hooks.report_update_major_tablet_table_store_ = report_update_major_tablet_table_store;
  hooks.report_tablet_copy_finish_task_ = report_tablet_copy_finish_task;
  hooks.reset_max_id_cache_ = reset_max_id_cache;
  return standby::ObStandbyObserverAdapter::set_hooks(hooks);
}

} // namespace observer
} // namespace oceanbase
