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
#include "sql/engine/px/exchange/ob_row_heap.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;




/************************************* ObDatumRowCompare *********************************/
ObDatumRowCompare::ObDatumRowCompare()
  : ret_(OB_SUCCESS), sort_collations_(nullptr), sort_cmp_funs_(nullptr),
    rows_(nullptr), datum_access_ctx_(nullptr), expressions_(nullptr), context_(nullptr)
{
}

int ObDatumRowCompare::init(
    const ObIArray<ObSortFieldCollation> *sort_collations,
    const ObIArray<ObSortCmpFunc> *sort_cmp_funs,
    const common::ObIArray<const ObChunkDatumStore::StoredRow*> &rows,
    const common::ObDatumAccessContext *datum_access_ctx,
    const common::ObIArray<ObExpr *> *expressions, ObEvalCtx *context)
{
  int ret = OB_SUCCESS;
  if (nullptr == sort_collations || nullptr == sort_cmp_funs) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(sort_collations), KP(sort_cmp_funs));
  } else if (sort_collations->count() != sort_cmp_funs->count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("column count miss match", K(ret),
      K(sort_collations->count()), K(sort_cmp_funs->count()));
  } else {
    sort_collations_ = sort_collations;
    sort_cmp_funs_ = sort_cmp_funs;
    rows_ = &rows;
    datum_access_ctx_ = datum_access_ctx;
    expressions_ = expressions;
    context_ = context;
    ret_ = OB_SUCCESS;
  }
  return ret;
}

bool ObDatumRowCompare::operator()(
  int64_t l_idx,
  int64_t r_idx)
{
  bool cmp_ret = false;
  int &ret = ret_;
  const auto *l = rows_ && l_idx >= 0 && l_idx < rows_->count() ? rows_->at(l_idx) : nullptr;
  const auto *r = rows_ && r_idx >= 0 && r_idx < rows_->count() ? rows_->at(r_idx) : nullptr;
  if (OB_UNLIKELY(OB_SUCCESS != ret)) {
    // already fail
  } else if (!is_inited() || OB_ISNULL(l) || OB_ISNULL(r)) {
    ret = !is_inited() ? OB_NOT_INIT : OB_INVALID_ARGUMENT;
  } else {
    const ObDatum *lcells = l->cells();
    const ObDatum *rcells = r->cells();
    int cmp = 0;
    for (int64_t i = 0; OB_SUCC(ret) && 0 == cmp && i < sort_cmp_funs_->count(); i++) {
      const int64_t idx = sort_collations_->at(i).field_idx_;
      if (idx >= l->cnt_ || idx >= r->cnt_) {
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_FAIL(compare_sort_datums(sort_collations_->at(i), sort_cmp_funs_->at(i),
              expressions_, context_, lcells[idx], rcells[idx], cmp, datum_access_ctx_))) {
      } else if (cmp < 0) {
        cmp_ret = !sort_collations_->at(i).is_ascending_;
      } else if (cmp > 0) {
        cmp_ret = sort_collations_->at(i).is_ascending_;
      }
    }
  }
  return cmp_ret;
}

/************************************* ObMaxDatumRowCompare *********************************/
ObMaxDatumRowCompare::ObMaxDatumRowCompare()
  : ret_(OB_SUCCESS), sort_collations_(nullptr), sort_cmp_funs_(nullptr),
    rows_(nullptr), datum_access_ctx_(nullptr), expressions_(nullptr), context_(nullptr)
{
}

int ObMaxDatumRowCompare::init(
    const ObIArray<ObSortFieldCollation> *sort_collations,
    const ObIArray<ObSortCmpFunc> *sort_cmp_funs,
    const common::ObIArray<const ObChunkDatumStore::LastStoredRow*> &rows,
    const common::ObDatumAccessContext *datum_access_ctx,
    const common::ObIArray<ObExpr *> *expressions, ObEvalCtx *context)
{
  int ret = OB_SUCCESS;
  if (nullptr == sort_collations || nullptr == sort_cmp_funs) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(sort_collations), KP(sort_cmp_funs));
  } else if (sort_collations->count() != sort_cmp_funs->count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("column count miss match", K(ret),
      K(sort_collations->count()), K(sort_cmp_funs->count()));
  } else {
    sort_collations_ = sort_collations;
    sort_cmp_funs_ = sort_cmp_funs;
    rows_ = &rows;
    datum_access_ctx_ = datum_access_ctx;
    expressions_ = expressions;
    context_ = context;
    ret_ = OB_SUCCESS;
  }
  return ret;
}

bool ObMaxDatumRowCompare::operator()(
  int64_t l_idx,
  int64_t r_idx)
{
  bool cmp_ret = false;
  int &ret = ret_;
  const auto *left = rows_ && l_idx >= 0 && l_idx < rows_->count() ? rows_->at(l_idx) : nullptr;
  const auto *right = rows_ && r_idx >= 0 && r_idx < rows_->count() ? rows_->at(r_idx) : nullptr;
  const auto *l = left ? left->store_row_ : nullptr;
  const auto *r = right ? right->store_row_ : nullptr;
  if (OB_UNLIKELY(OB_SUCCESS != ret)) {
    // already fail
  } else if (!is_inited() || OB_ISNULL(l) || OB_ISNULL(r)) {
    ret = !is_inited() ? OB_NOT_INIT : OB_INVALID_ARGUMENT;
  } else {
    const ObDatum *lcells = l->cells();
    const ObDatum *rcells = r->cells();
    int cmp = 0;
    for (int64_t i = 0; OB_SUCC(ret) && 0 == cmp && i < sort_cmp_funs_->count(); i++) {
      const int64_t idx = sort_collations_->at(i).field_idx_;
      if (idx >= l->cnt_ || idx >= r->cnt_) {
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_FAIL(compare_sort_datums(sort_collations_->at(i), sort_cmp_funs_->at(i),
              expressions_, context_, lcells[idx], rcells[idx], cmp, datum_access_ctx_))) {
      } else if (cmp < 0) {
        cmp_ret = !sort_collations_->at(i).is_ascending_;
      } else if (cmp > 0) {
        cmp_ret = sort_collations_->at(i).is_ascending_;
      }
    }
  }
  return cmp_ret;
}
