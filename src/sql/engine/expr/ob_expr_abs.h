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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_ABS_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_ABS_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprAbs : public ObExprOperator
{
  typedef int (*abs_func)(common::ObObj &res, const common::ObObj &param, common::ObExprCtx &expr_ctx);
public:
  explicit  ObExprAbs(common::ObIAllocator &alloc);
  ~ObExprAbs() {};

  virtual int calc_result_type1(ObExprResType &type, ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;

  virtual int assign(const ObExprOperator &other);

  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                     const ObRawExpr &raw_expr,
                     ObExpr &rt_expr) const override;

private:
  //tinyint, mediumint, smallint, int32
  //int64
  //utiniyint, umediumint, usmallint
  //uint32 uint64
  //float
  //double
  //ufloat
  //udouble
  //number
  //unumber
  //null
  //hexstring
  //year
  //others(datetime, time, varchar,etc)
  //bit
  //bit
  //json
  static int abs_json(common::ObObj &res,
                      const common::ObObj &param,
                      common::ObExprCtx &expr_ctx);

  static common::ObObjType calc_param_type(const common::ObObjType orig_param_type);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprAbs);
private:
  abs_func func_;
};

} // namespace sql
} // namespace oceanbase
#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_ABS_
