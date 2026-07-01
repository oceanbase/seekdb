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

#ifndef OCEANBASE_SQL_OB_EXPR_JSON_LENGTH_H_
#define OCEANBASE_SQL_OB_EXPR_JSON_LENGTH_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/json_type/ob_json_path.h"
#include "sql/engine/expr/ob_expr_multi_mode_func_helper.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
class ObExprJsonLength : public ObFuncExprOperator
{
public:
  explicit ObExprJsonLength(common::ObIAllocator &alloc);
  virtual ~ObExprJsonLength();
  virtual int calc_result_typeN(ObExprResType& type,
                                ObExprResType* types_stack,
                                int64_t param_num,
                                ObExprTypeCtx& type_ctx) const override;

  static int calc(ObEvalCtx &ctx, const ObDatum &data1, ObDatumMeta meta1, bool has_lob_header1,
                  const ObDatum *data2, ObDatumMeta meta2, bool has_lob_header2,
                  MultimodeAlloctor *allocator, ObDatum &res,
                  ObJsonPathCache* path_cache);
  static int eval_json_length(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  virtual bool need_rt_ctx() const override { return true; }
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprJsonLength);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_JSON_LENGTH_H_
