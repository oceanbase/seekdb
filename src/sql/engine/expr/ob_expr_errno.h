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

#ifndef _OCEANBASE_SQL_OB_EXPR_ERRNO_H_
#define _OCEANBASE_SQL_OB_EXPR_ERRNO_H_
#include "lib/ob_name_def.h"
#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprErrno : public ObFuncExprOperator
{
public:
  explicit ObExprErrno(common::ObIAllocator &alloc);
  virtual ~ObExprErrno();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_array,
                                int64_t param_num,
                                ObExprTypeCtx &type_ctx) const;
  static int eval_errno(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  static int conv2errno(int64_t value);
  static int get_value(const common::number::ObNumber &nmb, int64_t &value);
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprErrno);
};

} //sql
} //oceanbase
#endif //_OCEANBASE_SQL_OB_EXPR_ERRNO_H_
