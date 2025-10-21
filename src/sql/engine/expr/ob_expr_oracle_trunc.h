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

#ifndef _OCEANBASE_SQL_OB_EXPR_ORACLE_TRUNC_H_
#define _OCEANBASE_SQL_OB_EXPR_ORACLE_TRUNC_H_
#include "lib/ob_name_def.h"
#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprOracleTrunc : public ObFuncExprOperator
{
public:
  explicit ObExprOracleTrunc(common::ObIAllocator &alloc);
  explicit ObExprOracleTrunc(common::ObIAllocator &alloc, const char *name);
  virtual ~ObExprOracleTrunc() {}
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *params,
                                int64_t params_count,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const;
  virtual bool need_rt_ctx() const override { return true; }
protected:
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprOracleTrunc);
};

} //sql
} //oceanbase
#endif //_OCEANBASE_SQL_OB_EXPR_ORACLE_TRUNC_H_
