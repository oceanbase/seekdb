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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_FIND_IN_SET_H_
#define OCEANBASE_SQL_ENGINE_EXPR_FIND_IN_SET_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprFindInSet: public ObFuncExprOperator {
public:
	explicit ObExprFindInSet(common::ObIAllocator &alloc);
	virtual ~ObExprFindInSet();
	virtual int calc_result_type2(ObExprResType &type,
																ObExprResType &type1,
																ObExprResType &type2,
																common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const;
  virtual bool need_rt_ctx() const override
  {
    return true;
  }
  static int calc_find_in_set_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum);
  static int calc_find_in_set_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  DECLARE_SET_LOCAL_SESSION_VARS;

private:
  template <typename Arg0Vec, typename Arg1Vec, typename ResVec>
  static int calc_find_in_set_vector_dispatch(VECTOR_EVAL_FUNC_ARG_DECL);

  template <typename Arg0Vec, typename ResVec>
  static int calc_find_in_set_vector_dispatch(VECTOR_EVAL_FUNC_ARG_DECL);
  DISALLOW_COPY_AND_ASSIGN(ObExprFindInSet);
};

} /* namespace sql */
} /* namespace oceanbase */

#endif /* OCEANBASE_SQL_ENGINE_EXPR_FIND_IN_SET_H_ */
