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

#ifndef _OB_SQL_EXPR_NVL_
#define _OB_SQL_EXPR_NVL_

#include "sql/engine/expr/ob_expr_operator.h"


namespace oceanbase
{
namespace sql
{
class ObExprNvlUtil
{
public:
  static int calc_result_type(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx);
  static int calc_nvl_expr(const ObExpr &expr, ObEvalCtx &ctx,
                           ObDatum &res_datum);
  static int calc_nvl_expr_batch(const ObExpr &expr,
                                  ObEvalCtx &ctx,
                                  const ObBitVector &skip,
                                  const int64_t batch_size);
  // for nvl2()
  static int calc_nvl_expr2(const ObExpr &expr, ObEvalCtx &ctx,
                            ObDatum &res_datum);
};

class ObExprNvl: public ObFuncExprOperator
{
public:

  explicit  ObExprNvl(common::ObIAllocator &alloc);
  virtual ~ObExprNvl();

  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const override;
private:
  // disallow copy
  ObExprNvl(const ObExprNvl &other);
  ObExprNvl &operator=(const ObExprNvl &other);
protected:
  // data members
};
}
}
#endif
