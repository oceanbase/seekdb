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

#ifndef _OB_EXPR_UUID_SHORT_H_
#define _OB_EXPR_UUID_SHORT_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprUuidShort : public ObFuncExprOperator
{
public:
  explicit ObExprUuidShort(common::ObIAllocator &alloc);
  virtual ~ObExprUuidShort();

  virtual int calc_result_type0(ObExprResType &type,
                              common::ObExprTypeCtx &type_ctx) const;

  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int eval_uuid_short(const ObExpr &expr,
                      ObEvalCtx &ctx,
                      ObDatum &expr_datum);
private:
  static uint64_t generate_uuid_short();
  DISALLOW_COPY_AND_ASSIGN(ObExprUuidShort) const;
};

inline int ObExprUuidShort::calc_result_type0(ObExprResType &type,
                                         common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  type.set_uint64();
  type.set_scale(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObUInt64Type].scale_);
  type.set_precision(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObUInt64Type].precision_);
  return common::OB_SUCCESS;
}

}
}
#endif  /* _OB_EXPR_UUID_SHORT_H_ */

// select uuid_short();
