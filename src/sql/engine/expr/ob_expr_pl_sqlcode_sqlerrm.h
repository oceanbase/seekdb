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

#ifndef SRC_SQL_ENGINE_EXPR_OB_EXPR_PL_SQLCODE_SQL_ERRM_H_
#define SRC_SQL_ENGINE_EXPR_OB_EXPR_PL_SQLCODE_SQL_ERRM_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprPLSQLCodeSQLErrm : public ObFuncExprOperator
{
  OB_UNIS_VERSION(1);
public:
  explicit ObExprPLSQLCodeSQLErrm(common::ObIAllocator &alloc);
  virtual ~ObExprPLSQLCodeSQLErrm();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;

  virtual int assign(const ObExprOperator &other);

  void set_is_sqlcode(bool is_sqlcode) { is_sqlcode_ = is_sqlcode; }
  int cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const;
  static int eval_pl_sql_code_errm(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;

private:
  bool is_sqlcode_; // TRUE represents getting SQLCODE, FALSE represents getting SQLERRM
  DISALLOW_COPY_AND_ASSIGN(ObExprPLSQLCodeSQLErrm);
};

}
}

#endif /* SRC_SQL_ENGINE_EXPR_OB_EXPR_PL_SQLCODE_SQL_ERRM_H_ */
