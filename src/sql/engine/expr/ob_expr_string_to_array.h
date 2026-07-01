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

#ifndef OCEANBASE_SQL_OB_EXPR_STRING_TO_ARRAY
#define OCEANBASE_SQL_OB_EXPR_STRING_TO_ARRAY 

#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/udt/ob_array_utils.h"

namespace oceanbase
{
namespace sql
{
class ObExprStringToArray : public ObFuncExprOperator
{
public:
  explicit ObExprStringToArray(common::ObIAllocator &alloc);
  virtual ~ObExprStringToArray();

  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const override;
  static int eval_string_to_array(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int eval_string_to_array_batch(const ObExpr &expr, ObEvalCtx &ctx,
                                        const ObBitVector &skip, const int64_t batch_size);
  static int eval_string_to_array_vector(const ObExpr &expr, ObEvalCtx &ctx,
                                         const ObBitVector &skip, const EvalBound &bound);
  static int add_value_str_to_array(ObArrayBinary *binary_array, std::string value_str, bool has_null_str, std::string null_str);
  static int string_to_array(ObArrayBinary *binary_array,
                             std::string arr_str, std::string delimiter, std::string null_str,
                             ObCollationType cs_type, bool has_arr_str, bool has_delimiter, bool has_null_str);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprStringToArray);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_STRING_TO_ARRAY
