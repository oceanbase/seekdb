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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_EXPORT_SET_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_EXPORT_SET_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase {
namespace sql {
class ObExprExportSet : public ObStringExprOperator {
public:
  explicit ObExprExportSet(common::ObIAllocator& alloc);
  virtual ~ObExprExportSet();
  virtual int calc_result_typeN(
      ObExprResType& type, ObExprResType* types_array, int64_t param_num, common::ObExprTypeCtx& type_ctx) const;
  virtual int cg_expr(ObExprCGCtx& op_cg_ctx, const ObRawExpr& raw_expr, ObExpr& rt_expr) const override;
  static int eval_export_set(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& expr_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
  
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprExportSet);
  // helper func
  static int calc_export_set_inner(const int64_t max_result_size,
      ObString& ret_str, const uint64_t bits, const ObString& on, const ObString& off,
      const ObString& sep, const int64_t n_bits, ObExprStringBuf& string_buf);
};
}  // namespace sql
}  // namespace oceanbase

#endif  // OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_EXPORT_SET_
