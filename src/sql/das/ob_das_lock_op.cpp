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
#include "sql/das/ob_das_lock_op.h"
#include "data_plane/ob_i_dml_service.h"
#include "data_plane/ob_i_write_context_service.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/engine/dml/ob_dml_service.h"
namespace oceanbase
{
namespace common
{
namespace serialization
{
template <>
struct EnumEncoder<false, const sql::ObDASLockCtDef *> : sql::DASCtRefEncoder<sql::ObDASLockCtDef>
{
};

template <>
struct EnumEncoder<false, sql::ObDASLockRtDef *> : sql::DASRtRefEncoder<sql::ObDASLockRtDef>
{
};
} // end namespace serialization
} // end namespace common

using namespace common;
using namespace storage;
namespace sql
{
ObDASLockOp::ObDASLockOp(ObIAllocator &op_alloc)
  : ObIDASTaskOp(op_alloc),
    lock_ctdef_(nullptr),
    lock_rtdef_(nullptr),
    lock_buffer_(),
    affected_rows_(0)
{
}

int ObDASLockOp::open_op()
{
  int ret = OB_SUCCESS;
  data_plane::ObDmlExecution execution;
  int64_t affected_rows;
  concurrent_control::ObWriteFlag write_flag;

  ObDASDMLIterator dml_iter(
      lock_ctdef_, lock_buffer_, op_alloc_, srs_provider_,
      lob_read_options_);
  data_plane::ObIDmlService *as = ::oceanbase::share::server_service<::oceanbase::data_plane::ObIDmlService>();
  data_plane::ObWriteContext write_context;

  (void)ObDMLService::init_dml_write_flag(
      *lock_ctdef_, *lock_rtdef_, write_flag,
      das_snapshot_opt_info_.use_specify_snapshot_);
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::data_plane::ObIWriteContextService>()->acquire_write_context(
          lock_rtdef_->timeout_ts_,
          *trans_desc_,
          *snapshot_,
          write_branch_id_,
          write_flag,
          write_context))) {
    LOG_WARN("fail to acquire write context", K(ret));
  } else if (OB_FAIL(ObDMLService::prepare_dml_execution(
      *lock_ctdef_,
      *lock_rtdef_,
      *snapshot_,
      write_branch_id_,
      op_alloc_,
      write_context,
      execution,
      das_snapshot_opt_info_.use_specify_snapshot_))) {
    LOG_WARN("init dml param failed", K(ret));
  } else if (OB_FAIL(as->lock_rows(tablet_id_,
                                   *trans_desc_,
                                   execution,
                                   lock_rtdef_->for_upd_wait_time_,
                                   static_cast<data_plane::ObRowLockMode>(
                                       lock_ctdef_->lock_flag_),
                                   &dml_iter,
                                   affected_rows))) {
    if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
      LOG_WARN("lock row to partition storage failed", K(ret));
    }
  } else {
    affected_rows_ = affected_rows;
  }
  return ret;
}

int ObDASLockOp::release_op()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObDASLockOp::record_task_result_to_rtdef()
{
  int ret = OB_SUCCESS;
  lock_rtdef_->affected_rows_ += affected_rows_;
  return ret;
}

int ObDASLockOp::assign_task_result(ObIDASTaskOp *other)
{
  int ret = OB_SUCCESS;
  if (other->get_type() != get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected task type", K(ret), KPC(other));
  } else {
    ObDASLockOp *lock_op = static_cast<ObDASLockOp *>(other);
    affected_rows_ = lock_op->get_affected_rows();
  }
  return ret;
}

int ObDASLockOp::init_task_info(uint32_t row_extend_size)
{
  int ret = OB_SUCCESS;
  if (!lock_buffer_.is_inited()
      && OB_FAIL(lock_buffer_.init(CURRENT_CONTEXT->get_allocator(),
                                   row_extend_size,
                                   "DASLockBuffer"))) {
    LOG_WARN("init lock buffer failed", K(ret));
  }
  return ret;
}

int ObDASLockOp::write_row(const ExprFixedArray &row,
                           ObEvalCtx &eval_ctx,
                           ObChunkDatumStore::StoredRow *&stored_row)
{
  int ret = OB_SUCCESS;
  if (!lock_buffer_.is_inited()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("buffer not inited", K(ret));
  } else if (OB_FAIL(lock_buffer_.add_row(row, &eval_ctx, stored_row, true))) {
    LOG_WARN("add row to lock buffer failed", K(ret), K(row), K(lock_buffer_));
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObDASLockOp, ObIDASTaskOp),
                    lock_ctdef_,
                    lock_rtdef_,
                    lock_buffer_);

}  // namespace sql
}  // namespace oceanbase
