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

#ifndef DEV_SRC_SQL_ENGINE_EXPR_OB_EXPR_COLL_PRED_H_
#define DEV_SRC_SQL_ENGINE_EXPR_OB_EXPR_COLL_PRED_H_
#include "sql/engine/expr/ob_expr_multiset.h"
#include "lib/container/ob_fast_array.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/engine/expr/ob_i_expr_extra_info.h"

namespace oceanbase
{

namespace pl{
class ObPLCollectoin;
}
namespace sql
{
struct ObExprCollPredInfo : public ObIExprExtraInfo
{
  OB_UNIS_VERSION(1);
public:
  ObExprCollPredInfo(common::ObIAllocator &alloc, ObExprOperatorType type)
      : ObIExprExtraInfo(alloc, type)
  {
  }

  virtual int deep_copy(common::ObIAllocator &allocator,
                        const ObExprOperatorType type,
                        ObIExprExtraInfo *&copied_info) const override;

  template <typename RE>
  int from_raw_expr(RE &expr);
  ObMultiSetType ms_type_;
  ObMultiSetModifier ms_modifier_;
  int64_t tz_offset_;
  ObExprResType result_type_;
};

class ObExprCollPred : public ObExprOperator
{
  OB_UNIS_VERSION(1);
public:
  explicit ObExprCollPred(common::ObIAllocator &alloc);
  virtual ~ObExprCollPred();

  virtual void reset();
  int assign(const ObExprOperator &other);
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const;

  VIRTUAL_TO_STRING_KV(N_EXPR_TYPE, get_type_name(type_),
                       N_EXPR_NAME, name_,
                       N_DIM, row_dimension_,
                       N_REAL_PARAM_NUM, real_param_num_,
                       K_(ms_type),
                       K_(ms_modifier));

  inline void set_ms_type(ObMultiSetType ms_type) { ms_type_ = ms_type; }
  inline ObMultiSetType get_ms_type() const { return ms_type_; }
  inline void set_ms_modifier(ObMultiSetModifier ms_modifier) { ms_modifier_ = ms_modifier; }
  inline ObMultiSetModifier get_ms_modifier() const { return ms_modifier_; }

  static int compare_obj(const ObObj &obj1, const ObObj &obj2, ObCompareCtx &cmp_ctx);

  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_coll_pred(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
private:


  DISALLOW_COPY_AND_ASSIGN(ObExprCollPred);

private:
  ObMultiSetType ms_type_;
  ObMultiSetModifier ms_modifier_;
};

}  // namespace sql
}  // namespace oceanbase
#endif /* DEV_SRC_SQL_ENGINE_EXPR_OB_EXPR_COLL_PRED_H_ */
