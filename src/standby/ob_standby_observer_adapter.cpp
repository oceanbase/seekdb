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
#include "standby/ob_standby_observer_adapter.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase
{
namespace standby
{
namespace
{

ObStandbyObserverHooks &standby_observer_hooks()
{
  static ObStandbyObserverHooks hooks;
  return hooks;
}

} // namespace

int ObStandbyObserverAdapter::set_hooks(const ObStandbyObserverHooks &hooks)
{
  int ret = common::OB_SUCCESS;
  if (!hooks.is_valid()) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("invalid standby observer hooks", KR(ret));
  } else {
    standby_observer_hooks() = hooks;
  }
  return ret;
}

void ObStandbyObserverAdapter::stop_server()
{
  const ObStandbyObserverHooks &hooks = standby_observer_hooks();
  if (OB_ISNULL(hooks.stop_server_)) {
    int ret = common::OB_NOT_INIT;
    LOG_WARN("standby observer hooks are not registered", KR(ret));
  } else {
    hooks.stop_server_();
  }
}

void ObStandbyObserverAdapter::report_physical_copy_task(
    const uint64_t tenant_id,
    const int64_t ls_id,
    const uint64_t tablet_id,
    const storage::ObITable::TableKey &table_key,
    const int64_t macro_block_count)
{
  const ObStandbyObserverHooks &hooks = standby_observer_hooks();
  if (OB_NOT_NULL(hooks.report_physical_copy_task_)) {
    hooks.report_physical_copy_task_(tenant_id, ls_id, tablet_id, table_key, macro_block_count);
  }
}

void ObStandbyObserverAdapter::report_set_first_result(
    const uint64_t tenant_id,
    const int32_t result,
    const bool allow_retry,
    const int32_t retry_count,
    const char *failed_task_id,
    const char *dag_type)
{
  const ObStandbyObserverHooks &hooks = standby_observer_hooks();
  if (OB_NOT_NULL(hooks.report_set_first_result_)) {
    hooks.report_set_first_result_(tenant_id, result, allow_retry, retry_count, failed_task_id, dag_type);
  }
}

void ObStandbyObserverAdapter::report_schema_change_need_merge_tablet_meta(
    const uint64_t tenant_id,
    const uint64_t tablet_id,
    const int64_t old_schema_version,
    const int64_t new_schema_version)
{
  const ObStandbyObserverHooks &hooks = standby_observer_hooks();
  if (OB_NOT_NULL(hooks.report_schema_change_need_merge_tablet_meta_)) {
    hooks.report_schema_change_need_merge_tablet_meta_(
        tenant_id, tablet_id, old_schema_version, new_schema_version);
  }
}

void ObStandbyObserverAdapter::report_update_major_tablet_table_store(
    const uint64_t tablet_id,
    const int64_t old_multi_version_start,
    const int64_t new_multi_version_start,
    const int64_t old_snapshot_version,
    const int64_t new_snapshot_version,
    const bool has_truncate_info)
{
  const ObStandbyObserverHooks &hooks = standby_observer_hooks();
  if (OB_NOT_NULL(hooks.report_update_major_tablet_table_store_)) {
    hooks.report_update_major_tablet_table_store_(tablet_id,
        old_multi_version_start,
        new_multi_version_start,
        old_snapshot_version,
        new_snapshot_version,
        has_truncate_info);
  }
}

void ObStandbyObserverAdapter::report_tablet_copy_finish_task(
    const uint64_t tenant_id,
    const int64_t ls_id,
    const uint64_t tablet_id,
    const int32_t result_code,
    const bool dag_failed,
    const int64_t sstable_count,
    const char *extra_info)
{
  const ObStandbyObserverHooks &hooks = standby_observer_hooks();
  if (OB_NOT_NULL(hooks.report_tablet_copy_finish_task_)) {
    hooks.report_tablet_copy_finish_task_(
        tenant_id, ls_id, tablet_id, result_code, dag_failed, sstable_count, extra_info);
  }
}

void ObStandbyObserverAdapter::reset_max_id_cache()
{
  const ObStandbyObserverHooks &hooks = standby_observer_hooks();
  if (OB_NOT_NULL(hooks.reset_max_id_cache_)) {
    hooks.reset_max_id_cache_();
  } else {
    LOG_ERROR_RET(OB_NOT_INIT, "standby observer reset max id cache hook is not registered");
  }
}

} // namespace standby
} // namespace oceanbase
