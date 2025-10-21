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
#include "sql/engine/px/ob_px_util.h"

namespace oceanbase
{
namespace common
{
namespace serialization
{
template <>
struct EnumEncoder<false, const sql::ObDASBaseCtDef*> : sql::DASCtEncoder<sql::ObDASBaseCtDef>
{
};

template <>
struct EnumEncoder<false, sql::ObDASBaseRtDef*> : sql::DASRtEncoder<sql::ObDASBaseRtDef>
{
};
} // end namespace serialization
} // end namespace common

using namespace common;
using namespace transaction;
namespace sql
{
OB_DEF_SERIALIZE_SIZE(ObDASRemoteInfo)
{
  int64_t len = 0;
  ObSQLSessionInfo *session = exec_ctx_->get_my_session();
  OB_UNIS_ADD_LEN(flags_);
  if (need_tx_) {
    OB_UNIS_ADD_LEN(*trans_desc_);
  }
  OB_UNIS_ADD_LEN(snapshot_);
  if (need_calc_expr_ || need_calc_udf_) {
    OB_UNIS_ADD_LEN(session->get_effective_tenant_id());
    OB_UNIS_ADD_LEN(*session);
  }
  if (has_expr_) {
    len += ObPxTreeSerializer::get_serialize_expr_frame_info_size<true>(*exec_ctx_,
             const_cast<ObExprFrameInfo&>(*frame_info_));
  }
  OB_UNIS_ADD_LEN(ctdefs_.count());
  for (int64_t i = 0; i < ctdefs_.count(); ++i) {
    const ObDASBaseCtDef *ctdef = ctdefs_.at(i);
    OB_UNIS_ADD_LEN(ctdef->op_type_);
    OB_UNIS_ADD_LEN(*ctdef);
  }
  OB_UNIS_ADD_LEN(rtdefs_.count());
  for (int64_t i = 0; i < rtdefs_.count(); ++i) {
    ObDASBaseRtDef *rtdef = rtdefs_.at(i);
    OB_UNIS_ADD_LEN(rtdef->op_type_);
    OB_UNIS_ADD_LEN(*rtdef);
  }

  OB_UNIS_ADD_LEN(sizeof(sql_id_));
  len += sizeof(sql_id_);
  OB_UNIS_ADD_LEN(user_id_);
  OB_UNIS_ADD_LEN(session_id_);
  OB_UNIS_ADD_LEN(plan_id_);
  OB_UNIS_ADD_LEN(plan_hash_);
  //Serializing the reference relationship between ctdefs and rtdefs.
  for (int i = 0; i < ctdefs_.count(); ++i) {
    const ObDASBaseCtDef *ctdef = ctdefs_.at(i);
    OB_UNIS_ADD_LEN(ctdef->children_cnt_);
    for (int j = 0; j < ctdef->children_cnt_; ++j) {
      const ObDASBaseCtDef *child_ctdef = ctdef->children_[j];
      OB_UNIS_ADD_LEN(child_ctdef);
    }
  }
  for (int i = 0; i < rtdefs_.count(); ++i) {
    ObDASBaseRtDef *rtdef = rtdefs_.at(i);
    OB_UNIS_ADD_LEN(rtdef->ctdef_);
    OB_UNIS_ADD_LEN(rtdef->children_cnt_);
    for (int j = 0; j < rtdef->children_cnt_; ++j) {
      ObDASBaseRtDef *child_rtdef = rtdef->children_[j];
      OB_UNIS_ADD_LEN(child_rtdef);
    }
  }
  if (need_subschema_ctx_) {
    if (OB_NOT_NULL(exec_ctx_->get_physical_plan_ctx()->get_phy_plan())) {
      OB_UNIS_ADD_LEN(exec_ctx_->get_physical_plan_ctx()->get_phy_plan()->get_subschema_ctx());
    } else {
      OB_UNIS_ADD_LEN(exec_ctx_->get_physical_plan_ctx()->get_subschema_ctx());
    }
  }
  OB_UNIS_ADD_LEN(stmt_type_);
  return len;
}

int ObIDASTaskOp::swizzling_remote_task(ObDASRemoteInfo *remote_info)
{
  int ret = OB_SUCCESS;
  if (remote_info != nullptr) {
    snapshot_ = &remote_info->snapshot_;
    if (das_gts_opt_info_.use_specify_snapshot_) {
      if (OB_ISNULL(das_gts_opt_info_.get_specify_snapshot())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected nullptr of specify_snapshot", K(ret));
      } else {
        snapshot_ = das_gts_opt_info_.get_specify_snapshot();
      }
    }
  }
  return ret;
}

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
  // no need to advance state here because this function could be called on remote executor.
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
      LOG_WARN("release das task op failed", K(tmp_ret), K_(errcode));
    }
    ret = COVER_SUCC(tmp_ret);
  }
  
  task_started_ = false;
  errcode_ = OB_SUCCESS;
  return ret;
}

int ObIDASTaskOp::init_das_gts_opt_info(transaction::ObTxIsolationLevel isolation_level)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_das_gts_opt_info().init(isolation_level))) {
    LOG_WARN("fail to init das gts opt", K(ret), K(isolation_level));
  } else {
    snapshot_ = get_das_gts_opt_info().get_specify_snapshot();
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObIDASTaskOp,
                    tenant_id_,
                    task_id_,
                    task_flag_,
                    tablet_id_,
                    ls_id_,
                    related_ctdefs_,
                    related_rtdefs_,
                    related_tablet_ids_,
                    attach_ctdef_,
                    attach_rtdef_,
                    das_gts_opt_info_,
                    plan_line_id_);

OB_DEF_SERIALIZE(ObDASGTSOptInfo)
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

OB_DEF_DESERIALIZE(ObDASGTSOptInfo)
{
  int ret = OB_SUCCESS;
  bool serialize_specify_snapshot = false;
  LST_DO_CODE(OB_UNIS_DECODE,
              use_specify_snapshot_,
              isolation_level_,
              serialize_specify_snapshot);
  if (serialize_specify_snapshot) {
    if (OB_FAIL(init(isolation_level_))) {
      LOG_WARN("fail to init gts_opt_info", K(ret));
    } else {
      OB_UNIS_DECODE(*specify_snapshot_);
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDASGTSOptInfo)
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

int ObDASGTSOptInfo::init(transaction::ObTxIsolationLevel isolation_level)
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

ObDASTaskArg::ObDASTaskArg()
  : timeout_ts_(0),
    ctrl_svr_(),
    runner_svr_(),
    task_ops_(),
    remote_info_(nullptr)
{
}

OB_DEF_SERIALIZE_SIZE(ObDASTaskArg)
{
  int64_t len = 0;
  if (task_ops_.count() != 0) {
    LST_DO_CODE(OB_UNIS_ADD_LEN,
                timeout_ts_,
                ctrl_svr_,
                runner_svr_,
                *remote_info_);
    len += serialization::encoded_length_vi64(task_ops_.count());
    for (int i = 0; i < task_ops_.count(); i++) {
      len += serialization::encoded_length(task_ops_.at(i)->get_type());
      len += serialization::encoded_length(*task_ops_.at(i));
    }
  }
  return len;
}


ObIDASTaskOp *ObDASTaskArg::get_task_op()
{
  return task_ops_.at(0);
}

int ObIDASTaskOp::state_advance()
{
  int ret = OB_SUCCESS;
  OB_ASSERT(cur_agg_list_ != nullptr);
  OB_ASSERT(task_status_ != ObDasTaskStatus::UNSTART);
  if (task_status_ == ObDasTaskStatus::FINISHED) {
    if (OB_FAIL(get_agg_task()->move_to_success_tasks(this))) {
      LOG_WARN("failed to move task to success tasks", KR(ret));
    }
  } else if (task_status_ == ObDasTaskStatus::FAILED) {
    if (OB_FAIL(get_agg_task()->move_to_failed_tasks(this))) {
      LOG_WARN("failed to move task to success tasks", KR(ret));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid task state",KR(ret), K_(task_status));
  }
  return ret;
}

ObDASTaskResp::ObDASTaskResp()
  : has_more_(false),
    ctrl_svr_(),
    runner_svr_(),
    op_results_(),
    rcode_(),
    trans_result_(),
    das_factory_(nullptr)
{
}

void ObDASTaskResp::store_err_msg(const ObString &msg)
{
  int ret = OB_SUCCESS;
  if (!msg.empty()) {
    // Here the reason for using databuff_printf is that databuff_printf ensures the buffer ends with '\0' when a buffer overflow occurs, ensuring the safety of print
    if (OB_FAIL(databuff_printf(rcode_.msg_, OB_MAX_ERROR_MSG_LEN, "%.*s", msg.length(), msg.ptr()))) {
      SHARE_LOG(WARN, "store err msg failed", K(ret), K(msg));
      if (OB_SIZE_OVERFLOW == ret) {
        rcode_.msg_[OB_MAX_ERROR_MSG_LEN - 1] = '\0';
      }
    }
  } else {
    rcode_.msg_[0] = '\0';
  }
}

int ObDASTaskResp::store_warning_msg(const ObWarningBuffer &wb)
{
  int ret = OB_SUCCESS;
  bool not_null = true;
  for (uint32_t idx = 0; OB_SUCC(ret) && not_null && idx < wb.get_readable_warning_count(); idx++) {
    const ObWarningBuffer::WarningItem *item = wb.get_warning_item(idx);
    if (item != NULL) {
      if (OB_FAIL(rcode_.warnings_.push_back(*item))) {
        RPC_OBRPC_LOG(WARN, "Failed to add warning", K(ret));
      }
    } else {
      not_null = false;
    }
  }
  return ret;
}

OB_DEF_SERIALIZE(ObDASTaskResp)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              has_more_,
              ctrl_svr_,
              runner_svr_);
  if (OB_SUCC(ret) && OB_FAIL(serialization::encode_vi64(
      buf, buf_len, pos, op_results_.count()))) {
    LOG_WARN("fail to encode ob array count", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < op_results_.count(); i ++) {
    if (OB_FAIL(serialization::encode(buf, buf_len, pos, *op_results_.at(i)))) {
      LOG_WARN("fail to encode item", K(i), K(ret));
    }
  }
  LST_DO_CODE(OB_UNIS_ENCODE,
              rcode_,
              trans_result_);
  return ret;
}

OB_DEF_DESERIALIZE(ObDASTaskResp)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  ObIDASTaskResult *op_result = nullptr;
  LST_DO_CODE(OB_UNIS_DECODE,
              has_more_,
              ctrl_svr_,
              runner_svr_);
  if (OB_SUCC(ret) && OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &count))) {
    LOG_WARN("fail to decode ob array count", K(ret));
  } else if (count > op_results_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("receive das task response count mismatch", K(count), K(op_results_.count()));
  }
  while (op_results_.count() > count) {
    op_results_.pop_back();
  }
  OB_ASSERT(op_results_.count() == count);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    if (OB_FAIL(serialization::decode(buf, data_len, pos, *op_results_.at(i)))) {
      LOG_WARN("fail to decode array item", K(ret), K(i), K(count));
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE,
              rcode_,
              trans_result_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDASTaskResp)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              has_more_,
              ctrl_svr_,
              runner_svr_);
  len += serialization::encoded_length_vi64(op_results_.count());
  for (int i = 0; i < op_results_.count(); i++) {
    len += serialization::encoded_length(*op_results_.at(i));
  }
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              rcode_,
              trans_result_);
  return len;
}

int ObDASTaskResp::add_op_result(ObIDASTaskResult *op_result)
{
  return op_results_.push_back(op_result);
}

OB_SERIALIZE_MEMBER(ObIDASTaskResult, task_id_);

OB_SERIALIZE_MEMBER(ObDASDataFetchReq, tenant_id_, task_id_);

int ObDASDataFetchReq::init(const uint64_t tenant_id, const int64_t task_id)
{
  tenant_id_ = tenant_id;
  task_id_ = task_id;
  return OB_SUCCESS;
}

OB_SERIALIZE_MEMBER(ObDASDataEraseReq, tenant_id_, task_id_);

int ObDASDataEraseReq::init(const uint64_t tenant_id, const int64_t task_id)
{
  tenant_id_ = tenant_id;
  task_id_ = task_id;
  return OB_SUCCESS;
}

OB_SERIALIZE_MEMBER(ObDASDataFetchRes,
                    datum_store_,
                    tenant_id_, task_id_, has_more_,
                    enable_rich_format_, vec_row_store_,
                    io_read_bytes_,
                    ssstore_read_bytes_,
                    ssstore_read_row_cnt_,
                    memstore_read_row_cnt_);

ObDASDataFetchRes::ObDASDataFetchRes()
        : datum_store_("DASDataFetch"),
          tenant_id_(0),
          task_id_(0),
          has_more_(false),
          enable_rich_format_(false),
          vec_row_store_(),
          io_read_bytes_(0),
          ssstore_read_bytes_(0),
          ssstore_read_row_cnt_(0),
          memstore_read_row_cnt_(0)
{
}

int ObDASDataFetchRes::init(const uint64_t tenant_id, const int64_t task_id)
{
  int ret = OB_SUCCESS;
  tenant_id_ = tenant_id;
  task_id_ = task_id;
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
    LOG_DEBUG("fetch next das task end", K(ret));
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
    //global index scan and its lookup maybe share some expr,
    //so remote lookup task change its datum ptr,
    //and also lead index scan to touch the wild datum ptr
    //so need to associate the result iterator of scan and lookup
    //resetting the index scan result datum ptr will also reset the lookup result datum ptr
    if (wild_datum_info_->lookup_iter_ != nullptr) {
      wild_datum_info_->lookup_iter_->reset_wild_datums_ptr();
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
