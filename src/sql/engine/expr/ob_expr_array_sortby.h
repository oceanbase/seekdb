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

#ifndef OCEANBASE_SQL_OB_EXPR_ARRAY_SORTBY
#define OCEANBASE_SQL_OB_EXPR_ARRAY_SORTBY

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_expr_array_map.h"
#include "lib/udt/ob_array_type.h"

namespace oceanbase
{
namespace sql
{
class ObExprArraySortby : public ObExprArrayMapCommon
{
public:
  explicit ObExprArraySortby(common::ObIAllocator &alloc);
  explicit ObExprArraySortby(common::ObIAllocator &alloc, ObExprOperatorType type, 
                           const char *name, int32_t param_num, int32_t dimension);
  virtual ~ObExprArraySortby();
  
  virtual int calc_result_typeN(ObExprResType& type,
                                ObExprResType* types,
                                int64_t param_num, 
                                common::ObExprTypeCtx& type_ctx) const override;
  static int eval_array_sortby(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int index_sort(common::ObArenaAllocator &allocator, ObIArrayType *lambda_arr, uint32_t *&sort_idx);
  static int fill_array_by_index(ObIArrayType *src_arr, uint32_t *sort_idx, ObIArrayType *res_arr);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprArraySortby);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_ARRAY_SORTBY
