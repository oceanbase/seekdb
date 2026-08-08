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

class IStandbyObserver
{
public:
  virtual ~IStandbyObserver() = default;
  virtual void report_physical_copy_task(
      const uint64_t tenant_id,
      const int64_t ls_id,
      const uint64_t tablet_id,
      const storage::ObITable::TableKey &table_key,
      const int64_t macro_block_count) = 0;
  virtual void report_schema_change_need_merge_tablet_meta(
      const uint64_t tenant_id,
      const uint64_t tablet_id,
      const int64_t old_schema_version,
      const int64_t new_schema_version) = 0;
  virtual void report_update_major_tablet_table_store(
      const uint64_t tablet_id,
      const int64_t old_multi_version_start,
      const int64_t new_multi_version_start,
      const int64_t old_snapshot_version,
      const int64_t new_snapshot_version,
      const bool has_truncate_info) = 0;
  virtual void report_tablet_copy_finish_task(
      const uint64_t tenant_id,
      const int64_t ls_id,
      const uint64_t tablet_id,
      const int32_t result_code,
      const int64_t sstable_count,
      const char *extra_info) = 0;
  virtual void reset_max_id_cache() = 0;
};

class ObStandbyObserverAdapter final
{
public:
  static int set_observer(IStandbyObserver &observer);
  static void report_physical_copy_task(
      const uint64_t tenant_id,
      const int64_t ls_id,
      const uint64_t tablet_id,
      const storage::ObITable::TableKey &table_key,
      const int64_t macro_block_count);
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
      const int64_t sstable_count,
      const char *extra_info);
  static void reset_max_id_cache();
};

} // namespace standby
} // namespace oceanbase

#endif /* OCEANBASE_STANDBY_OB_STANDBY_OBSERVER_ADAPTER_H_ */
