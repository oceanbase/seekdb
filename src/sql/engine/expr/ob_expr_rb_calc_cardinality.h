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

#ifndef OCEANBASE_SQL_OB_EXPR_RB_CALC_CARDINALITY_
#define OCEANBASE_SQL_OB_EXPR_RB_CALC_CARDINALITY_

#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/roaringbitmap/ob_roaringbitmap.h"
#include "lib/roaringbitmap/ob_rb_utils.h"

namespace oceanbase
{
namespace sql
{
class ObExprRbCalcCardinality : public ObFuncExprOperator
{
public:
  explicit ObExprRbCalcCardinality(common::ObIAllocator &alloc, ObExprOperatorType type, const char *name);
  virtual ~ObExprRbCalcCardinality();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx)
                                const override; 
  static int eval_rb_calc_cardinality(const ObExpr &expr,
                                      ObEvalCtx &ctx,
                                      ObDatum &res,
                                      ObRbOperation op,
                                      bool is_null2empty = false);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRbCalcCardinality);
};

class ObExprRbAndCardinality : public ObExprRbCalcCardinality
{
public:
  explicit ObExprRbAndCardinality(common::ObIAllocator &alloc);
  virtual ~ObExprRbAndCardinality();
  static int eval_rb_and_cardinality(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRbAndCardinality);
};

class ObExprRbOrCardinality : public ObExprRbCalcCardinality
{
public:
  explicit ObExprRbOrCardinality(common::ObIAllocator &alloc);
  virtual ~ObExprRbOrCardinality();
  static int eval_rb_or_cardinality(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRbOrCardinality);
};

class ObExprRbXorCardinality : public ObExprRbCalcCardinality
{
public:
  explicit ObExprRbXorCardinality(common::ObIAllocator &alloc);
  virtual ~ObExprRbXorCardinality();
  static int eval_rb_xor_cardinality(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRbXorCardinality);
};

class ObExprRbAndnotCardinality : public ObExprRbCalcCardinality
{
public:
  explicit ObExprRbAndnotCardinality(common::ObIAllocator &alloc);
  virtual ~ObExprRbAndnotCardinality();
  static int eval_rb_andnot_cardinality(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRbAndnotCardinality);
};

class ObExprRbAndNull2emptyCardinality : public ObExprRbCalcCardinality
{
public:
  explicit ObExprRbAndNull2emptyCardinality(common::ObIAllocator &alloc);
  virtual ~ObExprRbAndNull2emptyCardinality();
  static int eval_rb_and_null2empty_cardinality(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRbAndNull2emptyCardinality);
};

class ObExprRbOrNull2emptyCardinality : public ObExprRbCalcCardinality
{
public:
  explicit ObExprRbOrNull2emptyCardinality(common::ObIAllocator &alloc);
  virtual ~ObExprRbOrNull2emptyCardinality();
  static int eval_rb_or_null2empty_cardinality(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRbOrNull2emptyCardinality);
};

class ObExprRbAndnotNull2emptyCardinality : public ObExprRbCalcCardinality
{
public:
  explicit ObExprRbAndnotNull2emptyCardinality(common::ObIAllocator &alloc);
  virtual ~ObExprRbAndnotNull2emptyCardinality();
  static int eval_rb_andnot_null2empty_cardinality(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRbAndnotNull2emptyCardinality);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_RB_CALC_CARDINALITY_
