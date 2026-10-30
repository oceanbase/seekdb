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
#include "sql/das/iter/ob_das_mvi_lookup_iter.h"
#include "sql/das/iter/ob_das_lookup_iter.h"
#include "sql/das/iter/ob_das_scan_iter.h"
#include "sql/das/ob_das_scan_op.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

int ObDASMVILookupIter::inner_get_next_row()
{
  int ret = OB_SUCCESS;

  bool got_next_row = false;
  do {
    switch (state_) {
      case INDEX_SCAN: {
        index_table_iter_->clear_evaluated_flag();
        if (OB_SUCC(index_table_iter_->get_next_row())) {
          bool has_rowkey = check_has_rowkey();
          if (has_rowkey) {
            lookup_rowkey_cnt_++;
            state_ = LookupState::OUTPUT_ROWS;
          } else {
            state_ = DO_LOOKUP;
          }
        }

        if (OB_ITER_END == ret) {
          state_ = FINISHED;
        }
        break;
      }
      case DO_LOOKUP: {
        if (OB_FAIL(do_index_lookup())) {
        } else {
          state_ = LookupState::OUTPUT_ROWS;
        }
        break;
      }
      case LookupState::OUTPUT_ROWS: {
        if (lookup_rowkey_cnt_ != 0) {
          lookup_rowkey_cnt_ = 0;
        } else {
          data_table_iter_->clear_evaluated_flag();
          if (OB_FAIL(data_table_iter_->get_next_row())) {
          }
        }

        got_next_row = true;
        state_ = LookupState::INDEX_SCAN;
        break;
      }
      case FINISHED: {
        ret = OB_ITER_END;
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        break;
      }
    }
  } while (OB_SUCC(ret) && !got_next_row);

  return ret;
}

int ObDASMVILookupIter::save_rowkey()
{
  int ret = OB_SUCCESS;
  const ObDASScanCtDef *index_table_ctdef = static_cast<const ObDASScanCtDef*>(index_ctdef_);
  if (OB_ISNULL(index_table_ctdef) || OB_ISNULL(lookup_ctdef_) || OB_ISNULL(lookup_rtdef_)
      || OB_ISNULL(lookup_rtdef_->eval_ctx_) || OB_ISNULL(data_table_iter_)
      || data_table_iter_->get_type() != DAS_ITER_SCAN) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const int64_t rowkey_cnt = index_table_ctdef->rowkey_exprs_.count();
    ObExpr *doc_id_expr = nullptr;
    if (OB_UNLIKELY(rowkey_cnt >= index_table_ctdef->result_output_.count())
        || OB_ISNULL(doc_id_expr = index_table_ctdef->result_output_.at(rowkey_cnt))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const ObDatum &doc_id_datum = doc_id_expr->locate_expr_datum(*lookup_rtdef_->eval_ctx_);
      // DOC_ID() and its hidden column are binary VARCHAR(16). This lookup
      // always targets the real doc-id auxiliary table. Keep that invariant
      // explicit so the generic scan helper cannot select its old-value path.
      if (OB_UNLIKELY(doc_id_datum.is_null()
          || doc_id_expr->obj_meta_.get_type() != ObVarcharType
          || doc_id_expr->obj_meta_.get_cs_type() != CS_TYPE_BINARY
          || doc_id_expr->obj_datum_map_ != OBJ_DATUM_STRING
          || doc_id_datum.len_ != OB_DOC_ID_COLUMN_BYTE_LENGTH
          || is_virtual_table(lookup_ctdef_->ref_table_id_))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObArenaAllocator &allocator = get_arena_allocator();
        ObSEArray<ObExpr *, 1> doc_id_exprs;
        if (OB_FAIL(doc_id_exprs.push_back(doc_id_expr))) {
        } else {
          ObDASScanIter *scan_iter = static_cast<ObDASScanIter *>(data_table_iter_);
          // The lookup arena survives until the data scan consumes its keys.
          // The helper owns each published payload there, independently of
          // the index expression frame, with the same default group 0.
          ret = scan_iter->set_scan_datum_rowkey(lookup_rtdef_->eval_ctx_, doc_id_exprs,
                                                lookup_ctdef_, &allocator, 0);
        }
      }
    }
  }
  return ret;
}

int ObDASMVILookupIter::inner_get_next_rows(int64_t &count, int64_t capacity)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDASMVILookupIter::inner_get_next_row())) {
  } else {
    count = 1;
  }
  return ret;
}

int ObDASMVILookupIter::do_index_lookup()
{
  int ret = OB_SUCCESS;

  if (OB_NOT_NULL(data_table_iter_) && OB_FAIL(data_table_iter_->reuse())) {
    LOG_WARN("failed to reuse data table iter");
  } else if (OB_FAIL(save_rowkey())) {
  } else if (OB_FAIL(ObDASLocalLookupIter::do_index_lookup())) {
  }

  return ret;
}

bool ObDASMVILookupIter::check_has_rowkey()
{
  const ObDASScanCtDef *index_table_ctdef = static_cast<const ObDASScanCtDef*>(index_ctdef_);
  const ObDASScanCtDef *data_table_ctdef = static_cast<const ObDASScanCtDef*>(lookup_ctdef_);
  int64_t rowkey_col_cnt = index_table_ctdef->rowkey_exprs_.count();
  int64_t rowkey_null_col_cnt = 0;
  
  for (int64_t i = 0; i < rowkey_col_cnt; ++i) {
    ObExpr *expr = index_table_ctdef->result_output_.at(i);
    if (T_PSEUDO_GROUP_ID == expr->type_) {
      // do nothing
    } else {
      ObDatum &datum = expr->locate_expr_datum(*lookup_rtdef_->eval_ctx_);
      if (datum.is_null()) {
        rowkey_null_col_cnt++;
      }
    }
  }

  return rowkey_null_col_cnt != rowkey_col_cnt;
}


}  // namespace sql
}  // namespace oceanbase
