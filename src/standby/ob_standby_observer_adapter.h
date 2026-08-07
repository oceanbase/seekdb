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

#ifndef OCEANBASE_STANDBY_OB_STANDBY_OBSERVER_ADAPTER_H_
#define OCEANBASE_STANDBY_OB_STANDBY_OBSERVER_ADAPTER_H_

#include "storage/ob_i_table.h"

namespace oceanbase
{
namespace common
{
class ObTabletID;
}
namespace share
{
class ObLSID;
}
namespace standby
{

struct ObStandbyObserverHooks
{
  typedef void (*StopServerFunc)();
  typedef void (*ReportPhysicalCopyTaskFunc)(
      const uint64_t tenant_id,
      const int64_t ls_id,
      const uint64_t tablet_id,
      const storage::ObITable::TableKey &table_key,
      const int64_t macro_block_count);
  typedef void (*ReportSetFirstResultFunc)(
      const uint64_t tenant_id,
      const int32_t result,
      const bool allow_retry,
      const int32_t retry_count,
      const char *failed_task_id,
      const char *dag_type);
  typedef void (*ReportSchemaChangeNeedMergeTabletMetaFunc)(
      const uint64_t tenant_id,
      const uint64_t tablet_id,
      const int64_t old_schema_version,
      const int64_t new_schema_version);
  typedef void (*ReportUpdateMajorTabletTableStoreFunc)(
      const uint64_t tablet_id,
      const int64_t old_multi_version_start,
      const int64_t new_multi_version_start,
      const int64_t old_snapshot_version,
      const int64_t new_snapshot_version,
      const bool has_truncate_info);
  typedef void (*ReportTabletCopyFinishTaskFunc)(
      const uint64_t tenant_id,
      const int64_t ls_id,
      const uint64_t tablet_id,
      const int32_t result_code,
      const bool dag_failed,
      const int64_t sstable_count,
      const char *extra_info);
  typedef void (*ResetMaxIdCacheFunc)();

  ObStandbyObserverHooks()
      : stop_server_(nullptr),
        report_physical_copy_task_(nullptr),
        report_set_first_result_(nullptr),
        report_schema_change_need_merge_tablet_meta_(nullptr),
        report_update_major_tablet_table_store_(nullptr),
        report_tablet_copy_finish_task_(nullptr),
        reset_max_id_cache_(nullptr)
  {}

  bool is_valid() const
  {
    return nullptr != stop_server_
        && nullptr != report_physical_copy_task_
        && nullptr != report_set_first_result_
        && nullptr != report_schema_change_need_merge_tablet_meta_
        && nullptr != report_update_major_tablet_table_store_
        && nullptr != report_tablet_copy_finish_task_
        && nullptr != reset_max_id_cache_;
  }

  StopServerFunc stop_server_;
  ReportPhysicalCopyTaskFunc report_physical_copy_task_;
  ReportSetFirstResultFunc report_set_first_result_;
  ReportSchemaChangeNeedMergeTabletMetaFunc report_schema_change_need_merge_tablet_meta_;
  ReportUpdateMajorTabletTableStoreFunc report_update_major_tablet_table_store_;
  ReportTabletCopyFinishTaskFunc report_tablet_copy_finish_task_;
  ResetMaxIdCacheFunc reset_max_id_cache_;
};

class ObStandbyObserverAdapter final
{
public:
  static int set_hooks(const ObStandbyObserverHooks &hooks);
  static void stop_server();
  static void report_physical_copy_task(
      const uint64_t tenant_id,
      const int64_t ls_id,
      const uint64_t tablet_id,
      const storage::ObITable::TableKey &table_key,
      const int64_t macro_block_count);
  static void report_set_first_result(
      const uint64_t tenant_id,
      const int32_t result,
      const bool allow_retry,
      const int32_t retry_count,
      const char *failed_task_id,
      const char *dag_type);
  static void report_schema_change_need_merge_tablet_meta(
      const uint64_t tenant_id,
      const uint64_t tablet_id,
      const int64_t old_schema_version,
      const int64_t new_schema_version);
  static void report_update_major_tablet_table_store(
      const uint64_t tablet_id,
      const int64_t old_multi_version_start,
      const int64_t new_multi_version_start,
      const int64_t old_snapshot_version,
      const int64_t new_snapshot_version,
      const bool has_truncate_info);
  static void report_tablet_copy_finish_task(
      const uint64_t tenant_id,
      const int64_t ls_id,
      const uint64_t tablet_id,
      const int32_t result_code,
      const bool dag_failed,
      const int64_t sstable_count,
      const char *extra_info);
  static void reset_max_id_cache();
};

} // namespace standby
} // namespace oceanbase

#endif /* OCEANBASE_STANDBY_OB_STANDBY_OBSERVER_ADAPTER_H_ */
