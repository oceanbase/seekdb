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

#ifndef OCEANBASE_SQL_ENGINE_SORT_SORT_VEC_OP_CONTEXT_H_
#define OCEANBASE_SQL_ENGINE_SORT_SORT_VEC_OP_CONTEXT_H_

#include "lib/container/ob_array.h"
#include "sql/engine/sort/ob_sort_basic_info.h"

namespace oceanbase {
namespace sql {
struct ObPushDownTopNFilterInfo;
struct ObSortVecOpContext
{
  ObSortVecOpContext() :
    tenant_id_(UINT64_MAX), sk_exprs_(nullptr), addon_exprs_(nullptr), sk_collations_(nullptr),
    base_sk_collations_(nullptr), addon_collations_(nullptr), eval_ctx_(nullptr),
    exec_ctx_(nullptr), op_(nullptr), prefix_pos_(0), part_cnt_(0), topn_cnt_(INT64_MAX),
    sort_row_cnt_(nullptr), flag_(0), compress_type_(NONE_COMPRESSOR)
  {}
  TO_STRING_KV(K_(tenant_id), KP_(sk_exprs), KP_(addon_exprs), KP_(sk_collations),
               KP_(base_sk_collations), KP_(addon_collations), K_(prefix_pos), K_(part_cnt),
               K_(topn_cnt), KP_(sort_row_cnt), K_(flag), K_(compress_type));

  uint64_t tenant_id_;
  const ObIArray<ObExpr *> *sk_exprs_;
  const ObIArray<ObExpr *> *addon_exprs_;
  const ObIArray<ObSortFieldCollation> *sk_collations_;
  const ObIArray<ObSortFieldCollation> *base_sk_collations_;
  const ObIArray<ObSortFieldCollation> *addon_collations_;
  ObEvalCtx *eval_ctx_;
  ObExecContext *exec_ctx_;
  ObOperator *op_;
  int64_t prefix_pos_;
  int64_t part_cnt_;
  int64_t topn_cnt_;
  int64_t *sort_row_cnt_;
  union
  {
    struct //FARM COMPAT WHITELIST
    {
      uint32_t enable_encode_sortkey_ : 1;
      uint32_t in_local_order_ : 1;
      uint32_t need_rewind_ : 1;
      uint32_t is_fetch_with_ties_ : 1;
      uint32_t has_addon_ : 1;
      uint32_t enable_pd_topn_filter_ : 1;
      uint32_t enable_single_col_compare_ : 1;
      uint32_t reserved_ : 25;
    };
    uint32_t flag_;
  };
  ObCompressorType compress_type_;
  const ObPushDownTopNFilterInfo *pd_topn_filter_info_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_SORT_VEC_OP_CONTEXT_H_ */
