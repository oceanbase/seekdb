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

#ifndef _OB_SQL_EXPR_DATE_ADD_H_
#define _OB_SQL_EXPR_DATE_ADD_H_
#include "sql/engine/expr/ob_expr_operator.h"
namespace oceanbase
{
namespace sql
{

class ObExprDateAdjust : public ObFuncExprOperator
{
public:
  ObExprDateAdjust(common::ObIAllocator &alloc,
                   ObExprOperatorType type,
                   const char *name,
                   int32_t param_num,
                   int32_t dimension = NOT_ROW_DIMENSION);

  virtual ~ObExprDateAdjust();
  virtual int calc_result_type3(ObExprResType &type,
                                ObExprResType &date,
                                ObExprResType &interval,
                                ObExprResType &unit,
                                common::ObExprTypeCtx &type_ctx) const;
  static int calc_date_adjust(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum, bool is_add);
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprDateAdjust);
};

class ObExprDateAdd : public ObExprDateAdjust
{
public:
  explicit  ObExprDateAdd(common::ObIAllocator &alloc);
  virtual ~ObExprDateAdd() {};
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int calc_date_add(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  static int calc_date_add_vector(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const EvalBound &bound);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprDateAdd);
};

class ObExprDateSub : public ObExprDateAdjust
{
public:
  explicit  ObExprDateSub(common::ObIAllocator &alloc);
  virtual ~ObExprDateSub() {};
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int calc_date_sub(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  static int calc_date_sub_vector(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const EvalBound &bound);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprDateSub);
};

class ObExprLastDay : public ObFuncExprOperator
{
public:
  explicit ObExprLastDay(common::ObIAllocator &alloc);
  virtual ~ObExprLastDay() {}
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int calc_last_day(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprLastDay);
};


} //sql
} //oceanbase
#endif //_OCEANBASE_SQL_OB_EXPR_DATE_ADD_H_
