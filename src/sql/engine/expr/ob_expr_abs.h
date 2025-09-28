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
