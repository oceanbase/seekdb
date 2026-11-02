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

#include "sql/engine/expr/ob_expr_mysql_port.h"
#include "observer/ob_server_struct.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{


ObExprMySQLPort::ObExprMySQLPort(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_MYSQL_PORT, N_MYSQL_PORT, 0, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprMySQLPort::~ObExprMySQLPort()
{
}

int ObExprMySQLPort::calc_result_type0(ObExprResType &type, ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  type.set_int();
  type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_);
  type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].precision_);
  return ret;
}

int ObExprMySQLPort::eval_mysql_port(const ObExpr &expr, ObEvalCtx &ctx,
    ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  UNUSED(expr);
  UNUSED(ctx);
  common::ObServerConfig *config = GCTX.config_;
  if (OB_ISNULL(config)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(WARN, "server config is null, get mysql port failed", K(ret));
  } else {
    expr_datum.set_int(config->mysql_port);
  }
  return ret;
}

int ObExprMySQLPort::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr,
    ObExpr &rt_expr) const
{
  UNUSED(raw_expr);
  UNUSED(op_cg_ctx);
  rt_expr.eval_func_ = ObExprMySQLPort::eval_mysql_port;
  return OB_SUCCESS;
}
} // namespace sql
} // namespace oceanbase
