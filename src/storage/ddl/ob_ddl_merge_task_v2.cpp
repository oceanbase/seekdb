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
#include "storage/ddl/ob_ddl_merge_task.h"
#include "share/rc/ob_module_provider.h"
#include "storage/ddl/ob_ddl_merge_task_utils.h"
#include "storage/ddl/ob_ddl_merge_task_v2.h"
#include "share/ob_ddl_checksum.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/ob_ddl_sim_point.h"
#include "storage/compaction/ob_tenant_tablet_scheduler.h"
#include "storage/ob_storage_schema_util.h"
#include "storage/compaction/ob_schedule_dag_func.h"
#include "storage/ddl/ob_direct_load_struct.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "share/ob_structured_event_logger.h"
#include "storage/ddl/ob_ddl_merge_schedule.h"
using namespace oceanbase::observer;
using namespace oceanbase::share::schema;
using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::blocksstable;

namespace oceanbase
{
namespace storage
{

ObDDLMergePrepareTask::ObDDLMergePrepareTask():
  ObITask(ObITaskType::TASK_TYPE_DDL_MERGE_PREPARE),
  merge_param_(), is_inited_(false)
{}

ObDDLMergePrepareTask::~ObDDLMergePrepareTask()
{}

int ObDDLMergePrepareTask::init(const ObDDLTabletMergeDagParamV2 &merge_param)
{
  int ret = OB_SUCCESS;
  if (!merge_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(merge_param));
  } else {
    merge_param_ = merge_param;
    is_inited_   = true;
  }
  FLOG_INFO("[DDL_MERGE_TASK] success to create merge prepare task", K(ret), K(merge_param_));
  return ret;
}

/*
 * process task dependency for single tablet
 * on contrast process should process relative tablet， such as lob
*/
int ObDDLMergePrepareTask::inner_process()
{
  int ret = OB_SUCCESS;
  ObIDag *dag = get_dag();
  ObArray<ObITask*> merge_slice_tasks;
  ObDDLMergeAssembleTask *assemble_task = nullptr;

  int64_t merge_slice_idx = 0;
  ObArray<ObDDLSliceRange> slice_ranges;
  ObTabletID tablet_id;
  ObWriteTabletParam *tablet_param = nullptr;

  /* debug sync for building major in leader server*/
  if (merge_param_.for_major_) {
    DEBUG_SYNC(BEFORE_TABLET_FULL_DIRECT_LOAD_MGR_CLOSE);
  }

  /* validate tablet context before generating merge tasks */
  if (OB_ISNULL(dag)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag should not be null", K(ret));
  } else if (OB_FAIL(merge_param_.get_tablet_param(tablet_id, tablet_param))) {
    LOG_WARN("failed to get tablet param", K(ret));
  } else if (OB_ISNULL(tablet_param)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet param should not be nullptr", K(ret), K(merge_param_));
  } else if (OB_ISNULL(tablet_param->storage_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("storage schema should not be nullptr", K(ret), KPC(tablet_param));
  }
  
  /* pre-check before merge */
  bool need_merge = true;
  ObIDDLMergeHelper *merge_helper = nullptr;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(merge_param_.get_merge_helper(merge_helper))) {
    LOG_WARN("failed to get merge helper", K(ret));
  } else if (OB_ISNULL(merge_helper)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge param is invalid", K(ret));
  } else if (OB_FAIL(merge_helper->check_need_merge(dag, merge_param_, need_merge))) {
    LOG_WARN("failed to check need merge", KR(ret));
  }

  /* 
   * 1. calculate merge slice task count
   * 2. get_rec_scn for release ddl kvs 
  */
  if (OB_FAIL(ret) || !need_merge) {
  } else if (OB_FAIL(merge_helper->process_prepare_task(dag, merge_param_, slice_ranges))) {
    LOG_WARN("failed to process prepare task", KR(ret), K(merge_param_));
  } else if (OB_FAIL(merge_helper->get_rec_scn(merge_param_))) {
    LOG_WARN("failed to get rec scn", K(ret));
  }

  /* generate assemble table task */
  if (OB_FAIL(ret) || !need_merge) {
  } else if (OB_FAIL(dag->alloc_task(assemble_task))) {
    LOG_WARN("failed alloc assemble task", K(ret));
  } else if (OB_ISNULL(assemble_task)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("assemble task should not be null", K(ret), K(merge_param_));
  } else if (OB_FAIL(assemble_task->init(merge_param_))) {
    LOG_WARN("failed to init assemble", K(ret));
  } else if (OB_FAIL(assemble_task->deep_copy_children(get_child_nodes()))) {
    LOG_WARN("fail to deep copy children", KR(ret));
  } else if (OB_FAIL(::ObITask::add_child(*assemble_task))) {
    LOG_WARN("failed to add assemble task to prepare task", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < slice_ranges.count(); i++ ) {
      ObDDLMergeSliceTask *merge_slice_task = nullptr;
      const ObDDLSliceRange &slice_range = slice_ranges.at(i);
      if (OB_FAIL(dag->alloc_task(merge_slice_task))) {
        LOG_WARN("failed to alloc merge slice task", K(ret));
      } else if (OB_FAIL(merge_slice_task->init(
          merge_param_, slice_range.start_slice_idx_, slice_range.end_slice_idx_))) {
        LOG_WARN("failed to init merge slice task", K(ret));
      } else if (OB_FAIL(merge_slice_task->add_child(*assemble_task))) {
        LOG_WARN("failed add child for merge slice task", K(ret));
      } else if (OB_FAIL(::ObITask::add_child(*merge_slice_task))) {
        LOG_WARN("failed to add child to prepare task", K(ret));
      } else if (OB_FAIL(merge_slice_tasks.push_back(merge_slice_task))) {
        LOG_WARN("failed to push back task", K(ret));
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else {
    // add task in reverse order of running
    /* generate assemble task */
    if (OB_FAIL(ret)) {
    } else if (nullptr == assemble_task) {
    } else if (OB_FAIL(dag->add_task(*assemble_task))) {
      LOG_WARN("failed to add assemble task to dag", K(ret));
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(dag->batch_add_task(merge_slice_tasks))) {
        LOG_WARN("batch add task failed", K(ret));
      }
    }
  }
  return ret;
}

int ObDDLMergePrepareTask::process()
{
  int ret = OB_SUCCESS;
  bool has_lob = false;
  ObTabletID target_tablet_id;
  ObWriteTabletParam *tablet_param = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("task is not inited", K(ret), KPC(this));
  } else if (OB_FAIL(inner_process())) {
    LOG_WARN("failed to inner process prepare task", K(ret));
  } else if (OB_FAIL(merge_param_.get_tablet_param(target_tablet_id, tablet_param))) {
    LOG_WARN("failed to get tablet param", K(ret));
  }

  FLOG_INFO("[DDL_MERGE_TASK] finish merge prepare task", K(ret), K(merge_param_));
  return ret;
}

void ObDDLMergePrepareTask::task_debug_info_to_string(char *buf, const int64_t buf_len, int64_t &pos) const
{
  ObTabletID tablet_id;
  ObWriteTabletParam *tablet_param = nullptr;
  if (OB_SUCCESS == merge_param_.get_tablet_param(tablet_id, tablet_param)) {
    BUF_PRINTF("DDL Merge Prepare Task: tablet_id=%ld, is_inited=%s",
               tablet_id.id(), is_inited_ ? "true" : "false");
  } else {
    BUF_PRINTF("DDL Merge Prepare Task: is_inited=%s", is_inited_ ? "true" : "false");
  }
}

ObDDLMergeSliceTask::ObDDLMergeSliceTask():
ObITask(ObITaskType::TASK_TYPE_DDL_MERGE_SLICE), merge_param_(), start_slice_idx_(-1), end_slice_idx_(-1), is_inited_(false)
{}

int ObDDLMergeSliceTask::init(const ObDDLTabletMergeDagParamV2 &merge_param,
                              const int64_t start_slice_idx,
                              const int64_t end_slice_idx)
{
  int ret = OB_SUCCESS;
  if (!merge_param.is_valid() || start_slice_idx < 0 || end_slice_idx < start_slice_idx) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid merge param", K(ret), K(merge_param), K(start_slice_idx), K(end_slice_idx));
  } else {
    merge_param_     = merge_param;
    start_slice_idx_ = start_slice_idx;
    end_slice_idx_   = end_slice_idx;
    is_inited_       = true;
  }
  FLOG_INFO("[DDL_MERGE_TASK] create ddl slice merge task", K(ret), K(start_slice_idx_), K(end_slice_idx_), K(merge_param_.for_replay_));

  return ret;
}

int ObDDLMergeSliceTask::process()
{
  int ret = OB_SUCCESS;
  ObIDag *dag = get_dag();
  ObArenaAllocator allocator("MergeSlice");
  ObIDDLMergeHelper *merge_helper = nullptr;
  
  if (OB_ISNULL(dag)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag should not be null", K(ret));
  } else if (OB_FAIL(merge_param_.get_merge_helper(merge_helper))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get merge helper", K(ret), K(merge_param_));
  } else if (OB_ISNULL(merge_helper)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge helper should not be null", K(ret), K(merge_param_));
  } else if (OB_FAIL(merge_helper->merge_slice(dag, merge_param_, start_slice_idx_, end_slice_idx_))) {
    LOG_WARN("failed to merge slice", K(ret));
  }

  FLOG_INFO("[DDL_MERGE_TASK] finish merge slice", K(ret), K(start_slice_idx_), K(end_slice_idx_), K(merge_param_));
  return ret;
}

void ObDDLMergeSliceTask::task_debug_info_to_string(char *buf, const int64_t buf_len, int64_t &pos) const
{
  ObTabletID tablet_id;
  ObWriteTabletParam *tablet_param = nullptr;
  if (OB_SUCCESS == merge_param_.get_tablet_param(tablet_id, tablet_param)) {
    BUF_PRINTF("DDL Merge Slice Task: tablet_id=%ld, start_slice=%ld, end_slice=%ld",
               tablet_id.id(), start_slice_idx_, end_slice_idx_);
  } else {
    BUF_PRINTF("DDL Merge Slice Task: start_slice=%ld, end_slice=%ld",
               start_slice_idx_, end_slice_idx_);
  }
}


ObDDLMergeAssembleTask::ObDDLMergeAssembleTask():
  ObITask(ObITaskType::TASK_TYPE_DDL_MERGE_ASSEMBLE), merge_param_(), is_inited_(false)
{}

int ObDDLMergeAssembleTask::init(const ObDDLTabletMergeDagParamV2 &ddl_merge_param)
{
  int ret = OB_SUCCESS;
  if (!ddl_merge_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_merge_param));
  } else {
    merge_param_ = ddl_merge_param;
    is_inited_ = true;
  }
  FLOG_INFO("[DDL_MERGE_TASK] create ddl slice merge task,", K(ret), K(ddl_merge_param));
  return ret;
}


int ObDDLMergeAssembleTask::process()
{
  int ret = OB_SUCCESS;
  ObIDDLMergeHelper *merge_helper = nullptr;
  ObArenaAllocator allocator(ObMemAttr("Ddl_Assm_Task"));
  ObTabletID target_tablet_id;
  ObWriteTabletParam *tablet_param = nullptr;
  ObTabletHandle tablet_handle;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("assemble task has not been init", K(ret), KPC(this));
  } else if (OB_FAIL(merge_param_.get_tablet_param(target_tablet_id, tablet_param))) {
    LOG_WARN("failed to get tablet param", K(ret));
  } else if (OB_FAIL(merge_param_.get_merge_helper(merge_helper))) {
    LOG_WARN("failed to get merge helper", K(ret), K(merge_param_));
  } else if (OB_ISNULL(merge_helper)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge helper should not be null", K(ret), K(merge_param_));
  }

  if (FAILEDx(merge_helper->assemble_sstable(merge_param_))) {
    LOG_WARN("failed to assemble major sstable", K(ret));
  }
  FLOG_INFO("[DDL_MERGE_TASK]  ddl update table store finish", K(ret), K(merge_param_));
  return ret;
}

void ObDDLMergeAssembleTask::task_debug_info_to_string(char *buf, const int64_t buf_len, int64_t &pos) const
{
  ObTabletID tablet_id;
  ObWriteTabletParam *tablet_param = nullptr;
  if (OB_SUCCESS == merge_param_.get_tablet_param(tablet_id, tablet_param)) {
    BUF_PRINTF("DDL Merge Assemble Task: tablet_id=%ld, is_inited=%s",
               tablet_id.id(), is_inited_ ? "true" : "false");
  } else {
    BUF_PRINTF("DDL Merge Assemble Task: is_inited=%s", is_inited_ ? "true" : "false");
  }
}

} //namespcae storage
} //namespace oceanbase
