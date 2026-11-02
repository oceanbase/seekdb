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

#include "storage/ddl/ob_ddl_inc_task.h"
#include "storage/ddl/ob_ddl_inc_redo_log_writer.h"
#include "storage/ddl/ob_ddl_independent_dag.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "storage/ob_storage_schema_util.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"
#include "storage/compaction/ob_schedule_dag_func.h"

namespace oceanbase
{
using namespace share;
using namespace common;
using namespace transaction;
using namespace observer;
using namespace lib;
namespace storage
{
using namespace share;

/**
 * ObDDLIncStartTask
 */

ObDDLIncStartTask::ObDDLIncStartTask(const int64_t tablet_idx)
  : ObITask(TASK_TYPE_DDL_BUILD_MAJOR_SSTABLE), tablet_idx_(tablet_idx)
{
}

int ObDDLIncStartTask::generate_next_task(ObITask *&next_task)
{
  int ret = OB_SUCCESS;
  next_task = nullptr;
  ObDDLIndependentDag *dag = static_cast<ObDDLIndependentDag *>(dag_);
  const int64_t next_tablet_idx = tablet_idx_ + 1;
  if (next_tablet_idx >= dag->get_ls_tablet_ids().count()) {
    ret = OB_ITER_END;
  } else {
    ObDDLIncStartTask *start_task = nullptr;
    if (OB_FAIL(dag->alloc_task(start_task, next_tablet_idx))) {
      LOG_WARN("fail to alloc task", KR(ret));
    } else {
      next_task = start_task;
    }
  }
  return ret;
}

int ObDDLIncStartTask::process()
{
  int ret = OB_SUCCESS;
  ObDDLIndependentDag *dag = static_cast<ObDDLIndependentDag *>(dag_);
  const std::pair<share::ObLSID, ObTabletID> &ls_tablet_id =
    dag->get_ls_tablet_ids().at(tablet_idx_);
  ObDDLTabletContext *tablet_ctx = nullptr;
  ObDDLIncRedoLogWriter redo_writer;
  SCN start_scn;
  if (OB_FAIL(dag->get_tablet_context(ls_tablet_id.second, tablet_ctx))) {
    LOG_WARN("fail to get tablet context", KR(ret), K(ls_tablet_id.second));
  } else if (OB_FAIL(redo_writer.init(ls_tablet_id.first,
                                      ls_tablet_id.second))) {
    LOG_WARN("fail to init inc redo writer", KR(ret), K(ls_tablet_id.first), K(ls_tablet_id.second),
        K(dag->get_direct_load_type()), K(dag->get_tx_info().trans_id_), K(dag->get_tx_info().seq_no_), K(dag->is_inc_major_log()));
  } else if (OB_FAIL(redo_writer.write_inc_start_log_with_retry(tablet_ctx->lob_meta_tablet_id_,
                                                                dag->get_tx_info().tx_desc_,
                                                                start_scn))) {
    LOG_WARN("fail to write inc start log", KR(ret), KPC(tablet_ctx), K(dag->get_tx_info()));
  }
  return ret;
}


/**
 * ObDDLIncCommitTask
 */

ObDDLIncCommitTask::ObDDLIncCommitTask(const int64_t tablet_idx)
  : ObITask(TASK_TYPE_DDL_BUILD_MAJOR_SSTABLE), tablet_idx_(tablet_idx)
{
}

ObDDLIncCommitTask::ObDDLIncCommitTask(const ObTabletID &tablet_id)
  : ObITask(TASK_TYPE_DDL_BUILD_MAJOR_SSTABLE), tablet_idx_(-1), tablet_id_(tablet_id)
{
}

int ObDDLIncCommitTask::generate_next_task(ObITask *&next_task)
{
  int ret = OB_SUCCESS;
  next_task = nullptr;
  ObDDLIndependentDag *dag = static_cast<ObDDLIndependentDag *>(dag_);
  const int64_t next_tablet_idx = tablet_idx_ + 1;
  if (tablet_idx_ == -1) {
    ret = OB_ITER_END;
  } else if (next_tablet_idx >= dag->get_ls_tablet_ids().count()) {
    ret = OB_ITER_END;
  } else {
    ObDDLIncCommitTask *commit_task = nullptr;
    if (OB_FAIL(dag->alloc_task(commit_task, next_tablet_idx))) {
      LOG_WARN("fail to alloc task", KR(ret));
    } else {
      next_task = commit_task;
    }
  }
  return ret;
}

int ObDDLIncCommitTask::process()
{
  int ret = OB_SUCCESS;
  ObDDLIndependentDag *dag = static_cast<ObDDLIndependentDag *>(dag_);
  if (tablet_idx_ != -1) {
    const std::pair<share::ObLSID, ObTabletID> &ls_tablet_id =
      dag->get_ls_tablet_ids().at(tablet_idx_);
    tablet_id_ = ls_tablet_id.second;
  }
  ObDDLTabletContext *tablet_ctx = nullptr;
  ObDDLIncRedoLogWriter redo_writer;
  if (OB_FAIL(dag->get_tablet_context(tablet_id_, tablet_ctx))) {
    LOG_WARN("fail to get tablet context", KR(ret), K(tablet_id_));
  } else if (OB_FAIL(redo_writer.init(tablet_ctx->ls_id_,
                                      tablet_id_))) {
    LOG_WARN("fail to init inc redo writer", KR(ret), K(tablet_ctx->ls_id_), K(tablet_id_),
        K(dag->get_direct_load_type()), K(dag->get_tx_info().trans_id_), K(dag->get_tx_info().seq_no_), K(dag->is_inc_major_log()));
  } else if (OB_FAIL(redo_writer.write_inc_commit_log_with_retry(true /*allow_remote_write*/,
                                                                 tablet_ctx->lob_meta_tablet_id_,
                                                                 dag->get_tx_info().tx_desc_))) {
    LOG_WARN("fail to write inc commit log", KR(ret), KPC(tablet_ctx), K(dag->get_tx_info()));
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
