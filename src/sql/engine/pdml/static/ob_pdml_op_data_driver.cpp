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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/pdml/static/ob_pdml_op_data_driver.h"
#include "sql/engine/dml/ob_dml_service.h"
#include "sql/engine/px/ob_px_sqc_handler.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;


ObPDMLOpDataDriver::~ObPDMLOpDataDriver()
{
}
// data_service split into DMLDataReader, DMLDataWriter two interfaces,
// Completely decouple reading data, writing data conceptually
// Initialize the cache structure in pdml data driver
int ObPDMLOpDataDriver::init(const ObTableModifySpec &spec,
                             ObIAllocator &allocator,
                             ObDMLBaseRtDef &dml_rtdef,
                             ObDMLOpDataReader *reader,
                             ObDMLOpDataWriter *writer,
                             const bool is_heap_table_insert,
                             const bool with_barrier/*false*/)
{
  UNUSED(allocator);
  int ret = OB_SUCCESS;
  last_row_.reuse_ = true; // Do not reallocate memory for each row storage, reuse the memory of the previous row
  op_monitor_info_.otherstat_1_value_ = 0;
  op_monitor_info_.otherstat_1_id_ = ObSqlMonitorStatIds::PDML_PARTITION_FLUSH_TIME;
  op_monitor_info_.otherstat_2_value_ = 0;
  op_monitor_info_.otherstat_2_id_ = ObSqlMonitorStatIds::PDML_GET_ROW_COUNT_FROM_CHILD_OP;
  op_monitor_info_.otherstat_3_value_ = 0;
  op_monitor_info_.otherstat_3_id_ = ObSqlMonitorStatIds::PDML_WRITE_DAS_BUFF_ROW_COUNT;
  op_monitor_info_.otherstat_4_value_ = 0;
  op_monitor_info_.otherstat_4_id_ = ObSqlMonitorStatIds::PDML_SKIP_ROW_COUNT;
  op_monitor_info_.otherstat_5_value_ = 0;
  op_monitor_info_.otherstat_5_id_ = ObSqlMonitorStatIds::PDML_STORAGE_RETURN_ROW_COUNT;

  if (OB_ISNULL(reader)
      || OB_ISNULL(writer)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parameters", K(reader), K(writer));
  } else {
    reader_ = reader;
    writer_ = writer;
    dml_rtdef_ = &dml_rtdef;
    is_heap_table_insert_ = is_heap_table_insert;
    with_barrier_ = with_barrier;
  }
  // Initialize cache object
  if (OB_SUCC(ret)) {
    // Initialize cache when, need to consider the barrier
    // 1. No barrier situation does not dump
    // 2. Need to perform dump in case of barrier
    // TODO: The actual number of partitions processed by the current operator is temporarily set to 1, determine the number of hashmap buckets
    //       Need CG to actually calculate and pass in
    if (OB_FAIL(cache_.init(MTL_ID(), 1, with_barrier_, spec))) {
      LOG_WARN("failed to init batch row cache", K(ret));
    } else {
      LOG_TRACE("init pdml data driver", KPC(dml_rtdef_));
    }
  }
  return ret;
}

int ObPDMLOpDataDriver::destroy()
{
  int ret = OB_SUCCESS;
  returning_ctx_.reset();
  cache_.destroy();
  eval_ctx_ = nullptr;
  reader_ = NULL;
  writer_ = NULL;
  state_ = FILL_CACHE;
  last_row_.reset();
  last_row_tablet_id_ = OB_INVALID_ID;
  last_row_expr_ = nullptr;
  op_id_ = OB_INVALID_ID;
  is_heap_table_insert_ = false;
  with_barrier_ = false;
  dfo_id_ = OB_INVALID_ID;
  return ret;
}

int ObPDMLOpDataDriver::set_dh_barrier_param(uint64_t op_id,
                                             const ObPxMultiPartModifyOpInput *modify_input)
{
    int ret = OB_SUCCESS;
    op_id_ = op_id;
    if (OB_ISNULL(modify_input)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table modify is null", K(ret));
    } else {
      dfo_id_ = modify_input->get_dfo_id();
    }
    return ret;
}

int ObPDMLOpDataDriver::get_next_row(ObExecContext &ctx, const ObExprPtrIArray &row)
{
  int ret = OB_SUCCESS;
  bool found = false;
  do {
    // STEP1. Each time get_next_row is called, attempt to drive fetching the next batch of data and flush it into the storage layer
    if (FILL_CACHE == state_) {
      if (OB_FAIL(cache_.reuse_after_rows_processed())) { // reuse cache, will only clean up the state; will not release memory, convenient for memory reuse
        LOG_WARN("fail reuse cache", K(ret));
      } else if (OB_FAIL(fill_cache_unitl_cache_full_or_child_iter_end(ctx))) { // fill cache
        LOG_WARN("failed to fill the cache", K(ret));
      } else if (!cache_.empty()) {
        if (OB_FAIL(write_partitions(ctx))) { // Perform DML operations on data in cache
          LOG_WARN("fail write partitions", K(ret));
        } else if (OB_FAIL(switch_to_returning_state(ctx))) {
          LOG_WARN("fail init returning state, fail transfer state to ROW_RETURNING", K(ret));
        } else {
          state_ = ROW_RETURNING;
        }
      } else {
        // After filling data, there is still no data in cache, indicating no data
        state_ = ROW_RETURNING;
        ret = OB_ITER_END;
      }
      // No data, or all data has been written to the storage layer, then barrier waits for global write completion
      if (with_barrier_ && (OB_ITER_END == ret || OB_SUCCESS == ret)) {
        int tmp_ret = barrier(ctx);
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN("barrier fail. fail wait all dml op finish", K(tmp_ret), K(ret));
          ret = tmp_ret;
        }
      }
    }
    // STEP2. get_next_row return data from cache
    if (OB_SUCC(ret) && ROW_RETURNING == state_) {
      if (OB_FAIL(next_row_from_cache_for_returning(row))) {
        if (OB_ITER_END == ret) {
          if (!with_barrier_) {
            // Indicates that the data in the cache has been read, and needs to be refilled
            ret = OB_SUCCESS;
            state_ = FILL_CACHE;
          }
        } else {
          // An exception error occurred
          LOG_WARN("failed to next row from cache", K(ret));
        }
      } else {
        found = true;
        LOG_DEBUG("read row from cache", K(row), K(state_));
      }
    }
  } while (OB_SUCC(ret) && FILL_CACHE == state_ && !found);

  return ret;
}


int ObPDMLOpDataDriver::fill_cache_unitl_cache_full_or_child_iter_end(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  bool is_direct_load = false;
  const ObPhysicalPlanCtx *plan_ctx = nullptr;
  if (OB_ISNULL(reader_) || OB_ISNULL(eval_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the reader is null", K(ret));
  } else if (OB_ISNULL(plan_ctx = ctx.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null physical plan (ctx)", KR(ret), KP(plan_ctx));
  } else if (OB_FALSE_IT(is_direct_load = plan_ctx->get_is_direct_insert_plan())) {
    // Try to append the row data read from child last time, but not added to cache
  } else if (OB_FAIL(try_write_last_pending_row())) {
    LOG_WARN("fail write last pending row into cache", K(ret));
  } else {
    do {
      const ObExprPtrIArray *row = nullptr;
      ObTabletID tablet_id;
      bool is_skipped = false;
      if (OB_FAIL(reader_->read_row(ctx, row, tablet_id, is_skipped))) {
        if (OB_ITER_END == ret) {
          // Current reader's data has been read to the end
          // do nothing
        } else {
          LOG_WARN("failed to read row from reader", K(ret));
        }
      } else if (is_skipped) {
        //need to skip this row
      } else if (is_heap_table_insert_
          && OB_FAIL(set_heap_table_hidden_pk(row, tablet_id, is_direct_load))) {
        LOG_WARN("fail to set heap table hidden pk", K(ret), K(*row), K(tablet_id), K(is_direct_load));
      } else if (OB_FAIL(cache_.add_row(*row, tablet_id))) {
        if (!with_barrier_ && OB_EXCEED_MEM_LIMIT == ret) {
          // Currently does not support caching the last row of data
          // If the last row of data cannot be pushed into memory, report an error and return directly
          LOG_TRACE("the cache is overflow, the current row will be cached in the last row",
                    K(ret), KPC(row), K(tablet_id));
          // Temporarily retain the current row in last_row, waiting for the next round to fill the cache when,
          // Through the `try_write_last_pending_row` function write the last row data into the cache
          if (OB_FAIL(last_row_.save_store_row(*row, *eval_ctx_))) {
            LOG_WARN("fail cache last row", K(*row), K(ret));
          } else {
            last_row_tablet_id_ = tablet_id;
            last_row_expr_ = row;
          }
          break;
        } else {
          LOG_WARN("failed to add row to cache", K_(with_barrier), K(ret));
        }
      } else {
        LOG_DEBUG("add row to cache successfully", "row", ROWEXPR2STR(*eval_ctx_, *row), K(tablet_id));
      }
    } while (OB_SUCCESS == ret);
    // reader has finished reading error, can be processed
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}
// Write all partition data cached in cache to the storage layer
// Note: Data cannot be released from cache after writing is complete, because this data is still needed
// return to the operator above DML continue using
int ObPDMLOpDataDriver::write_partitions(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObTabletIDArray tablet_id_array;
  ObPDMLOpRowIterator *row_iter = nullptr;
  if (OB_ISNULL(writer_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the writer is null", K(ret));
  } else if (OB_FAIL(cache_.get_part_id_array(tablet_id_array))) {
    LOG_WARN("fail get part index iterator", K(ret));
  } else {
    // Total time consumed in the storage layer
    TimingGuard g(op_monitor_info_.otherstat_1_value_);
    // Write to storage layer by partition
    FOREACH_X(it, tablet_id_array, OB_SUCC(ret)) {
      ObTabletID tablet_id = *it;
      ObDASTabletLoc *tablet_loc = nullptr;
      ObDASTableLoc *table_loc = dml_rtdef_->das_base_rtdef_.table_loc_;
      if (OB_FAIL(cache_.get_row_iterator(tablet_id, row_iter))) {
        LOG_WARN("fail get row iterator", K(tablet_id), K(ret));
      } else if (OB_FAIL(DAS_CTX(ctx).extended_tablet_loc(*table_loc, tablet_id, tablet_loc))) {
        LOG_WARN("extended tablet location failed", K(ret));
      } else if (OB_FAIL(writer_->write_rows(ctx, tablet_loc, *row_iter))) {
        LOG_WARN("fail write rows", K(tablet_id), K(ret));
      }
      if (NULL != row_iter) {
        row_iter->close();
        row_iter = NULL;
      }
    }
  }
  return ret;
}
// Last row read from data_service encountered a cache error when attempting to write to cache
// Report size overflow, this line is recorded as last_row_, to be cached
// Data written to storage layer, then write last_row_ to cache
inline int ObPDMLOpDataDriver::try_write_last_pending_row()
{
  int ret = OB_SUCCESS;
  if (last_row_tablet_id_.is_valid()) {
    ObChunkDatumStore::StoredRow *store_row = last_row_.store_row_;
    if (OB_ISNULL(store_row) || OB_ISNULL(eval_ctx_) || OB_ISNULL(last_row_expr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected", KP(store_row), KP_(last_row_expr), K(ret));
    } else if (OB_FAIL(store_row->to_expr(*last_row_expr_, *eval_ctx_))) {
      LOG_WARN("fail store row to expr", K(ret));
    } else if (OB_FAIL(cache_.add_row(*last_row_expr_, last_row_tablet_id_))) {
      LOG_WARN("fail add cached last row", K(ret), K(last_row_tablet_id_));
    } else {
      // After adding the leftover row from last time to the cache, clean up the last row pointer and last row part id values
      // However, for memory reuse, the memory of last_row_ is not cleaned up
      last_row_tablet_id_.reset();
      last_row_expr_ = nullptr;
    }
  }
  return ret;
}
// Each time fill cache ends, change the state to ROW_RETURNING state
int ObPDMLOpDataDriver::switch_to_returning_state(ObExecContext &ctx)
{
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  if (cache_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cache is empty for init returning state", K(ret));
  } else {
    returning_ctx_.reset();
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(cache_.get_part_id_array(returning_ctx_.tablet_id_array_))) {
      LOG_WARN("failed to get part id array for init returning state", K(ret));
    } else if (0 == returning_ctx_.tablet_id_array_.count()) { // TODO: redundant check
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("part id array is empty for init returning state", K(ret));
    }
  }
  return ret;
}

int ObPDMLOpDataDriver::barrier(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObPxSqcHandler *handler = ctx.get_sqc_handler();
  if (OB_ISNULL(handler)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("barrier only supported in parallel execution mode", K_(with_barrier));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "barrier in non-px mode");
  } else if ((!with_barrier_) || (dfo_id_ == OB_INVALID_ID)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K_(with_barrier), K(dfo_id_ == OB_INVALID_ID), K(ret));
  } else {
    ObPxSQCProxy &proxy = handler->get_sqc_proxy();
    ObBarrierPieceMsg piece;
    piece.source_dfo_id_ = dfo_id_;
    piece.target_dfo_id_ = dfo_id_;
    piece.op_id_ = op_id_;
    piece.thread_id_ = GETTID();
    const ObBarrierWholeMsg *whole = nullptr;
    if (OB_FAIL(proxy.get_dh_msg_sync(op_id_,
                                      dtl::DH_BARRIER_WHOLE_MSG,
                                      piece,
                                      whole,
                                      ctx.get_physical_plan_ctx()->get_timeout_timestamp()))) {
      LOG_WARN("fail get barrier msg", K(ret));
    }
  }
  return ret;
}

int ObPDMLOpDataDriver::next_row_from_cache_for_returning(const ObExprPtrIArray &row)
{
  int ret = OB_SUCCESS;
  bool found = false;
  do {
    if (OB_ISNULL(returning_ctx_.row_iter_)) {
      // do nothing
    } else if (OB_FAIL(returning_ctx_.row_iter_->get_next_row(row))) {
      if (OB_ITER_END == ret) {
        // Current partition's row iter data iteration is complete, need to switch to the next partition
        ret = OB_SUCCESS;
        LOG_TRACE("current partition row iter has been iterated to end",
          K(returning_ctx_.next_idx_));
      } else {
        LOG_WARN("failed to get next row from returning ctx row iter", K(ret));
      }
    } else {
      found = true;
    }
    if (OB_SUCC(ret) && !found) {
      // Switch to the next partition
      if (OB_FAIL(switch_row_iter_to_next_partition())) {
        if (OB_ITER_END == ret) {
          // Indicates there is no next partition, return OB_ITER_END
          LOG_TRACE("no next partition row iter can be switched to", K(ret));
        } else {
          LOG_WARN("failed to switch next partition row iter", K(ret));
        }
      } else {
        // do nothing
      }
    }
  } while (OB_SUCC(ret) && !found);

  return ret;
}

int ObPDMLOpDataDriver::switch_row_iter_to_next_partition()
{
  int ret = OB_SUCCESS;
  if (returning_ctx_.row_iter_) {
    returning_ctx_.row_iter_->close();
  }
  // Current cache only one line of data
  // next idx is only equal to 0, if next idx equals 1, it indicates no data
  if (OB_SUCC(ret) && returning_ctx_.next_idx_ >= returning_ctx_.tablet_id_array_.count()) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(cache_.get_row_iterator(
              returning_ctx_.tablet_id_array_.at(returning_ctx_.next_idx_),
              returning_ctx_.row_iter_))) {
    int64_t next_idx = returning_ctx_.next_idx_;
    LOG_WARN("failed to get next partition iterator", K(ret),
        "part_id", returning_ctx_.tablet_id_array_.at(next_idx), K(next_idx));
  } else {
    returning_ctx_.next_idx_++;
  }
  return ret;
}

int ObPDMLOpDataDriver::set_heap_table_hidden_pk(
    const ObExprPtrIArray *&row,
    ObTabletID &tablet_id,
    const bool is_direct_load)
{
  int ret = OB_SUCCESS;
  uint64_t pk_value = 0;
  if (!is_direct_load) {
    uint64_t autoinc_seq = 0;
    ObSQLSessionInfo *my_session = eval_ctx_->exec_ctx_.get_my_session();
    uint64_t tenant_id = my_session->get_effective_tenant_id();
    if (OB_FAIL(ObDMLService::get_heap_table_hidden_pk(tenant_id,
                                                       tablet_id,
                                                       autoinc_seq))) {
      LOG_WARN("fail to get hidden pk", KR(ret), K(tablet_id), K(tenant_id));
    } else {
      pk_value = autoinc_seq;
    }
  } else {
    // init the datum with a simple value to avoid core in project_storage_row(),
    // direct-load will generate the real hidden pk later by itself
    pk_value = 0;
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(set_heap_table_hidden_pk_value(row, tablet_id, pk_value))) {
      LOG_WARN("fail to set heap table hidden pk value", KR(ret), K(tablet_id), K(pk_value));
    }
  }
  return ret;
}

int ObPDMLOpDataDriver::set_heap_table_hidden_pk_value(
    const ObExprPtrIArray *&row,
    ObTabletID &tablet_id,
    const uint64_t pk_value)
{
  int ret = OB_SUCCESS;
  ObExpr *auto_inc_expr = nullptr;
  for (int64_t i = 0; OB_SUCC(ret) && i < row->count(); ++i) {
    if (T_TABLET_AUTOINC_NEXTVAL == row->at(i)->type_) {
      auto_inc_expr = row->at(i);
      break;
    }
  }
  if (OB_ISNULL(auto_inc_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cannot find tablet autoinc expr", KR(ret), KPC(row));
  } else {
    ObDatum &datum = auto_inc_expr->locate_datum_for_write(*eval_ctx_);
    datum.set_uint(pk_value);
    auto_inc_expr->set_evaluated_projected(*eval_ctx_);
  }
  return ret;
}
