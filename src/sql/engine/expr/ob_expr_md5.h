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

#ifndef _OB_EXPR_MD5_H_
#define _OB_EXPR_MD5_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprMd5: public ObStringExprOperator
{
public:
  ObExprMd5();
  explicit  ObExprMd5(common::ObIAllocator &alloc);
  virtual ~ObExprMd5();

public:
    virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &str,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int calc_md5(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  static const common::ObString::obstr_size_t MD5_LENGTH = 16;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprMd5);
};
} /* namespace sql */
} /* namespace oceanbase */

#endif /* _OB_EXPR_MD5_H_ */
