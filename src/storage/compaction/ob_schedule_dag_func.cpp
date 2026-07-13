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

#define USING_LOG_PREFIX STORAGE_COMPACTION
#include "ob_schedule_dag_func.h"
#include "share/rc/ob_module_provider.h"
#include "storage/multi_data_source/ob_mds_table_merge_dag.h"
#include "storage/multi_data_source/ob_mds_table_merge_dag_param.h"
#include "storage/ddl/ob_tablet_lob_split_task.h"
#include "storage/ddl/ob_tablet_fork_task.h"
#include "storage/compaction/ob_batch_freeze_tablets_dag.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace compaction
{

#define CREATE_DAG(T)                                                          \
  if (OB_FAIL(share::g_mp->tenant_dag_scheduler()                                      \
                  ->create_and_add_dag<T>(&param, is_emergency))) {            \
    if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {                         \
      LOG_WARN("failed to create merge dag", K(ret), K(param));                \
    } else if (OB_EAGAIN == ret) {                                             \
      LOG_DEBUG("exists same dag, wait the dag to finish", K(ret), K(param));  \
    }                                                                          \
  } else {                                                                     \
    LOG_DEBUG("success to schedule tablet merge dag", K(ret), K(param));       \
  }

#define CREATE_AND_GET_DAG(T, dag) \
  { \
    if (OB_FAIL(share::g_mp->tenant_dag_scheduler()->create_dag<T>(&param, dag))) { \
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) { \
        LOG_WARN("failed to create merge dag", K(ret), K(param)); \
      } \
    } else { \
      LOG_DEBUG("success to create and get dag", K(ret), K(param)); \
    } \
  }
int ObScheduleDagFunc::schedule_tx_table_merge_dag(
    ObTabletMergeDagParam &param,
    const bool is_emergency)
{
  int ret = OB_SUCCESS;
  CREATE_DAG(ObTxTableMergeDag);
  return ret;
}

int ObScheduleDagFunc::schedule_tablet_merge_dag(
    ObTabletMergeDagParam &param,
    const bool is_emergency)
{
  int ret = OB_SUCCESS;
  if (is_major_merge_type(param.merge_type_)) {
    CREATE_DAG(ObTabletMajorMergeDag);
  } else if (MINI_MERGE == param.merge_type_) {
    CREATE_DAG(ObTabletMiniMergeDag);
  } else {
    ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

int ObScheduleDagFunc::schedule_ddl_table_merge_dag(
    ObDDLTableMergeDagParam &param,
    const bool is_emergency)
{
  int ret = OB_SUCCESS;
  CREATE_DAG(ObDDLTableMergeDag);
  return ret;
}

int ObScheduleDagFunc::schedule_tablet_split_dag(
    ObTabletSplitParam &param,
    const bool is_emergency)
{
  int ret = OB_SUCCESS;
  CREATE_DAG(ObTabletSplitDag);
  return ret;
}
int ObScheduleDagFunc::schedule_and_get_tablet_split_dag(
    storage::ObTabletSplitParam &param,
    storage::ObTabletSplitDag *&dag,
    const bool is_emergency)
{
  int ret = OB_SUCCESS;
  CREATE_AND_GET_DAG(ObTabletSplitDag, dag);
  return ret;
}

int ObScheduleDagFunc::schedule_lob_tablet_split_dag(
    ObLobSplitParam &param,
    const bool is_emergency)
{
  int ret = OB_SUCCESS;
  CREATE_DAG(ObTabletLobSplitDag);
  return ret;
}

int ObScheduleDagFunc::schedule_and_get_lob_tablet_split_dag(
    storage::ObLobSplitParam &param,
    storage::ObTabletLobSplitDag *&dag,
    const bool is_emergency)
{
  int ret = OB_SUCCESS;
  CREATE_AND_GET_DAG(ObTabletLobSplitDag, dag);
  return ret;
}

int ObScheduleDagFunc::schedule_tablet_fork_dag(
    storage::ObTabletForkParam &param,
    const bool is_emergency)
{
  int ret = OB_SUCCESS;
  CREATE_DAG(ObTabletForkDag);
  return ret;
}

int ObScheduleDagFunc::schedule_mds_table_merge_dag(
    storage::mds::ObMdsTableMergeDagParam &param,
    const bool is_emergency)
{
  int ret = OB_SUCCESS;
  CREATE_DAG(storage::mds::ObMdsTableMergeDag);
  return ret;
}

int ObScheduleDagFunc::schedule_batch_freeze_dag(
    const ObBatchFreezeTabletsParam &param)
{
  int ret = OB_SUCCESS;
  bool is_emergency = true;
  if (param.tablet_info_array_.empty()) {
    // do nothing
  } else {
    CREATE_DAG(ObBatchFreezeTabletsDag);
  }
  return ret;
}

int ObDagParamFunc::fill_param(
    const storage::ObTablet &tablet,
    const ObMergeType merge_type,
    const int64_t &merge_snapshot_version,
    const ObExecMode exec_mode,
    ObTabletMergeDagParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_merge_type(merge_type)
    || merge_snapshot_version < ObVersion::MIN_VERSION
    || !is_valid_exec_mode(exec_mode))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(merge_snapshot_version), K(exec_mode));
  } else {
    param.tablet_id_ = tablet.get_tablet_meta().tablet_id_;
    param.merge_type_ = merge_type;
    param.merge_version_ = merge_snapshot_version;
    param.exec_mode_ = exec_mode;
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
