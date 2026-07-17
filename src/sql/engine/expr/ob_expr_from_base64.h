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

#ifndef OCEANBASE_SQL_ENGINE_OB_EXPR_FROM_BASE64_
#define OCEANBASE_SQL_ENGINE_OB_EXPR_FROM_BASE64_

#include "sql/engine/expr/ob_expr_operator.h"
#include "share/object/ob_obj_cast.h"

namespace oceanbase
{
namespace sql
{
class ObExprFromBase64 : public ObStringExprOperator {
public:
  explicit ObExprFromBase64(common::ObIAllocator &alloc);
  virtual ~ObExprFromBase64();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &str,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const;
  static int eval_from_base64(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int eval_from_base64_batch(const ObExpr &expr, ObEvalCtx &ctx,
                              const ObBitVector &skip,
                              const int64_t batch_size);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprFromBase64);

  static const int64_t NCHAR_PER_BASE64 = 4;
  static const int64_t NCHAR_PER_BASE64_GROUP = 3;
  static inline ObLength base64_needed_decoded_length(ObLength length_of_encoded_data)
  {
    return (ObLength) ceil(length_of_encoded_data * NCHAR_PER_BASE64_GROUP / NCHAR_PER_BASE64);
  }
};
}
}

#endif //OCEANBASE_SQL_ENGINE_OB_EXPR_FROM_BASE64_
