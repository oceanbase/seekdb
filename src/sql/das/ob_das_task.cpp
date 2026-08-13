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

#define USING_LOG_PREFIX SQL_DAS
#include "ob_das_task.h"

namespace oceanbase
{
namespace common
{
namespace serialization
{
template <>
struct EnumEncoder<false, const sql::ObDASBaseCtDef*> : sql::DASCtRefEncoder<sql::ObDASBaseCtDef>
{
};

template <>
struct EnumEncoder<false, sql::ObDASBaseRtDef*> : sql::DASRtRefEncoder<sql::ObDASBaseRtDef>
{
};
} // end namespace serialization
} // end namespace common

using namespace common;
using namespace transaction;
namespace sql
{
int ObIDASTaskOp::start_das_task()
{
  int &ret = errcode_;
  int simulate_error = EVENT_CALL(EventTable::EN_DAS_SIMULATE_OPEN_ERROR);
  int need_dump = EVENT_CALL(EventTable::EN_DAS_SIMULATE_DUMP_WRITE_BUFFER);
  das_task_start_timestamp_ = common::ObTimeUtility::current_time();
  if (OB_UNLIKELY(!is_in_retry() && OB_SUCCESS != simulate_error)) {
    ret = simulate_error;
  } else {
    task_started_ = true;
    if (OB_FAIL(open_op())) {
      LOG_WARN("open das task op failed", K(ret));
      if (OB_ERR_DEFENSIVE_CHECK == ret) {
        //dump das task data to help analysis defensive bug
        dump_data();
      }
    } else if (OB_SUCCESS != need_dump) {
      dump_data();
    }
  }
  if (OB_FAIL(ret)) {
    set_task_status(ObDasTaskStatus::FAILED);
  } else {
    set_task_status(ObDasTaskStatus::FINISHED);
  }
  return ret;
}

void ObIDASTaskOp::set_task_status(ObDasTaskStatus status)
{
  task_status_ = status;
};

int ObIDASTaskOp::end_das_task()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  //release op，then rollback transcation
  if (task_started_) {
    if (OB_SUCCESS != (tmp_ret = release_op())) {
    }
    ret = COVER_SUCC(tmp_ret);
  }
  
  task_started_ = false;
  errcode_ = OB_SUCCESS;
  return ret;
}

int ObIDASTaskOp::init_das_snapshot_opt_info(transaction::ObTxIsolationLevel isolation_level)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_das_snapshot_opt_info().init(isolation_level))) {
  } else {
    snapshot_ = get_das_snapshot_opt_info().get_specify_snapshot();
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObIDASTaskOp,
                    task_id_,
                    task_flag_,
                    tablet_id_,
                    related_ctdefs_,
                    related_rtdefs_,
                    related_tablet_ids_,
                    attach_ctdef_,
                    attach_rtdef_,
                    das_snapshot_opt_info_,
                    plan_line_id_);

OB_DEF_SERIALIZE(ObDASSnapshotOptInfo)
{
  int ret = OB_SUCCESS;
  bool serialize_specify_snapshot = specify_snapshot_ == nullptr ? false : true;
  LST_DO_CODE(OB_UNIS_ENCODE,
              use_specify_snapshot_,
              isolation_level_,
              serialize_specify_snapshot);
  if (serialize_specify_snapshot) {
    OB_UNIS_ENCODE(*specify_snapshot_);
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObDASSnapshotOptInfo)
{
  int ret = OB_SUCCESS;
  bool serialize_specify_snapshot = false;
  LST_DO_CODE(OB_UNIS_DECODE,
              use_specify_snapshot_,
              isolation_level_,
              serialize_specify_snapshot);
  if (serialize_specify_snapshot) {
    if (OB_FAIL(init(isolation_level_))) {
    } else {
      OB_UNIS_DECODE(*specify_snapshot_);
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDASSnapshotOptInfo)
{
  int64_t len = 0;
  bool serialize_specify_snapshot = specify_snapshot_ == nullptr ? false : true;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              use_specify_snapshot_,
              isolation_level_,
              serialize_specify_snapshot);
  if (serialize_specify_snapshot) {
    OB_UNIS_ADD_LEN(*specify_snapshot_);
  }
  return len;
}

int ObDASSnapshotOptInfo::init(transaction::ObTxIsolationLevel isolation_level)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  void *buf2 = nullptr;
  int64_t mem_size = sizeof(transaction::ObTxReadSnapshot);
  if (OB_ISNULL(buf = alloc_.alloc(mem_size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory for ObTxReadSnapshot", K(ret), K(mem_size));
  } else if (OB_ISNULL(buf2 = alloc_.alloc(mem_size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory for ObTxReadSnapshot", K(ret), K(mem_size));
  } else {
    use_specify_snapshot_ = true;
    isolation_level_ = isolation_level;
    specify_snapshot_ = new(buf) transaction::ObTxReadSnapshot();
    response_snapshot_ = new(buf2) transaction::ObTxReadSnapshot();
  }
  return ret;
}

int ObIDASTaskOp::state_advance()
{
  int ret = OB_SUCCESS;
  OB_ASSERT(cur_agg_list_ != nullptr);
  OB_ASSERT(task_status_ != ObDasTaskStatus::UNSTART);
  if (task_status_ == ObDasTaskStatus::FINISHED) {
    if (OB_FAIL(get_agg_task()->move_to_success_tasks(this))) {
    }
  } else if (task_status_ == ObDasTaskStatus::FAILED) {
    if (OB_FAIL(get_agg_task()->move_to_failed_tasks(this))) {
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid task state",KR(ret), K_(task_status));
  }
  return ret;
}

int DASOpResultIter::get_next_row()
{
  int ret = OB_SUCCESS;
  if (!task_iter_.is_end()) {
    ObDASScanOp *scan_op = DAS_SCAN_OP(*task_iter_);
    if (OB_ISNULL(scan_op)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected das task op type", K(ret), KPC(*task_iter_));
    } else {
      ret = scan_op->get_output_result_iter()->get_next_row();
    }
  } else {
    ret = OB_ITER_END;
  }
  return ret;
}


int DASOpResultIter::next_result()
{
  int ret = OB_SUCCESS;
  if (!task_iter_.is_end()) {
    ++task_iter_;
  }
  if (OB_UNLIKELY(task_iter_.is_end())) {
    ret = OB_ITER_END;
  }
  return ret;
}

int DASOpResultIter::reset_wild_datums_ptr()
{
  int ret = OB_SUCCESS;
  if (wild_datum_info_ != nullptr) {
    if (wild_datum_info_->exprs_ != nullptr &&
        wild_datum_info_->max_output_rows_ > 0) {
      FOREACH_CNT(e, *wild_datum_info_->exprs_) {
        (*e)->locate_datums_for_update(wild_datum_info_->eval_ctx_,
                                       wild_datum_info_->max_output_rows_);
        ObEvalInfo &info = (*e)->get_eval_info(wild_datum_info_->eval_ctx_);
        info.point_to_frame_ = true;
      }
      wild_datum_info_->exprs_ = nullptr;
      wild_datum_info_->max_output_rows_ = 0;
    }
    // A global index scan and its lookup can share expressions. Associate the
    // two iterators so resetting either side also restores the shared datums.
    if (wild_datum_info_->lookup_iter_ != nullptr) {
      wild_datum_info_->lookup_iter_->reset_wild_datums_ptr();
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
