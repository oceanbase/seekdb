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
#include "lib/ob_abort.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase
{
namespace standby
{
namespace
{

IStandbyObserver *&registered_observer()
{
  static IStandbyObserver *observer = nullptr;
  return observer;
}

IStandbyObserver &standby_observer()
{
  IStandbyObserver *observer = registered_observer();
  if (OB_ISNULL(observer)) {
    LOG_ERROR_RET(common::OB_NOT_INIT, "standby observer is not registered");
    ob_abort();
  }
  return *observer;
}

} // namespace

int ObStandbyObserverAdapter::set_observer(IStandbyObserver &observer)
{
  int ret = common::OB_SUCCESS;
  if (OB_NOT_NULL(registered_observer()) && registered_observer() != &observer) {
    ret = common::OB_INIT_TWICE;
    LOG_WARN("standby observer is already registered", KR(ret));
  } else {
    registered_observer() = &observer;
  }
  return ret;
}

void ObStandbyObserverAdapter::report_physical_copy_task(
    const uint64_t tenant_id,
    const int64_t ls_id,
    const uint64_t tablet_id,
    const storage::ObITable::TableKey &table_key,
    const int64_t macro_block_count)
{
  standby_observer().report_physical_copy_task(
      tenant_id, ls_id, tablet_id, table_key, macro_block_count);
}

void ObStandbyObserverAdapter::report_schema_change_need_merge_tablet_meta(
    const uint64_t tenant_id,
    const uint64_t tablet_id,
    const int64_t old_schema_version,
    const int64_t new_schema_version)
{
  standby_observer().report_schema_change_need_merge_tablet_meta(
      tenant_id, tablet_id, old_schema_version, new_schema_version);
}

void ObStandbyObserverAdapter::report_update_major_tablet_table_store(
    const uint64_t tablet_id,
    const int64_t old_multi_version_start,
    const int64_t new_multi_version_start,
    const int64_t old_snapshot_version,
    const int64_t new_snapshot_version,
    const bool has_truncate_info)
{
  standby_observer().report_update_major_tablet_table_store(tablet_id,
      old_multi_version_start,
      new_multi_version_start,
      old_snapshot_version,
      new_snapshot_version,
      has_truncate_info);
}

void ObStandbyObserverAdapter::report_tablet_copy_finish_task(
    const uint64_t tenant_id,
    const int64_t ls_id,
    const uint64_t tablet_id,
    const int32_t result_code,
    const int64_t sstable_count,
    const char *extra_info)
{
  standby_observer().report_tablet_copy_finish_task(
      tenant_id, ls_id, tablet_id, result_code, sstable_count, extra_info);
}

void ObStandbyObserverAdapter::reset_max_id_cache()
{
  standby_observer().reset_max_id_cache();
}

} // namespace standby
} // namespace oceanbase
