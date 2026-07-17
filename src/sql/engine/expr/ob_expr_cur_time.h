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

#ifndef OCEANBASE_SQL_OB_EXPR_CUR_TIME_H_
#define OCEANBASE_SQL_OB_EXPR_CUR_TIME_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprUtcTimestamp : public ObFuncExprOperator
{
public:
  explicit  ObExprUtcTimestamp(common::ObIAllocator &alloc);
  virtual ~ObExprUtcTimestamp();
  virtual int calc_result_type0(ObExprResType &type, common::ObExprTypeCtx &type_ctx) const;
  static int eval_utc_timestamp(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprUtcTimestamp);
};

class ObExprUtcTime : public ObFuncExprOperator
{
public:
  explicit  ObExprUtcTime(common::ObIAllocator &alloc);
  virtual ~ObExprUtcTime();
  virtual int calc_result_type0(ObExprResType &type, common::ObExprTypeCtx &type_ctx) const;
  static int eval_utc_time(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprUtcTime);
};

class ObExprUtcDate : public ObFuncExprOperator
{
public:
  explicit  ObExprUtcDate(common::ObIAllocator &alloc);
  virtual ~ObExprUtcDate();
  virtual int calc_result_type0(ObExprResType &type, common::ObExprTypeCtx &type_ctx) const;
  static int eval_utc_date(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprUtcDate);
};

class ObExprCurTimestamp : public ObFuncExprOperator
{
public:
  explicit  ObExprCurTimestamp(common::ObIAllocator &alloc);
  virtual ~ObExprCurTimestamp();
  virtual int calc_result_type0(ObExprResType &type, common::ObExprTypeCtx &type_ctx) const;
  static int eval_cur_timestamp(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprCurTimestamp);
};

class ObExprSysdate : public ObFuncExprOperator
{
public:
  explicit  ObExprSysdate(common::ObIAllocator &alloc);
  virtual ~ObExprSysdate();
  virtual int calc_result_type0(ObExprResType &type, common::ObExprTypeCtx &type_ctx) const override;
  static int eval_sysdate(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprSysdate);
};

class ObExprCurDate : public ObFuncExprOperator
{
public:
  explicit  ObExprCurDate(common::ObIAllocator &alloc);
  virtual ~ObExprCurDate();
  virtual int calc_result_type0(ObExprResType &type, common::ObExprTypeCtx &type_ctx) const;
  static int eval_cur_date(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprCurDate);
};

class ObExprCurTime : public ObFuncExprOperator
{
public:
  explicit  ObExprCurTime(common::ObIAllocator &alloc);
  virtual ~ObExprCurTime();
  virtual int calc_result_type0(ObExprResType &type, common::ObExprTypeCtx &type_ctx) const;
  static int eval_cur_time(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprCurTime);
};
} //sql
} //oceanbase
#endif //OCEANBASE_SQL_OB_EXPR_CUR_TIME_H_
