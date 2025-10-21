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

#ifndef _OB_SQL_EXPR_INNER_TABLE_OPTION_PRINTER_H_
#define _OB_SQL_EXPR_INNER_TABLE_OPTION_PRINTER_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprInnerTableOptionPrinter : public ObStringExprOperator
{
public:
  explicit  ObExprInnerTableOptionPrinter(common::ObIAllocator &alloc);
  virtual ~ObExprInnerTableOptionPrinter();
  virtual int calc_result_type3(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                ObExprResType &type3,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_inner_table_option_printer(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprInnerTableOptionPrinter);
};

class ObExprInnerTableSequenceGetter : public ObExprOperator
{
public:
  explicit  ObExprInnerTableSequenceGetter(common::ObIAllocator &alloc);
  virtual ~ObExprInnerTableSequenceGetter();
  virtual int calc_result_type3(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                ObExprResType &type3,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_inner_table_sequence_getter(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprInnerTableSequenceGetter);
};

}
}
#endif /* _OB_SQL_EXPR_INNER_TABLE_OPTION_PRINTER_H_ */
