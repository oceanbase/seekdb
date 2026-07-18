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

#ifndef OCEANBASE_EXPR_TO_TEMPORAL_BASE_H
#define OCEANBASE_EXPR_TO_TEMPORAL_BASE_H

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprToTemporalBase: public ObFuncExprOperator
{
public:
  explicit ObExprToTemporalBase(common::ObIAllocator &alloc,
                                 ObExprOperatorType type,
                                 const char *name);
  virtual ~ObExprToTemporalBase() {}
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_array,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int set_my_result_from_ob_time(common::ObExprCtx &expr_ctx,
                                         common::ObTime &ob_time,
                                         common::ObObj &result) const = 0;
  virtual common::ObObjType get_my_target_obj_type() const = 0;

  //engine3.0
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  virtual bool need_rt_ctx() const override { return true; }
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprToTemporalBase);
};


}
}

#endif // OCEANBASE_EXPR_TO_TEMPORAL_BASE_H
