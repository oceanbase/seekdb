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

#ifndef OCEANBASE_SRC_SQL_ENGINE_EXPR_OB_EXPR_SUBQUERY_H_
#define OCEANBASE_SRC_SQL_ENGINE_EXPR_OB_EXPR_SUBQUERY_H_

#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_i_expr_extra_info.h"

namespace oceanbase
{
namespace sql
{

class ObSubQueryIterator;
class ObExprSubQueryRef : public ObExprOperator
{
  OB_UNIS_VERSION(1);
public:
  struct Extra : public ObExprExtraInfoAccess<Extra>
  {
    const static uint32_t DEF_OP_ID = std::numeric_limits<uint32_t>::max();
    Extra() : op_id_(DEF_OP_ID), iter_idx_(OB_INVALID_INDEX), is_scalar_(0) {}
    inline void reset() { op_id_ = DEF_OP_ID; iter_idx_ = OB_INVALID_INDEX; is_scalar_ = 0; }
    int assign(const Extra &other);

    uint32_t op_id_;
    int16_t iter_idx_;
    uint16_t is_scalar_;
    TO_STRING_KV(K(op_id_), K(iter_idx_), K(is_scalar_));
    bool is_valid() const { return DEF_OP_ID != op_id_; }
  } __attribute__((packed));

  static_assert(sizeof(Extra) <= sizeof(uint64_t), "too big extra info");

  struct ExtraInfo : public ObIExprExtraInfo
  {
    OB_UNIS_VERSION(1);
  public:
    ExtraInfo(common::ObIAllocator &alloc, ObExprOperatorType type)
      : ObIExprExtraInfo(alloc, type),
        scalar_result_type_() {}
    virtual ~ExtraInfo() = default;
    void reset();
    int assign(const ExtraInfo &other);
    static int init_extra_info(common::ObIAllocator *allocator,
                               const ObQueryRefRawExpr &expr,
                               const ObExprOperatorType type,
                               ObExpr &rt_expr);
    virtual int deep_copy(common::ObIAllocator &allocator,
                          const ObExprOperatorType type,
                          ObIExprExtraInfo *&copied_info) const override;
    TO_STRING_KV(K_(scalar_result_type));

    ObExprResType scalar_result_type_;
  };
public:
  explicit  ObExprSubQueryRef(common::ObIAllocator &alloc);
  virtual ~ObExprSubQueryRef();

  virtual int assign(const ObExprOperator &other);

  virtual void reset();
  virtual int calc_result_type0(ObExprResType &type, common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int expr_eval(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  static int get_subquery_iter(ObEvalCtx &ctx,
                               const Extra &extra,
                               ObSubQueryIterator *&iter);
  // for expr benchmark, when clear subquery flag, must rescan onetime expr
  static int reset_onetime_expr(const ObExpr &expr, ObEvalCtx &ctx);
  void set_result_is_scalar(bool is_scalar) { extra_.is_scalar_ = is_scalar; }
  void set_scalar_result_type(const ObExprResType &result_type);
  void set_subquery_idx(int64_t subquery_idx) { extra_.iter_idx_ = subquery_idx; }

  VIRTUAL_TO_STRING_KV(N_EXPR_TYPE, get_type_name(type_),
                       N_EXPR_NAME, name_,
                       N_PARAM_NUM, param_num_,
                       N_DIM, row_dimension_,
                       N_REAL_PARAM_NUM, real_param_num_,
                       K_(extra),
                       K_(extra_info));
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprSubQueryRef);
private:
  Extra extra_;
  ExtraInfo extra_info_;
};
}  // namespace sql
}  // namespace oceanbase
#endif // OCEANBASE_SRC_SQL_ENGINE_EXPR_OB_EXPR_SUBQUERY_H_
