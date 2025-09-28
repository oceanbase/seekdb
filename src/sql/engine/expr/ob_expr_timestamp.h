/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_EXPR_TIMESTAMP_H
#define OCEANBASE_EXPR_TIMESTAMP_H

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_expr_to_temporal_base.h"

namespace oceanbase
{
namespace sql
{
class ObExprTimestamp : public ObFuncExprOperator
{
public:
  explicit ObExprTimestamp(common::ObIAllocator &alloc);
  virtual ~ObExprTimestamp();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_array,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual common::ObCastMode get_cast_mode() const { return CM_NULL_ON_WARN;}
  static int calc_timestamp1(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &result);
  static int calc_timestamp2(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &result);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private :
  //disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprTimestamp);
};

}
}

#endif // OCEANBASE_EXPR_TIMESTAMP_H
