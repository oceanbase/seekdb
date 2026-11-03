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
#include "sql/das/iter/ob_das_spatial_scan_iter.h"
#include "sql/das/ob_das_scan_op.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

int ObDASSpatialScanIter::inner_init(ObDASIterParam &param)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ObDASScanIter::inner_init(param))) {
  } else {
    ObDASSpatialScanIterParam& scan_param = static_cast<ObDASSpatialScanIterParam&>(param);
    scan_rtdef_ = scan_param.scan_rtdef_;
    scan_ctdef_ = scan_param.scan_ctdef_;
    mbr_filters_ = nullptr;
    is_whole_range_ = false;
    mbr_filter_cnt_ = 0;
  }

  return ret;
}

void ObDASSpatialScanIter::set_scan_param(storage::ObTableScanParam &scan_param) 
{ 
  mbr_filters_ = &scan_param.mbr_filters_;
  is_whole_range_ = false;
  for (int64_t i = 0; i < scan_param.key_ranges_.count(); i++) {
    if (scan_param.key_ranges_.at(i).is_whole_range()) {
      is_whole_range_ = true;
    }
  }
  is_whole_range_ |= (mbr_filters_->count() == 0);

  ObDASScanIter::set_scan_param(scan_param);
}
  

int ObDASSpatialScanIter::inner_get_next_row()
{
  int ret = OB_SUCCESS;

  bool got_row = false;
  do {
    if (OB_FAIL(ObDASScanIter::inner_get_next_row())) {
    } else if (OB_FAIL(filter_by_mbr(got_row))){
    }
  } while (OB_SUCC(ret) && !got_row);
  
  return ret;
}

int ObDASSpatialScanIter::filter_by_mbr(bool &got_row)
{
  got_row = false;
  int ret = OB_SUCCESS;
  if (OB_ISNULL(scan_ctdef_) || OB_ISNULL(scan_rtdef_) ||
      OB_ISNULL(scan_rtdef_->eval_ctx_) || OB_ISNULL(mbr_filters_)) {
    ret = OB_NOT_INIT;
  } else {
    // Storage projects [rowkey..., MBR, optional transaction info]. Filtering
    // only needs MBR; the former rowkey allocation/conversion was unused.
    const int64_t mbr_idx = scan_ctdef_->result_output_.count() -
        (scan_ctdef_->trans_info_expr_ != nullptr ? 2 : 1);
    bool pass_through = true;
    ObObj mbr_obj;
    ObExpr *mbr_expr = nullptr;
    if (mbr_idx < 0 || OB_ISNULL(mbr_expr = scan_ctdef_->result_output_.at(mbr_idx))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(mbr_expr->locate_expr_datum(*scan_rtdef_->eval_ctx_).to_obj(
        mbr_obj, mbr_expr->obj_meta_, mbr_expr->obj_datum_map_))) {
    } else if (!is_whole_range_ && OB_FAIL(filter_by_mbr(mbr_obj, pass_through))) {
    } else if (!is_whole_range_ && pass_through) {
      // not target
      mbr_filter_cnt_++;
    } else {
      got_row = true;
    }
  }
  return ret;
}

int ObDASSpatialScanIter::filter_by_mbr(const ObObj &mbr_obj, bool &pass_through)
{
  pass_through = true;
  int ret = OB_SUCCESS;
  // MBR is stored as binary VARCHAR. is_varchar() deliberately excludes
  // CS_TYPE_BINARY (VARBINARY), so inspect the storage type instead.
  if (mbr_obj.get_type() != ObVarcharType) return OB_INVALID_ARGUMENT;
  ObString mbr_str = mbr_obj.get_varchar();
  bool is_point = (WKB_POINT_DATA_SIZE == mbr_str.length());
  ObSpatialMBR idx_spa_mbr;

  if (OB_FAIL(ObSpatialMBR::from_string(mbr_str, ObDomainOpType::T_INVALID, idx_spa_mbr, is_point))) {
  } else {
    idx_spa_mbr.is_point_ = is_point;
    for (int64_t i = 0; OB_SUCC(ret) && i < mbr_filters_->count() && pass_through; i++) {
      const ObSpatialMBR &spa_mbr = mbr_filters_->at(i);
      idx_spa_mbr.is_geog_ = spa_mbr.is_geog();
      if (OB_FAIL(idx_spa_mbr.filter(spa_mbr, spa_mbr.get_type(), pass_through))) {
      }
    }
  }

  return ret;
}

int ObDASSpatialScanIter::inner_get_next_rows(int64_t &count, int64_t capacity)
{
  count = 0;
  if (capacity <= 0) return OB_INVALID_ARGUMENT;
  if (OB_ISNULL(eval_ctx_) || OB_ISNULL(output_) || max_size_ <= 0) return OB_NOT_INIT;
  capacity = std::min(capacity, max_size_);
  ObEvalCtx::BatchInfoScopeGuard batch_guard(*eval_ctx_);
  int ret = OB_SUCCESS;
  do {
    int64_t fetched = 0;
    ret = ObDASScanIter::inner_get_next_rows(fetched, capacity);
    if (ret != OB_SUCCESS && ret != OB_ITER_END) break;
    const bool end = ret == OB_ITER_END;
    ret = OB_SUCCESS;
    if (fetched < 0 || fetched > capacity || (!end && fetched == 0)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      batch_guard.set_batch_size(fetched);
      for (int64_t i = 0; OB_SUCC(ret) && i < fetched; ++i) {
        bool keep = false;
        batch_guard.set_batch_idx(i);
        if (OB_FAIL(filter_by_mbr(keep))) {
        } else if (keep) {
          // Compact within this storage batch. Payloads remain borrowed until
          // the caller consumes it; never fetch another batch after retaining
          // rows. Copy every output, including optional transaction metadata.
          for (int64_t j = 0; OB_SUCC(ret) && j < output_->count(); ++j) {
            ObExpr *expr = output_->at(j);
            if (OB_ISNULL(expr)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (expr->is_batch_result() && count != i) {
              ObDatum *datums = expr->locate_batch_datums(*eval_ctx_);
              datums[count] = datums[i];
            }
          }
          if (OB_SUCC(ret)) ++count;
        }
      }
      if (OB_SUCC(ret) && end) ret = OB_ITER_END;
    }
  } while (OB_SUCC(ret) && count == 0);
  if (ret != OB_SUCCESS && ret != OB_ITER_END) count = 0;
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
