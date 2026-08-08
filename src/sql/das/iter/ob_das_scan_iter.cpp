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
#include "query/das/ob_das_iter_access.h"
#include "sql/das/iter/ob_das_scan_iter.h"
#include "share/rc/ob_server_runtime.h"
#include "data_plane/access/ob_tablet_scan.h"
#include "src/sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

int ObDASScanIter::inner_init(ObDASIterParam &param)
{
  int ret = OB_SUCCESS;
  if (param.type_ != ObDASIterType::DAS_ITER_SCAN) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("inner init das iter with bad param type", K(param), K(ret));
  } else {
    const ObDASScanCtDef *scan_ctdef = (static_cast<ObDASScanIterParam&>(param)).scan_ctdef_;
    output_ = &scan_ctdef->result_output_;
    tsc_service_ = is_virtual_table(scan_ctdef->ref_table_id_)
                       ? share::server_service<common::ObIVirtualTableScan>()
                       : share::server_service<common::ObITabletScan>();
    if (OB_ISNULL(tsc_service_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("tablet scan service is not bound", K(ret), K(scan_ctdef->ref_table_id_));
    }
  }

  return ret;
}

int ObDASScanIter::inner_reuse()
{
  int ret = OB_SUCCESS;
  // NOTE: need_switch_param_ should have been set before call reuse().
  if (OB_ISNULL(scan_param_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr scan param", K(ret));
  } else if (OB_FAIL(tsc_service_->reuse_scan_iter(scan_param_->need_switch_param_, result_))) {
  } else {
    scan_param_->key_ranges_.reuse();
    scan_param_->mbr_filters_.reuse();
  }
  return ret;
}

int ObDASScanIter::inner_release()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(result_)) {
    if (OB_FAIL(tsc_service_->revert_scan_iter(result_))) {
    }
    result_ = nullptr;
  }
  return ret;
}

int ObDASScanIter::do_table_scan()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(scan_param_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr scan param", K(ret));
  } else if (OB_UNLIKELY(nullptr != result_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected not null result iter ptr before do table scan", K(ret), KP_(result));
  } else if (OB_FAIL(tsc_service_->table_scan(*scan_param_, result_))) {
    if (OB_SNAPSHOT_DISCARDED == ret && scan_param_->fb_snapshot_.is_valid()) {
      ret = OB_INVALID_QUERY_TIMESTAMP;
    } else if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
      LOG_WARN("fail to scan table", KPC_(scan_param), K(ret));
    }
  }

  return ret;
}

int ObDASScanIter::rescan()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(scan_param_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr scan param", K(ret));
  } else if (OB_FAIL(tsc_service_->table_rescan(*scan_param_, result_))) {
      if (OB_SNAPSHOT_DISCARDED == ret && scan_param_->fb_snapshot_.is_valid()) {
        ret = OB_INVALID_QUERY_TIMESTAMP;
      }
    LOG_WARN("failed to rescan tablet", K(scan_param_->tablet_id_), K(ret));
  } else {
    // reset need_switch_param_ after real rescan.
    scan_param_->need_switch_param_ = false;
  }

  return ret;
}

int ObDASScanIter::advance_scan()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(scan_param_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr scan param", K(ret));
  } else if (OB_FAIL(tsc_service_->table_advance_scan(*scan_param_, result_))) {
  }
  return ret;
}

int ObDASScanIter::inner_get_next_row()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(result_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr scan iter", K(ret));
  } else if (OB_FAIL(result_->get_next_row())) {
    if (ret != OB_ITER_END) {
      LOG_WARN("failed to get next row", K(ret));
    }
  }
  return ret;
}

int ObDASScanIter::inner_get_next_rows(int64_t &count, int64_t capacity)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(result_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr scan iter", K(ret));
  } else if (OB_FAIL(result_->get_next_rows(count, capacity))) {
    if (ret != OB_ITER_END) {
      LOG_WARN("failed to get next row", K(ret));
    }
  }
  const ObBitVector *skip = nullptr;
  PRINT_VECTORIZED_ROWS(SQL, DEBUG, *eval_ctx_, *output_, count, skip);
  return ret;
}

void ObDASScanIter::clear_evaluated_flag()
{
  OB_ASSERT(nullptr != scan_param_);
  if (OB_NOT_NULL(scan_param_->op_)) {
    scan_param_->op_->clear_evaluated_flag();
  }
}

int ObDASScanIter::set_scan_rowkey(ObEvalCtx *eval_ctx,
                                   const ObIArray<ObExpr *> &rowkey_exprs,
                                   const ObDASScanCtDef *lookup_ctdef,
                                   ObIAllocator *alloc,
                                   int64_t group_id)
{
  int ret = OB_SUCCESS;
  ObNewRange range;
  if (OB_ISNULL(eval_ctx) || OB_UNLIKELY(rowkey_exprs.empty()) || OB_ISNULL(lookup_ctdef) || OB_ISNULL(alloc)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid eval ctx, rowkey exprs, lookup ctdef, or allocator",
             K(eval_ctx), K(rowkey_exprs), K(lookup_ctdef), K(alloc), K(ret));
  } else {
    ObObj *obj_ptr = nullptr;
    void *buf = nullptr;
    int64_t rowkey_cnt = rowkey_exprs.count();
    if (OB_ISNULL(buf = alloc->alloc(sizeof(ObObj) * rowkey_cnt))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate enough memory", K(rowkey_cnt), K(ret));
    } else {
      obj_ptr = new (buf) ObObj(rowkey_cnt);
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_cnt; i++) {
      ObObj tmp_obj;
      const ObExpr *expr = rowkey_exprs.at(i);
      ObDatum &col_datum = expr->locate_expr_datum(*eval_ctx);
      if (OB_UNLIKELY(T_PSEUDO_GROUP_ID == expr->type_ || T_PSEUDO_ROW_TRANS_INFO_COLUMN == expr->type_)) {
        // skip.
      } else if (OB_FAIL(col_datum.to_obj(tmp_obj, expr->obj_meta_, expr->obj_datum_map_))) {
      } else if (OB_FAIL(ob_write_obj(*alloc, tmp_obj, obj_ptr[i]))) {
      }
    }

    if (OB_SUCC(ret)) {
      ObRowkey row_key(obj_ptr, rowkey_cnt);
      if (OB_FAIL(range.build_range(lookup_ctdef->ref_table_id_, row_key))) {
      } else if (FALSE_IT(range.group_idx_ = ObNewRange::get_group_idx(group_id))) {
      } else if (OB_FAIL(scan_param_->key_ranges_.push_back(range))) {
      } else {
        scan_param_->is_get_ = true;
      }
    }
  }

  return ret;
}

}  // namespace sql

namespace query
{

int das_scan_next_row(sql::ObDASScanIter *iterator)
{
  return OB_ISNULL(iterator) ? common::OB_INVALID_ARGUMENT
                             : iterator->get_next_row();
}

int das_scan_next_rows(
    sql::ObDASScanIter *iterator, int64_t &count, const int64_t capacity)
{
  return OB_ISNULL(iterator) ? common::OB_INVALID_ARGUMENT
                             : iterator->get_next_rows(count, capacity);
}

int das_scan_reuse(sql::ObDASScanIter *iterator)
{
  return OB_ISNULL(iterator) ? common::OB_INVALID_ARGUMENT : iterator->reuse();
}

int das_scan_rescan(sql::ObDASScanIter *iterator)
{
  return OB_ISNULL(iterator) ? common::OB_INVALID_ARGUMENT : iterator->rescan();
}

int das_scan_advance(sql::ObDASScanIter *iterator)
{
  return OB_ISNULL(iterator) ? common::OB_INVALID_ARGUMENT
                             : iterator->advance_scan();
}

void das_scan_reset(sql::ObDASScanIter *iterator)
{
  if (OB_NOT_NULL(iterator)) {
    iterator->reset();
  }
}

void das_scan_set_param(
    sql::ObDASScanIter *iterator, storage::ObTableScanParam &scan_param)
{
  if (OB_NOT_NULL(iterator)) {
    iterator->set_scan_param(scan_param);
  }
}

} // namespace query
}  // namespace oceanbase
