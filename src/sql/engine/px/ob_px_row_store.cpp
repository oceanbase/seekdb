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

#include "ob_px_row_store.h"
#include "sql/dtl/ob_dtl.h"
#include "sql/engine/expr/ob_array_expr_utils.h"


using namespace oceanbase::common;
using namespace oceanbase::sql;

/*
 * This file describes:
 * To improve efficiency, ObPxNewRow does some rather trick things in the deserialization process
 *
 * ObPxNewRow copies the serialized row payload out of the local DTL buffer.
 * The linked buffer can then be released when its processing callback ends,
 * while the decoded row remains valid for get_next_row().
 */

void ObPxNewRow::set_eof_row()
{
  row_cell_count_ = EOF_ROW_FLAG;
  des_row_buf_size_ = 0;
  des_row_buf_ = NULL;
}

OB_DEF_SERIALIZE(ObPxNewRow)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(row_cell_count_);
  if (OB_FAIL(ret)) {
  } else if (OB_LIKELY(NULL != row_)) {
    for (int64_t idx = 0; OB_SUCC(ret) && idx < row_->get_count(); ++idx) {
      const ObObj &cell = row_->get_cell(idx);
      if (OB_FAIL(serialization::encode(buf, buf_len, pos, cell))) {
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObPxNewRow)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(row_cell_count_);
  if (OB_LIKELY(NULL != row_ && row_cell_count_ > 0)) {
    for (int64_t idx = 0; idx < row_->get_count(); ++idx) {
      const ObObj &cell = row_->get_cell(idx);
      len += serialization::encoded_length(cell);
    }
  }
  return len;
}

OB_DEF_DESERIALIZE(ObPxNewRow)
{
  int ret = OB_SUCCESS;
  // reset value
  des_row_buf_ = NULL;
  des_row_buf_size_ = 0;
  OB_UNIS_DECODE(row_cell_count_);
  if (OB_FAIL(ret)) {
  } else if (OB_LIKELY(row_cell_count_ > 0)) {
    if (OB_UNLIKELY(pos >= data_len)) {
      ret = OB_SERIALIZE_ERROR;
      LOG_WARN("invalid serialization data", K(pos), K(data_len), K_(row_cell_count), K(ret));
    } else {
      // Delay reading row's cells until the get_row stage
      des_row_buf_ = (char*)buf + pos;
      des_row_buf_size_ = data_len - pos;
      pos += des_row_buf_size_;
    }
  }
  return ret;
}
// Used to copy row from DTL memory to get_next_row context for output
// If not copied, the memory of row will be released after the DTL process call ends,
// get_next_row will obtain an illegal memory reference
// Decode the buffered row and construct an ObNewRow view.

int ObReceiveRowReader::add_buffer(dtl::ObDtlLinkedBuffer &buf, bool &transferred)
{
  int ret = OB_SUCCESS;
  transferred = false;
  if (!buf.is_data_msg()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not data message", K(ret));
  } else if (buf.msg_type() < 0) {
    // for interm result iterator.
    dtl::ObDtlMsgType msg_type = static_cast<dtl::ObDtlMsgType>(-buf.msg_type());
    if (dtl::PX_DATUM_ROW == msg_type) {
      if (NULL != datum_iter_ && datum_iter_->is_valid() && datum_iter_->has_next()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("rows must be all iterated before new iterate added", K(ret));
      } else {
        datum_iter_ = reinterpret_cast<ObChunkDatumStore::Iterator *>(buf.buf());
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected msg_type", K(ret), K(msg_type));
    }
  } else {
    // add buffer to receive list.
    int64_t rows = 0;
    if (dtl::PX_DATUM_ROW == buf.msg_type()) {
      auto block = reinterpret_cast<ObChunkDatumStore::Block *>(buf.buf());
      rows = block->rows_;
      if (rows > 0 && OB_FAIL(block->swizzling(NULL))) {
        LOG_WARN("block swizzling failed", K(ret));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get invalid msg", K(buf.msg_type()));
    }
    if (OB_SUCC(ret)){
      if (rows > 0) {
        transferred = true;
        recv_list_rows_ += rows;
        // add buffer to receive list
        buf.next_ = NULL;
        if (NULL == recv_head_) {
          recv_head_ = &buf;
          recv_tail_ = &buf;

          cur_iter_pos_ = 0;
          cur_iter_rows_ = 0;
        } else {
          recv_tail_->next_ = &buf;
          recv_tail_ = &buf;
        }
      } else {
        // no need to add buffer with no rows, keep %transferred false, return OB_ITER_END
        ret = OB_ITER_END;
      }
    }
  }
  return ret;
}

void ObReceiveRowReader::free(dtl::ObDtlLinkedBuffer *buf)
{
  // free buffer to DFC memory manager, see: ObDtlBasicChannel::free_buf()
  if (NULL != buf) {
    int ret = OB_SUCCESS;
    auto mgr = DTL.get_dfc_server().get_mem_manager();
    CK(NULL != mgr);
    OZ(mgr->free(buf));
  }
}

inline void ObReceiveRowReader::free_buffer_list(dtl::ObDtlLinkedBuffer *buf)
{
  while (NULL != buf) {
    dtl::ObDtlLinkedBuffer *next = reinterpret_cast<dtl::ObDtlLinkedBuffer *>(buf->next_);
    free(buf);
    buf = next;
  }
}

void ObReceiveRowReader::move_to_iterated(const int64_t rows)
{
  auto cur = recv_head_;
  if (recv_tail_ == recv_head_) {
    recv_tail_ = NULL;
    recv_head_ = NULL;
  } else {
    recv_head_ = reinterpret_cast<dtl::ObDtlLinkedBuffer *>(recv_head_->next_);
  }

  cur->next_ = iterated_buffers_;
  iterated_buffers_ = cur;

  recv_list_rows_ -= rows;
  cur_iter_rows_ = 0;
  cur_iter_pos_ = 0;
}

template <typename BLOCK, typename ROW>
const ROW *ObReceiveRowReader::next_store_row()
{
  const ROW *srow = NULL;
  if (NULL != recv_head_) {
    BLOCK *b = reinterpret_cast<BLOCK *>(recv_head_->buf());
    if (cur_iter_rows_ == b->rows_) {
      move_to_iterated(b->rows_);
      if (NULL != recv_head_) {
        b = reinterpret_cast<BLOCK *>(recv_head_->buf());
      } else {
        b = NULL;
      }
    }
    if (NULL != b) {
      int ret = b->get_store_row(cur_iter_pos_, srow);
      if (OB_FAIL(ret)) {
      } else {
        cur_iter_rows_ += 1;
      }
    }
  }
  return srow;
}

int ObReceiveRowReader::to_expr(const ObChunkDatumStore::StoredRow *srow,
                                const ObIArray<ObExpr*> &dynamic_const_exprs,
                                const ObIArray<ObExpr*> &exprs,
                                ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(srow)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid rows", K(ret));
  } else if (srow->cnt_ != exprs.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unmatch rows", K(ret), K(exprs.count()), K(srow->cnt_));
  } else {
    for (uint32_t i = 0; i < srow->cnt_; ++i) {
      if (exprs.at(i)->is_static_const_) {
        continue;
      } else {
        exprs.at(i)->locate_expr_datum(eval_ctx) = srow->cells()[i];
        exprs.at(i)->set_evaluated_projected(eval_ctx);
      }
    }
    // deep copy dynamic const expr datum
    if (dynamic_const_exprs.count() > 0) {
      for (int64_t i = 0; OB_SUCC(ret) && i < dynamic_const_exprs.count(); i++) {
        ObExpr *expr = dynamic_const_exprs.at(i);
        // Fixed-width datums do not reserve an external result buffer.
        if (0 != expr->res_buf_off_ && OB_FAIL(expr->deep_copy_self_datum(eval_ctx))) {
          LOG_WARN("fail to deep copy datum", K(ret), K(eval_ctx), K(*expr));
        }
      }
    }
  }
  return ret;
}

int ObReceiveRowReader::get_next_row(const ObIArray<ObExpr*> &exprs,
                                     const ObIArray<ObExpr*> &dynamic_const_exprs,
                                     ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  if (NULL != datum_iter_) {
    const ObChunkDatumStore::StoredRow *srow = NULL;
    if (!datum_iter_->is_valid()) {
      // If invalid , it is a mocked empty buffer.
      ret = OB_ITER_END;
    } else if (OB_FAIL(datum_iter_->get_next_row(srow))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get next stored row failed", K(ret));
      }
    } else {
      ret = to_expr(srow, dynamic_const_exprs, exprs, eval_ctx);
    }
  } else {
    free_iterated_buffers();
    const ObChunkDatumStore::StoredRow *srow
        = next_store_row<ObChunkDatumStore::Block, ObChunkDatumStore::StoredRow>();
    if (NULL == srow) {
      ret = OB_ITER_END;
    } else {
      ret = to_expr(srow, dynamic_const_exprs, exprs, eval_ctx);
    }
  }

  return ret;
}
// todo: shanting2.0 implement vectorized interface, format as continuous.
int ObReceiveRowReader::attach_rows(const common::ObIArray<ObExpr*> &exprs,
                                    const ObIArray<ObExpr*> &dynamic_const_exprs,
                                    ObEvalCtx &eval_ctx,
                                    const ObChunkDatumStore::StoredRow **srows,
                                    const int64_t read_rows)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(srows)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    for (int64_t col_idx = 0; col_idx < exprs.count(); col_idx++) {
      if (exprs.at(col_idx)->is_static_const_) {
        continue;
      } else {
        ObExpr *e = exprs.at(col_idx);
        ObDatum *datums = e->locate_batch_datums(eval_ctx);
        if (!e->is_batch_result()) {
          datums[0] = srows[0]->cells()[col_idx];
        } else {
          for (int64_t i = 0; i < read_rows; i++) {
            datums[i] = srows[i]->cells()[col_idx];
          }
        }
        e->set_evaluated_projected(eval_ctx);
        ObEvalInfo &info = e->get_eval_info(eval_ctx);
        info.notnull_ = false;
        info.point_to_frame_ = false;
      }
    }
    // deep copy dynamic const expr datum
    if (OB_SUCC(ret) && dynamic_const_exprs.count() > 0 && read_rows > 0) {
      ObEvalCtx::BatchInfoScopeGuard batch_info_guard(eval_ctx);
      batch_info_guard.set_batch_size(read_rows);
      batch_info_guard.set_batch_idx(0);
      for (int64_t i = 0; OB_SUCC(ret) && i < dynamic_const_exprs.count(); i++) {
        ObExpr *expr = dynamic_const_exprs.at(i);
        OB_ASSERT(!expr->is_batch_result());
        // Fixed-width datums do not reserve an external result buffer.
        if (0 != expr->res_buf_off_ && OB_FAIL(expr->deep_copy_self_datum(eval_ctx))) {
          LOG_WARN("fail to deep copy datum", K(ret), K(eval_ctx), K(*expr));
        }
      }
    }
  }

  return ret;
}

int ObReceiveRowReader::get_next_batch(const ObIArray<ObExpr*> &exprs,
                                       const ObIArray<ObExpr*> &dynamic_const_exprs,
                                       ObEvalCtx &eval_ctx,
                                       const int64_t max_rows,
                                       int64_t &read_rows,
                                       const ObChunkDatumStore::StoredRow **srows)
{
  int ret = OB_SUCCESS;
  typedef ObChunkDatumStore Store;
  if (NULL == srows) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("NULL store rows", K(ret));
  } else if (NULL != datum_iter_) {
    if (max_rows > eval_ctx.max_batch_size_) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(max_rows), K(eval_ctx.max_batch_size_));
    } else if (!datum_iter_->is_valid()) {
      // If invalid , it is a mocked empty buffer.
      ret = OB_ITER_END;
    } else if (OB_FAIL(datum_iter_->get_next_batch(srows, max_rows, read_rows))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get next batch failed", K(ret), K(max_rows));
      } else {
        read_rows = 0;
      }
    } else {
      OZ(attach_rows(exprs, dynamic_const_exprs, eval_ctx, srows, read_rows));
    }
  } else {
    free_iterated_buffers();
    read_rows = 0;
    const Store::StoredRow *srow = NULL;
    while (read_rows < max_rows
           && NULL != (srow = next_store_row<Store::Block, Store::StoredRow>())) {
      srows[read_rows++] = srow;
    }
    if (0 == read_rows) {
      ret = OB_ITER_END;
    } else {
      OZ(attach_rows(exprs, dynamic_const_exprs, eval_ctx, srows, read_rows));
    }
  }
  return ret;
}

void ObReceiveRowReader::reset()
{
  free_buffer_list(recv_head_);
  recv_head_ = NULL;
  recv_tail_ = NULL;

  free_buffer_list(iterated_buffers_);
  iterated_buffers_ = NULL;

  cur_iter_pos_ = 0;
  cur_iter_rows_ = 0;
  recv_list_rows_ = 0;

  datum_iter_ = NULL;
}
