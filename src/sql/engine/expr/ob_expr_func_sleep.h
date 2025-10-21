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

#ifndef _OCEANBASE_SQL_OB_EXPR_FUNC_SLEEP_H_
#define _OCEANBASE_SQL_OB_EXPR_FUNC_SLEEP_H_
#include "lib/ob_name_def.h"
#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprSleep : public ObFuncExprOperator
{
public:
  explicit ObExprSleep(common::ObIAllocator &alloc);
  virtual ~ObExprSleep();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &param,
                                common::ObExprTypeCtx &type_ctx) const;

  static int eval_sleep(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  static const int64_t CHECK_INTERVAL_IN_US = 100 * 1000LL;
  static const int64_t SCALE_OF_SECOND = 9;//ns
private:
  static int sleep(int64_t usec);
  static int get_usec(const common::number::ObNumber &nmb, int64_t &value,
                      common::ObIAllocator &alloc);
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprSleep);
};

} //sql
} //oceanbase
#endif //_OCEANBASE_SQL_OB_EXPR_FUNC_SLEEP_H_
