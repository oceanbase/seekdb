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

#ifndef OCEANBASE_SQL_OB_EXPR_ARRAY_TO_STRING
#define OCEANBASE_SQL_OB_EXPR_ARRAY_TO_STRING

#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/udt/ob_array_type.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

namespace oceanbase
{
namespace sql
{
class ObExprArrayToString : public ObFuncExprOperator
{
public:
  explicit ObExprArrayToString(common::ObIAllocator &alloc);

  virtual ~ObExprArrayToString();

  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const override;
  static int eval_array_to_string(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int eval_array_to_string_batch(const ObExpr &expr, ObEvalCtx &ctx,
                                        const ObBitVector &skip, const int64_t batch_size);
  static int eval_array_to_string_vector(const ObExpr &expr, ObEvalCtx &ctx,
                                         const ObBitVector &skip, const EvalBound &bound);
  template <typename ResVec>
  static int set_text_res(ObStringBuffer &res_buf, const ObExpr &expr, ObEvalCtx &ctx,
                           ResVec *res_vec, int64_t batch_idx)
  {
    int ret = OB_SUCCESS;
    char *buf = nullptr;
    int64_t len = 0;
    ObTextStringVectorResult<ResVec> str_result(expr.datum_meta_.type_, &expr, &ctx, res_vec, batch_idx);
    if (OB_FAIL(str_result.init_with_batch_idx(res_buf.length(), batch_idx))) {
      SQL_ENG_LOG(WARN, "fail to init result", K(ret), K(res_buf.length()));
    } else if (OB_FAIL(str_result.get_reserved_buffer(buf, len))) {
      SQL_ENG_LOG(WARN, "fail to get reserver buffer", K(ret));
    } else if (len < res_buf.length()) {
      ret = OB_ERR_UNEXPECTED;
      SQL_ENG_LOG(WARN, "get invalid res buf len", K(ret), K(len), K(res_buf.length()));
    } else if (OB_FALSE_IT(MEMCPY(buf, res_buf.ptr(), res_buf.length()))) {
    } else if (OB_FAIL(str_result.lseek(len, 0))) {
      SQL_ENG_LOG(WARN, "failed to lseek res.", K(ret), K(str_result), K(len));
    } else {
      str_result.set_result();
    }
    return ret;
  }
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprArrayToString);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_ARRAY_TO_STRING
