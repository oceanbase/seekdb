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

#ifndef OCEANBASE_STORAGE_COMPACTION_OB_SCHEDULE_DAG_FUNC_H_
#define OCEANBASE_STORAGE_COMPACTION_OB_SCHEDULE_DAG_FUNC_H_
#include "lib/container/ob_iarray.h"
#include "storage/compaction/ob_compaction_util.h"
#include "data_plane/scheduler/ob_dag_scheduler.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{
class ObMdsTableMergeDagParam;
}
struct ObDDLTableMergeDagParam;
struct ObTabletForkParam;
class ObComplementDataDag;
class ObTablet;
}

namespace share
{
class ObDagScheduler;
}
namespace compaction
{
struct ObTabletMergeDagParam;
struct ObTabletSchedulePair;
struct ObBatchFreezeTabletsParam;

class ObScheduleDagFunc final
{
public:
  static int schedule_tablet_merge_dag(
      ObTabletMergeDagParam &param,
      const bool is_emergency = false);
  static int schedule_tx_table_merge_dag(
      ObTabletMergeDagParam &param,
      const bool is_emergency = false);
  static int schedule_ddl_table_merge_dag(
      storage::ObDDLTableMergeDagParam &param,
      const bool is_emergency = false);
  static int schedule_tablet_fork_dag(
      storage::ObTabletForkParam &param,
      const bool is_emergency = false);
  static int schedule_mds_table_merge_dag(
      storage::mds::ObMdsTableMergeDagParam &param,
      const bool is_emergency = false);
  static int schedule_batch_freeze_dag(
    const ObBatchFreezeTabletsParam &freeze_param);
};

class ObDagParamFunc final
{
public:
  static int fill_param(
    const storage::ObTablet &tablet,
    const ObMergeType merge_type,
    const int64_t &merge_snapshot_version,
    ObTabletMergeDagParam &param);
};

}
} /* namespace oceanbase */

#endif /* OCEANBASE_STORAGE_OB_SCHEDULE_DAG_FUNC_H_ */
