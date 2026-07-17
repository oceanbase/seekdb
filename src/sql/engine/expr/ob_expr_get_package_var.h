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

#ifndef _OB_EXPR_GET_PACKAGE_VAR_H
#define _OB_EXPR_GET_PACKAGE_VAR_H 1

namespace oceanbase
{
}

#endif /* _OB_EXPR_GET_PACKAGE_VAR_H */

// Copyright 2014-2017 Alibaba Inc. All Rights Reserved.
// Author:
#ifndef SRC_SQL_ENGINE_EXPR_OB_EXPR_GET_PACKAGE_VAR_H_
#define SRC_SQL_ENGINE_EXPR_OB_EXPR_GET_PACKAGE_VAR_H_
#include "lib/ob_name_def.h"
#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{
class ObExprGetPackageVar : public ObFuncExprOperator
{
public:
  explicit ObExprGetPackageVar(common::ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_OP_GET_PACKAGE_VAR, N_GET_PACKAGE_VAR, PARAM_NUM_UNKNOWN, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, INTERNAL_IN_MYSQL_MODE) {}

  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_stack,
                                int64_t param_num,
                                ObExprTypeCtx &type_ctx) const;

  static int eval_get_package_var(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  static int calc(common::ObObj &result,
                  uint64_t package_id,
                  int64_t spec_version,
                  int64_t body_version,
                  int64_t var_idx,
                  ObExecContext *exec_ctx,
                  ObSQLSessionInfo *session_info);
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprGetPackageVar);
};
} //end namespace sql
} //end namespace oceanbase
#endif /* SRC_SQL_ENGINE_EXPR_OB_EXPR_GET_PACKAGE_VAR_H_ */
