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

#include "ob_hash_partitioning_infrastructure_op.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;


int ObHashPartCols::equal_distinct(
  const common::ObIArray<ObExpr*> *exprs,
  const ObHashPartCols &other,
  const ObIArray<ObSortFieldCollation> *sort_collations,
  const ObIArray<ObCmpFunc> *cmp_funcs,
  ObEvalCtx *eval_ctx,
  bool &result, ObEvalCtx::BatchInfoScopeGuard &batch_info_guard) const
{
  UNUSED(other);
  int ret = OB_SUCCESS;
  result = true;
  int cmp_result = 0;
  const common::ObDatumAccessContext *datum_access_ctx = nullptr;
  if (OB_ISNULL(sort_collations) || OB_ISNULL(cmp_funcs)
      || OB_ISNULL(eval_ctx) || OB_ISNULL(exprs)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(eval_ctx->get_datum_access_ctx(datum_access_ctx))) {
  } else if (use_expr_) {
    //for this situation, must be crash in a batch, need to get datum from expr
    Datum *l_cell = nullptr;
    Datum *r_cell = nullptr;
    const int64_t right_batch_idx = eval_ctx->get_batch_idx();
    const int64_t left_batch_idx = batch_idx_;
    for (int64_t i = 0; OB_SUCC(ret) && i < sort_collations->count() && 0 == cmp_result; ++i) {
      int64_t idx = sort_collations->at(i).field_idx_;
      batch_info_guard.set_batch_idx(left_batch_idx);
      //be careful left && right exprs are evaled in calc_hash_values
      l_cell = &exprs->at(idx)->locate_expr_datum(*eval_ctx);
      batch_info_guard.set_batch_idx(right_batch_idx);
      r_cell = &exprs->at(idx)->locate_expr_datum(*eval_ctx);
      alignas(8) char left_scratch[64], right_scratch[64];
      ObDatum left, right;
      const common::DatumCopyPlan plan = exprs->at(idx)->get_datum_copy_plan();
      if (OB_FAIL(plan.legacy_view(*l_cell, left, left_scratch, sizeof(left_scratch)))) {
      } else if (OB_FAIL(plan.legacy_view(*r_cell, right, right_scratch, sizeof(right_scratch)))) {
      } else if (OB_FAIL(cmp_funcs->at(i).cmp_func_(left, right, cmp_result, datum_access_ctx))) {
      }
    }
    //reset batch_idx before return 
    batch_info_guard.set_batch_idx(right_batch_idx);
    result = (0 == cmp_result);
  } else {
    const StoredDatumCell *l_cells = store_row_->stored_cells();
    Datum *r_cell = nullptr;
    // must evaled in calc_hash_values
    for (int64_t i = 0; OB_SUCC(ret) && i < sort_collations->count() && 0 == cmp_result; ++i) {
      int64_t idx = sort_collations->at(i).field_idx_;
      const ObDatum l_cell = StoredDatumCodec::decode_legacy_view(l_cells[idx]);
      r_cell = &exprs->at(idx)->locate_expr_datum(*eval_ctx);
      alignas(8) char scratch[64];
      ObDatum right;
      if (OB_FAIL(exprs->at(idx)->get_datum_copy_plan().legacy_view(
          *r_cell, right, scratch, sizeof(scratch)))) {
      } else if (OB_FAIL(cmp_funcs->at(i).cmp_func_(l_cell, right, cmp_result, datum_access_ctx))) {
      }
    }
    result = (0 == cmp_result);
  }
  return ret;
}
