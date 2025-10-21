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

#ifndef OCEANBASE_STORAGE_OB_MDS_TABLE_MERGE_TASK
#define OCEANBASE_STORAGE_OB_MDS_TABLE_MERGE_TASK

#include "share/scheduler/ob_tenant_dag_scheduler.h"

namespace oceanbase
{
namespace compaction
{
class ObTabletMergeCtx;
}

namespace storage
{
class ObTabletHandle;
class ObTablet;
class ObTableHandleV2;

namespace mds
{
class ObMdsTableMergeDag;

class ObMdsTableMergeTask : public share::ObITask
{
public:
  ObMdsTableMergeTask();
  virtual ~ObMdsTableMergeTask() = default;
  ObMdsTableMergeTask(const ObMdsTableMergeTask&) = delete;
  ObMdsTableMergeTask &operator=(const ObMdsTableMergeTask&) = delete;
public:
  virtual int process() override;

  int init();
private:
  void try_schedule_compaction_after_mds_mini(compaction::ObTabletMergeCtx &ctx, ObTabletHandle &tablet_handle);
  int check_tablet_status_for_empty_mds_table_(const ObTablet& tablet) const;
  static int build_mds_sstable(
      compaction::ObTabletMergeCtx &ctx,
      const int64_t mds_construct_sequence,
      ObTableHandleV2 &table_handle);
private:
  bool is_inited_;
  ObMdsTableMergeDag *mds_merge_dag_;
};
} // namespace mds
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_MDS_TABLE_MERGE_TASK
