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

#ifndef OCEANBASE_SHARE_VECTOR_OB_VECTOR_BASE_H_
#define OCEANBASE_SHARE_VECTOR_OB_VECTOR_BASE_H_

#include "share/vector/ob_i_vector.h"

namespace oceanbase
{
namespace sql
{
  class ObExpr;
  class ObEvalCtx;
}
namespace common
{

class ObVectorBase : public ObIVector
{
public:
  ObVectorBase() : max_row_cnt_(INT32_MAX), flags_(0), expr_(nullptr), eval_ctx_(nullptr) {}

  // TODO: check calling
  void set_max_row_cnt(int32_t max_row_cnt) { max_row_cnt_ = max_row_cnt; }
  int32_t get_max_row_cnt() const { return max_row_cnt_; }

  void set_expr_and_ctx(sql::ObExpr *expr, sql::ObEvalCtx *ctx)
  {
    expr_ = expr;
    eval_ctx_ = ctx;
  }

  OB_INLINE sql::ObExpr *get_expr() { return expr_; }
  OB_INLINE sql::ObEvalCtx *get_eval_ctx() { return eval_ctx_; }

  virtual bool is_collection_expr() const final { return is_collection_expr_; }
  virtual void set_has_compact_collection() final { collection_all_vector_fmt_ = false; }
  virtual bool has_compact_collection() const final { return !collection_all_vector_fmt_; }
  virtual void unset_has_compact_collection() final { collection_all_vector_fmt_ = true; }
protected:
friend class sql::ObExpr;
  int32_t max_row_cnt_;
  union {
    struct {
      uint32_t is_collection_expr_: 1;
      uint32_t collection_all_vector_fmt_: 1;
      uint32_t reserved_: 30;
    };
    uint32_t flags_;
  };
  sql::ObExpr *expr_;
  sql::ObEvalCtx *eval_ctx_;
};

}
}
#endif // OCEANBASE_SHARE_VECTOR_OB_VECTOR_BASE_H_
