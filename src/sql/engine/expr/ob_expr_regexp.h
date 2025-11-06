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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_REGEXP_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_REGEXP_

#include "lib/string/ob_string.h"
#include "sql/engine/expr/ob_expr_regexp_context.h"

namespace oceanbase
{
namespace sql
{
class ObExprRegexp : public ObFuncExprOperator
{
  OB_UNIS_VERSION_V(1);
public:
  explicit  ObExprRegexp(common::ObIAllocator &alloc);
  virtual ~ObExprRegexp();

  virtual int assign(const ObExprOperator &other);

  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual inline void reset()
  {
    regexp_idx_ = common::OB_COMPACT_INVALID_INDEX;
    pattern_is_const_ = false;
    value_is_const_ = false;
    ObFuncExprOperator::reset();
  }

  inline int16_t get_regexp_idx() const { return regexp_idx_; }
  inline void set_regexp_idx(int16_t regexp_idx) { regexp_idx_ = regexp_idx; }
  inline void set_pattern_is_const(bool pattern_is_const) { pattern_is_const_ = pattern_is_const; }
  inline void set_value_is_const(bool value_is_const) { value_is_const_ = value_is_const; }

  virtual bool need_rt_ctx() const override { return true; }
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int eval_regexp(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  static int eval_hs_regexp(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
private:
  template<typename RegExpCtx>
  static int regexp_match(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
private:
  int16_t regexp_idx_; // idx of posix_regexp_list_ in plan ctx, for regexp operator
  bool pattern_is_const_;
  bool value_is_const_;
private:
  // disallow copy
  ObExprRegexp(const ObExprRegexp &other);
  ObExprRegexp &operator=(const ObExprRegexp &ohter);
};
}
}
#endif /* OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_REGEXP_ */
