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

#ifndef _OCEANBASE_SQL_OB_EXPR_TIME_STMAP_ADD_H_
#define _OCEANBASE_SQL_OB_EXPR_TIME_STMAP_ADD_H_
#define USING_LOG_PREFIX  SQL_ENG
#include "lib/ob_name_def.h"
#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/session/ob_sql_session_info.h"
namespace oceanbase
{
namespace common
{
struct ObTimeConvertCtx;
}
namespace sql
{
class ObExprTimeStampAdd : public ObFuncExprOperator
{
public:
  explicit  ObExprTimeStampAdd(common::ObIAllocator &alloc);
  virtual ~ObExprTimeStampAdd();
  virtual int calc_result_type3(ObExprResType &type,
                                ObExprResType &unit,
                                ObExprResType &interval,
                                ObExprResType &timestamp,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                                ObExpr &rt_expr) const override;
  static int calc(const int64_t unit_value, common::ObTime &ot, const int64_t ts,
                  const common::ObTimeConvertCtx &cvrt_ctx, int64_t interval,
                  int64_t &delta);
  static int calc_timestamp_add_vector(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const EvalBound &bound);

  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprTimeStampAdd);
};

} //sql
} //oceanbase
#endif //_OCEANBASE_SQL_OB_EXPR_TIME_STMAP_ADD_H_
