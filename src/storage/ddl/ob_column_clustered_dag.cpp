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

#include "storage/ddl/ob_column_clustered_dag.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "storage/ddl/ob_tablet_slice_row_iterator.h"
#include "storage/ddl/ob_cg_macro_block_write_task.h"
#include "rootserver/ddl_task/ob_ddl_task.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/column_store/ob_column_store_replica_util.h"
#include "storage/ddl/ob_ddl_merge_task_v2.h"
#include "storage/ddl/ob_group_write_macro_block_task.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase;
using namespace oceanbase::storage;
using namespace oceanbase::sql;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

ObColumnClusteredDag::ObColumnClusteredDag()
  : px_thread_count_(0), px_finished_count_(0), is_range_count_ready_(false), total_slice_count_(0), use_static_plan_(true)
{

}

ObColumnClusteredDag::~ObColumnClusteredDag()
{

}

int ObColumnClusteredDag::init_by_param(const share::ObIDagInitParam *param)
{
  int ret = OB_SUCCESS;
  ObITask *merge_parent_task = nullptr;
  const ObColumnClusteredDagInitParam *init_param = static_cast<const ObColumnClusteredDagInitParam*>(param);
  if (OB_UNLIKELY(nullptr == init_param || !init_param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(init_param));
  } else if (OB_FAIL(ObDDLIndependentDag::init_by_param(init_param))) {
    LOG_WARN("init ddl independent dag failed", K(ret), KPC(init_param));
  } else {
    px_thread_count_ = init_param->px_thread_count_;
    is_inited_ = true;

    ObArray<ObITask *> write_macro_block_tasks;
    if (is_fts_aux_build()) {
      if (OB_FAIL(generate_partition_local_fixed_tasks(write_macro_block_tasks))) {
        LOG_WARN("fail to generate fts partition local fixed tasks", KR(ret));
      }
    } else if (OB_FAIL(generate_write_macro_block_tasks(write_macro_block_tasks))) {
      LOG_WARN("fail to generate write macro block tasks", KR(ret));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(batch_add_task(write_macro_block_tasks))) {
      LOG_WARN("batch add task failed", K(ret), K(write_macro_block_tasks.count()));
    }
  }
  FLOG_INFO("columnn clustered dag init", K(ret), KPC(this));
  return ret;
}

int ObColumnClusteredDag::set_px_finished()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ATOMIC_INC(&px_finished_count_);
    if (is_scan_finished() && !use_static_plan_) {
      /* do nothing */
    }
  }
  FLOG_INFO("set px finished", K(px_finished_count_), K(px_thread_count_));
  return ret;
}

bool ObColumnClusteredDag::is_fts_aux_build() const
{
  const ObIndexType index_type = ddl_table_schema_.table_item_.index_type_;
  return share::schema::is_fts_index_aux(index_type)
      || share::schema::is_fts_doc_word_aux(index_type);
}

int ObColumnClusteredDag::generate_partition_local_fixed_tasks(
    ObIArray<ObITask *> &tasks,
    ObITask *next_task)
{
  int ret = OB_SUCCESS;
  tasks.reset();
  ObDDLScanTask *scan_task = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObColumnClusteredDag not init", KR(ret), KP(this));
  } else if (is_incremental_direct_load(direct_load_type_)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("fts partition local fixed tasks only support full direct load", KR(ret), K_(direct_load_type));
  } else if (OB_FAIL(alloc_task(scan_task))) {
    LOG_WARN("fail to alloc scan task", KR(ret));
  } else if (OB_FAIL(scan_task->init(this))) {
    LOG_WARN("fail to init scan task", KR(ret));
  } else if (OB_FAIL(tasks.push_back(scan_task))) {
    LOG_WARN("fail to push scan task", KR(ret));
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < ls_tablet_ids_.count(); ++i) {
    const ObTabletID &tablet_id = ls_tablet_ids_.at(i).second;
    ObGroupWriteMacroBlockTask *group_write_task = nullptr;
    ObITask *data_merge_task = nullptr;
    ObITask *lob_merge_task = nullptr;
    if (OB_FAIL(alloc_task(group_write_task))) {
      LOG_WARN("fail to alloc fts group write task", KR(ret), K(tablet_id));
    } else if (OB_FAIL(group_write_task->init(this, tablet_id))) {
      LOG_WARN("fail to init fts group write task", KR(ret), K(tablet_id));
    } else if (OB_FAIL(init_tablet_merge_task(tablet_id, true/*for_major*/, data_merge_task, lob_merge_task))) {
      LOG_WARN("fail to init fts tablet merge task", KR(ret), K(tablet_id));
    } else if (OB_ISNULL(data_merge_task)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null fts data merge task", KR(ret), K(tablet_id));
    } else if (OB_FAIL(tasks.push_back(group_write_task))) {
      LOG_WARN("fail to push fts group write task", KR(ret), K(tablet_id));
    } else if (OB_FAIL(tasks.push_back(data_merge_task))) {
      LOG_WARN("fail to push fts data merge task", KR(ret), K(tablet_id));
    } else if (nullptr != lob_merge_task && OB_FAIL(tasks.push_back(lob_merge_task))) {
      LOG_WARN("fail to push fts lob merge task", KR(ret), K(tablet_id));
    } else if (OB_FAIL(scan_task->add_child(*group_write_task))) {
      LOG_WARN("fail to link scan to fts group write task", KR(ret), K(tablet_id));
    } else if (OB_FAIL(group_write_task->add_child(*data_merge_task))) {
      LOG_WARN("fail to link fts group write to data merge task", KR(ret), K(tablet_id));
    } else if (nullptr != lob_merge_task && OB_FAIL(group_write_task->add_child(*lob_merge_task))) {
      LOG_WARN("fail to link fts group write to lob merge task", KR(ret), K(tablet_id));
    } else if (nullptr != next_task) {
      if (OB_FAIL(data_merge_task->add_child(*next_task))) {
        LOG_WARN("fail to link fts data merge to next task", KR(ret), K(tablet_id));
      } else if (nullptr != lob_merge_task && OB_FAIL(lob_merge_task->add_child(*next_task))) {
        LOG_WARN("fail to link fts lob merge to next task", KR(ret), K(tablet_id));
      }
    }
  }
  LOG_INFO("generate fts partition local fixed tasks", KR(ret), K(tasks.count()), K(ls_tablet_ids_.count()),
           K(ddl_table_schema_.table_item_.index_type_));
  return ret;
}

int ObColumnClusteredDag::update_tablet_range_count()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObMutexGuard mutex_guard(mutex_);
    if (is_range_count_ready_) {
      // do nothing
    } else {
      ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
      bool use_idempotent_mode = false;
      ObArenaAllocator arena(ObMemAttr("ddl_slice_info"));
      rootserver::ObDDLSliceInfo ddl_slice_info;
      if (OB_ISNULL(sql_proxy)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("sql proxy is null", K(ret));
      } else if (OB_FAIL(rootserver::ObDDLTaskRecordOperator::get_schedule_info(
                     *sql_proxy, ddl_task_param_.ddl_task_id_, arena, false/*is_for_update*/, ddl_slice_info, use_idempotent_mode))) {
        LOG_WARN("fail to get schedule info", K(ret), K(ddl_task_param_));
      } else if (!use_idempotent_mode) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl dag always use idempotent mode", K(ret), K(use_idempotent_mode), K(ddl_task_param_));
      } else {
        total_slice_count_ = 0;
        const common::Ob2DArray<sql::ObPxTabletRange> &part_ranges = ddl_slice_info.part_ranges_;
        if (0 == part_ranges.count()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("no partition range", K(ret), K(ddl_slice_info));
        } else if (1 == part_ranges.count() && 0 == part_ranges.at(0).tablet_id_) {
          // for unpartitioned table, there is only one tablet and its tablet id is 0
          if (ls_tablet_ids_.count() != 1) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("tablet count not match", K(ret), K(part_ranges), K(ls_tablet_ids_));
          } else {
            const ObTabletID &tablet_id = ls_tablet_ids_.at(0).second;
            total_slice_count_ = part_ranges.at(0).range_cut_.count() + 1;
            ObDDLTabletContext *tablet_context = nullptr;
            if (OB_FAIL(get_tablet_context(tablet_id, tablet_context))) {
              LOG_WARN("get tablet context failed", K(ret), K(tablet_id));
            } else {
              tablet_context->slice_count_ = total_slice_count_;
              tablet_context->table_slice_offset_ = 0;
            }
          }
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < part_ranges.count(); ++i) {
            const ObPxTabletRange &cur_part_range = part_ranges.at(i);
            const int64_t tablet_slice_count = cur_part_range.range_cut_.count() + 1;
            ObTabletID tablet_id(cur_part_range.tablet_id_);
            ObDDLTabletContext *tablet_context = nullptr;
            if (OB_FAIL(get_tablet_context(tablet_id, tablet_context))) {
              if (OB_HASH_NOT_EXIST != ret) {
                LOG_WARN("get tablet context failed", K(ret), K(tablet_id));
              } else {
                // may get tablet not in this node, skip it, but add total slice count
                total_slice_count_ += tablet_slice_count;
                ret = OB_SUCCESS;
              }
            } else {
              tablet_context->slice_count_ = tablet_slice_count;
              tablet_context->table_slice_offset_ = total_slice_count_;
              total_slice_count_ += tablet_slice_count;
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        is_range_count_ready_ = true;
      }
    }
  }
  return ret;
}
